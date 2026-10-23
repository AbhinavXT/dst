#include "testutil.h"

#include "cabio.h"
#include "capturedecoder.h"

#ifdef DL_HAVE_SERIAL
#include "cabiosimwindow.h"
#include "messagedispatcher.h"
#include "seriallink.h"
#include "serialmanager.h"
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>

#if defined(DL_HAVE_SERIAL) && defined(Q_OS_UNIX)
#  include <fcntl.h>
#  include <termios.h>
#  include <unistd.h>
#  if defined(Q_OS_MACOS)
#    include <util.h>
#  else
#    include <pty.h>
#  endif
#endif

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSet>

// =============================================================================
//  Session 324 — the Cab I/O Simulator: the VCC <-> IOA card protocol.
//
//  Abhinav sent STRUCT_INTERNAL_MESSAGE_HEADER, the input request / reply and
//  output data frames, the SIFA-extended and old cab structs, and asked for
//  an input simulator (toggle, NO/NC four states, 250 ms flicker) that sends
//  to the VCC over serial, and a view of the output data the VCC sends.
//
//  The bit layouts are checked against kavach.xml's DIP1 / DIP2 decode of
//  every distinct real @dip1 / @dip2 payload in replay/: the simulator's
//  input_data must mean what the VCC logs it as. No real VCC <-> IOA frame
//  exists yet (Abhinav is collecting them), so the frames are built here
//  from the structs and read back.
// =============================================================================

namespace {

using namespace CabIo;

// Every distinct payload of `tag` lines ("@dip1_") in replay/.
QSet<QString> realPayloads(const char *tag)
{
    QSet<QString> out;
    QDirIterator it(QStringLiteral(DL_SRC_DIR "/replay"), { QStringLiteral("*.cap") }, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile f(it.next());
        if (!f.open(QIODevice::ReadOnly)) continue;
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            if (!l.startsWith(tag)) continue;
            const QList<QByteArray> parts = l.split(' ');
            if (parts.size() < 7) continue;
            QStringList bytes;
            for (int i = 3; i < parts.size(); ++i) bytes << QString::fromLatin1(parts.at(i));
            out.insert(bytes.join(QLatin1Char(' ')));
        }
    }
    return out;
}

QString valueOf(const QVector<FieldRow> &rows, const QString &field)
{
    for (const FieldRow &r : rows)
        if (r.field.trimmed() == field) return r.value.trimmed();
    return QStringLiteral("<missing>");
}

quint32 wordOf(const QString &hexBytes)
{
    quint32 w = 0;
    const QStringList b = hexBytes.split(QLatin1Char(' '));
    for (int i = 0; i < 4 && i < b.size(); ++i) w |= quint32(b.at(i).toUInt(nullptr, 16)) << (8 * i);
    return w;
}

// Each input of `layout`, for each real payload, against the schema's rows.
int crossCheck(const InputLayout &layout, const char *tag, const char *captype, int *payloads)
{
    int mismatches = 0;
    const QSet<QString> real = realPayloads(tag);
    *payloads = real.size();
    for (const QString &hexBytes : real) {
        const QString line = QStringLiteral("@%1_1_1 2026-10-14T11:10:22 1 %2").arg(QLatin1String(captype), hexBytes);
        const QVector<FieldRow> rows = CaptureDecoder::describe(CaptureDecoder::parseLine(line));
        InputBank bank(&layout);
        bank.setBase(wordOf(hexBytes));
        for (int i = 0; i < bank.count(); ++i) {
            const Input &in = bank.input(i);
            const int s = bank.state(i);
            if (in.isPair()) {
                const QString no = valueOf(rows, QStringLiteral("%1 %2_no").arg(in.noBit + 1).arg(in.name));
                const QString nc = valueOf(rows, QStringLiteral("%1 %2_nc").arg(in.ncBit + 1).arg(in.name));
                if (no != QString::number((s >> 1) & 1) || nc != QString::number(s & 1)) ++mismatches;
            } else {
                const QString v = valueOf(rows, QStringLiteral("%1 %2").arg(in.bit + 1).arg(in.name));
                if (v != QString::number(s)) ++mismatches;
            }
        }
    }
    return mismatches;
}

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

quint32 coverage(const InputLayout &l, bool *overlap)
{
    quint32 all = 0;
    *overlap = false;
    for (const Input &in : l.inputs) {
        if (all & in.mask()) *overlap = true;
        all |= in.mask();
    }
    return all;
}

}  // namespace

TEST_SUITE(session324)
{
    // ---- the layouts are the structs, bit for bit ---------------------------------------
    {
        bool overlap = false;
        CHECK(coverage(sifaSet1(), &overlap) == 0x7FFFFFFFu && !overlap,
              "SET1: bits 0-30 each owned once, bit 31 (padding) by none");
        CHECK(coverage(sifaSet2(), &overlap) == 0x3FF003FFu && !overlap,
              "SET2: bits 0-9 and 20-29, padding1 and padding2 by none");
        CHECK(coverage(oldCabInputs(), &overlap) == 0xFFFDFFFFu && !overlap,
              "STRUCT_CAB_INPUT: all 32 but bit 17 (padding)");
        int pairs = 0;
        for (const Input &in : sifaSet1().inputs) pairs += in.isPair();
        for (const Input &in : sifaSet2().inputs) pairs += in.isPair();
        CHECK(pairs == 8 + 9, "17 NO/NC contacts: 8 on the DMIs, 9 in set 2");

        int n1 = 0, n2 = 0;
        const int bad1 = crossCheck(sifaSet1(), "@dip1_", "dip1", &n1);
        const int bad2 = crossCheck(sifaSet2(), "@dip2_", "dip2", &n2);
        CHECK(n1 >= 10 && n2 >= 3, qPrintable(QStringLiteral("fixture: real @dip1 / @dip2 payloads (%1 / %2 distinct)").arg(n1).arg(n2)));
        CHECK(bad1 == 0, qPrintable(QStringLiteral("every SET1 input reads as kavach.xml's DIP1 decodes it (%1 mismatches)").arg(bad1)));
        CHECK(bad2 == 0, qPrintable(QStringLiteral("every SET2 input reads as DIP2, the swapped sifa_or_veb pair too (%1 mismatches)").arg(bad2)));

        CHECK(outputBits(Cab::Sifa).size() == 16 && outputBits(Cab::Old).size() == 16, "16 output bits each");
        QSet<int> bits;
        for (const OutputBit &o : outputBits(Cab::Sifa)) bits.insert(o.bit);
        CHECK(bits.size() == 16, "SIFA outputs: bits 0-15 once each");
        CHECK(outputBits(Cab::Sifa).at(0).name == QLatin1String("dmi_sos_red")
                  && outputBits(Cab::Old).at(0).name == QLatin1String("kavach_ok"),
              "bit 0 is dmi_sos_red (SIFA) and kavach_ok (old)");
        CHECK(displayName(QStringLiteral("dmi1_leading_non_leading")) == QLatin1String("DMI1 lead/non-lead")
                  && displayName(QStringLiteral("kavach_isolation_fb")) == QLatin1String("Kavach isolation FB"),
              "names read as words");
    }

    // ---- an input bank: toggles, four states, flicker ------------------------------------
    {
        InputBank b(&sifaSet1());
        b.reset();
        // dmi1_sos: NO bit 1, NC bit 0. At rest NO 0 NC 1 = "01".
        CHECK(b.state(0) == 1 && b.word() == 0x5555u, "reset: every DMI contact at rest (NC 1), others 0");
        CHECK(InputBank::stateText(b.input(0), b.state(0)) == QLatin1String("01"), "a pair reads NO NC");
        b.toggle(0);
        CHECK(b.state(0) == 2 && (b.word() & 3u) == 2u, "a contact switches On: NO 1 (bit 1), NC 0");
        b.toggle(0);
        CHECK(b.state(0) == 1, "and Off again: NO 0, NC 1");
        b.setState(0, 3);
        CHECK(b.state(0) == 3 && (b.word() & 3u) == 3u, "the fault both 1 is set directly");
        b.toggle(0);
        CHECK(b.state(0) == 1, "switching a faulted contact puts it Off");
        b.setState(0, 0);
        CHECK(b.state(0) == 0 && (b.word() & 3u) == 0u, "the fault both 0");
        b.setState(0, 1);

        const int cab1 = 8;                       // cab1_active, bit 23
        CHECK(b.input(cab1).bit == 23, "fixture: cab1_active is bit 23");
        b.toggle(cab1);
        CHECK(b.word() & (1u << 23), "a plain input toggles its bit");
        const quint32 base = b.word();

        b.setFlicker(0, true);
        b.setFlicker(cab1, true);
        CHECK(b.flickerCount() == 2 && b.word() == base, "flicker on: phase 0 is the state set");
        b.tick();
        CHECK(b.liveState(0) == 2 && !(b.word() & (1u << 23)), "phase 1: the pair 01 -> 10, the input inverts");
        CHECK(b.state(0) == 1, "and the state set is unchanged");
        b.tick();
        CHECK(b.word() == base, "phase 0 again");
        b.tick();
        b.stopFlicker();
        CHECK(b.word() == base && b.flickerCount() == 0, "stopping returns to the state set");
        b.setState(0, 3);
        b.setFlicker(0, true);
        b.tick();
        CHECK(b.liveState(0) == 0, "11 flickers with 00");
    }

    // ---- frames --------------------------------------------------------------------------
    WireFormat wf;
    {
        const QByteArray req = inputRequest(5, 0x1234, wf);
        CHECK(req.size() == 14, "input request: 14 bytes");
        CHECK(hex(req.left(8)) == QLatin1String("AA AA 0E 01 05 15 34 12"),
              "AA AA, length 14, VCC (1) -> 5, 0x15, seq little-endian");
        CHECK(quint8(req.at(12)) == 0xBB && quint8(req.at(13)) == 0xBB, "ends BB BB");

        // A real @dip1 payload as the reply's input_data.
        const quint32 w = wordOf(QStringLiteral("95 95 F8 41"));
        const QByteArray rep = inputReply(5, kVccId, 0x1234, w, kHealthy, wf);
        CHECK(rep.size() == 19, "input reply: 19 bytes (packed)");
        CHECK(hex(rep.left(13)) == QLatin1String("AF AF 13 05 01 51 34 12 95 95 F8 41 13"),
              "AF AF, 5 -> VCC, 0x51, input_data as the @dip1 log prints it, vic_health 13");
        CHECK(quint8(rep.at(17)) == kVccRxEof && quint8(rep.at(18)) == kVccRxEof, "ends FA FA");

        const QByteArray out = outputData(13, 7, 0x2041, wf);
        CHECK(out.size() == 16 && hex(out.left(10)) == QLatin1String("CC CC 10 01 0D 2D 07 00 41 20"),
              "output data: 16 bytes, CC CC, VCC -> 13, 0x2D, data_byte1 then data_byte2");

        Frame f;
        CHECK(parse(rep, &f) && f.isInputReply() && f.inputWord() == w && f.health() == kHealthy && f.known,
              "the reply reads back");
        CHECK(f.crcMatches.contains(wf.crc), "its CRC is the one it was built with");
        CHECK(parse(out, &f) && f.isOutputData() && f.outputWord() == 0x2041, "the output data reads back");

        WireFormat z = wf;
        z.crc = Crc::Crc32;
        Frame g;
        CHECK(parse(inputRequest(9, 1, z), &g) && g.crcMatches == QVector<Crc>{ Crc::Crc32 },
              "a received CRC names its algorithm (CRC-32 only)");
        CHECK(crcOf(Crc::Crc32, QByteArray("123456789")) == 0xCBF43926u
                  && crcOf(Crc::Jam, QByteArray("123456789")) == 0x340BC6D9u,
              "CRC-32 and JAMCRC check values");

        z.length = LengthRule::HeaderAndPayload;
        CHECK(quint8(inputReply(5, 1, 0, 0, 3, z).at(2)) == 11, "message_length = header + payload: 11");
        z.length = LengthRule::Payload;
        CHECK(quint8(inputReply(5, 1, 0, 0, 3, z).at(2)) == 5, "message_length = payload: 5");

        CHECK(!parse(req.left(13), &f), "a frame cut short is not a frame");
        QByteArray bent = rep;
        bent[18] = 0x00;
        CHECK(!parse(bent, &f), "nor is one without its end pair");
    }

    // ---- a stream in pieces ------------------------------------------------------------------
    {
        QByteArray stream = QByteArray::fromHex("00FF11");                 // noise
        stream += inputRequest(5, 1, wf);
        stream += QByteArray::fromHex("AA");                               // a lone AA
        stream += inputReply(5, 1, 1, 0x41F89595u, 3, wf);
        stream += outputData(14, 2, 0x0003, wf);
        // Analog data: its payload is not known, so it is found by its end pair.
        Header h;
        h.src = 15; h.dest = 1; h.msgId = kMsgAnalogToVcc; h.seq = 3;
        stream += build(kVccRxSof, h, QByteArray::fromHex("0102030405060708"), wf);

        Scanner s;
        QVector<Frame> got;
        for (char c : stream) got += s.feed(QByteArray(1, c));
        CHECK(got.size() == 4, qPrintable(QStringLiteral("four frames from a byte-at-a-time stream (%1)").arg(got.size())));
        if (got.size() == 4) {
            CHECK(got[0].isInputRequest() && got[1].isInputReply() && got[2].isOutputData(), "in order, by kind");
            CHECK(got[3].h.msgId == kMsgAnalogToVcc && !got[3].known && got[3].payload.size() == 8,
                  "analog data by its end bytes, 8 bytes of payload");
        }
        CHECK(s.skippedBytes() == 4 && s.pending() == 0, "the noise and the lone AA skipped, nothing held");
    }

#ifdef DL_HAVE_SERIAL
    // ---- the window: answers the VCC, flickers, lights the lamps -------------------------------
    {
        CabIoSimWindow w(nullptr);
        QVector<QByteArray> sent;
        w.setWriteSink([&](const QByteArray &b) { sent << b; });
        w.setCab(Cab::Sifa);
        w.setWireFormat(WireFormat());
        w.setRespond(CabIoSimWindow::Respond::Answer);
        w.setHealth(kHealthy);
        CHECK(w.bankCount() == 2, "SIFA: two banks, set 1 and set 2");
        w.setBankIds(0, { 5, 6, 7, 8 });
        w.setBankIds(1, { 9, 10, 11, 12 });

        // Set 1 as a real @dip1 reads.
        const quint32 real = wordOf(QStringLiteral("95 95 F8 41"));
        w.bank(0)->stopFlicker();
        w.bank(0)->setBase(real);
        w.feedBytes(inputRequest(6, 0x0102, WireFormat()));
        CHECK(sent.size() == 1, "a request to 6 is answered once");
        Frame f;
        if (!sent.isEmpty() && parse(sent.last(), &f)) {
            CHECK(f.isInputReply() && f.h.src == 6 && f.h.dest == kVccId && f.h.seq == 0x0102,
                  "from 6 to the VCC, the request's seq_num");
            CHECK(f.inputWord() == real && f.health() == kHealthy, "carrying set 1's word and vic_health");
        } else {
            CHECK(false, "the reply parses");
        }

        w.feedBytes(inputRequest(10, 9, WireFormat()));
        CHECK(sent.size() == 2 && parse(sent.last(), &f) && f.h.src == 10 && f.inputWord() == w.bank(1)->word(),
              "a request to 10 is answered with set 2");
        w.feedBytes(inputRequest(13, 9, WireFormat()));
        CHECK(sent.size() == 2, "a request to an id not ticked is not answered");
        w.setBankIds(0, { 5 });
        w.feedBytes(inputRequest(6, 9, WireFormat()));
        CHECK(sent.size() == 2, "nor once 6 is unticked");

        // Flicker: the next reply carries the other phase.
        w.bank(0)->setFlicker(0, true);
        w.tickFlicker();
        w.feedBytes(inputRequest(5, 10, WireFormat()));
        CHECK(sent.size() == 3 && parse(sent.last(), &f) && (f.inputWord() ^ real) == 3u,
              "a flickering dmi1_sos goes out as its complement on the next phase");
        CHECK(w.requestsSeen() == 5 && w.repliesSent() == 3, "requests heard and replies counted");

        // Send now, unasked: one frame per ticked id.
        sent.clear();
        w.sendNow();
        CHECK(sent.size() == 1 + 4, "Send now: one frame per ticked id (5, and 9-12)");

        // Outputs.
        w.feedBytes(outputData(13, 1, 0x0001, WireFormat()));
        w.feedBytes(outputData(13, 2, 0x2001, WireFormat()));
        CHECK(w.outputSeen() && w.outputWord() == 0x2001, "output data: the latest word");
        CHECK(w.outputChanges(13) == 1 && w.outputChanges(0) == 0, "emergency_brake changed once, dmi_sos_red never");
        w.feedBytes(outputData(14, 2, 0x0000, WireFormat()));
        CHECK(w.outputWord() == 0x2001, "the lamps follow output 13; 14's different word is reported, not drawn");

        // A frame with another CRC: the Frames tab says which one it is.
        WireFormat other;
        other.crc = Crc::Crc32;
        w.feedBytes(inputRequest(99, 1, other));
        CHECK(w.crcHint().contains(QLatin1String("CRC-32")), "a received frame's CRC is named");
        w.flushLog();
        CHECK(w.logRows() >= 8, qPrintable(QStringLiteral("the Frames tab logs both ways (%1 rows)").arg(w.logRows())));
        CHECK(w.nextReplyText().contains(QLatin1String("AF AF")), "the next reply is previewed");

        w.setCab(Cab::Old);
        CHECK(w.bankCount() == 1 && w.bank(0)->layout() == &oldCabInputs(), "old cab: one bank, STRUCT_CAB_INPUT");
        w.setCab(Cab::Sifa);
        w.close();
    }

#ifdef Q_OS_UNIX
    // ---- over a real serial line: a pty stands in for the VCC ----------------------------------
    {
        int master = -1, slave = -1;
        char name[256] = {};
        if (::openpty(&master, &slave, name, nullptr, nullptr) != 0) { CHECK(false, "openpty"); return; }
        termios t{};
        tcgetattr(master, &t);
        cfmakeraw(&t);
        tcsetattr(master, TCSANOW, &t);
        ::close(slave);
        ::fcntl(master, F_SETFL, ::fcntl(master, F_GETFL) | O_NONBLOCK);
        auto vccWrite = [master](const QByteArray &b) { (void)::write(master, b.constData(), size_t(b.size())); };
        auto vccRead = [master]() {
            QByteArray all;
            char buf[512];
            for (;;) {
                const ssize_t n = ::read(master, buf, sizeof buf);
                if (n <= 0) break;
                all.append(buf, int(n));
            }
            return all;
        };

        const QString port = QString::fromLocal8Bit(name);
        MessageDispatcher disp;
        SerialManager mgr(&disp);
        {
            CabIoSimWindow w(&mgr);
            w.setAttribute(Qt::WA_DeleteOnClose, false);
            w.setCab(Cab::Sifa);
            w.setWireFormat(WireFormat());
            w.setRespond(CabIoSimWindow::Respond::Answer);
            w.setHealth(kHealthy);
            w.setBankIds(0, { 5, 6, 7, 8 });
            w.setBankIds(1, { 9, 10, 11, 12 });
            const quint32 real = wordOf(QStringLiteral("95 95 F8 41"));
            w.bank(0)->stopFlicker();
            w.bank(0)->setBase(real);

            CHECK(w.openPort(port, 115200), "the simulator opens the port");
            QElapsedTimer clock;
            clock.start();
            while (!w.isPortOpen() && clock.elapsed() < 3000) settle(20);
            CHECK(w.isPortOpen() && mgr.isBinary(port) && !mgr.feeds(port),
                  "open, marked binary, not fed to a console tab");

            // The request split in two writes, as a slow line delivers it.
            const QByteArray req = inputRequest(7, 0x0A0B, WireFormat());
            vccWrite(req.left(5));
            settle(60);
            vccWrite(req.mid(5));
            QByteArray got;
            clock.restart();
            while (got.size() < 19 && clock.elapsed() < 3000) { settle(20); got += vccRead(); }
            Frame f;
            CHECK(parse(got, &f) && f.isInputReply() && f.h.src == 7 && f.h.seq == 0x0A0B && f.inputWord() == real,
                  qPrintable(QStringLiteral("the VCC reads input 7's reply over the line: %1").arg(hex(got))));

            for (int i = 0; i < 12; ++i) vccWrite(inputRequest(quint8(5 + i % 8), quint16(i), WireFormat()) + "\r\n");
            settle(400);
            (void)vccRead();
            const SerialLineHealth::Stats h = mgr.health(port, QDateTime::currentMSecsSinceEpoch());
            CHECK(h.lines == 0 && !h.failing() && mgr.fedLines(port) == 0,
                  "binary frames with CR LF in them: no line health judged (no \"check baud\"), nothing fed");

            w.close();
            settle(100);
            CHECK(!mgr.link(port)->isOpen() && !mgr.isBinary(port), "closing the window closes the port it opened");
        }
        ::close(master);
    }
#endif
#endif
}
