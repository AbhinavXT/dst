#include "testutil.h"

#include "cabio.h"

#ifdef DL_HAVE_SERIAL
#include "cabiosimwindow.h"
#endif

#include <QFile>
#include <QSet>

// =============================================================================
//  Session 325 — the Cab I/O Simulator against real frames.
//
//  Abhinav captured the VCC <-> IOA card traffic on the bench: input requests
//  and data for controllers 5-12, output data to 13 / 14, readback requests
//  to 17 / 18 and the cards' readback (tests/fixtures/cabio_frames_20261023.txt,
//  44 frames). They settle what session 324 had to leave to the operator:
//  JAMCRC init 0 over header + payload, little-endian; message_length the
//  whole frame; seq_num echoed; vic_health 0x13. And they give the readback,
//  which the simulator can now answer.
//
//  The test: every real frame parses with the defaults, and the builders
//  rebuild each one byte for byte; the window, fed the VCC's real requests,
//  answers with the real cards' bytes.
// =============================================================================

namespace {

using namespace CabIo;

QVector<QByteArray> realFrames()
{
    QVector<QByteArray> out;
    QFile f(QStringLiteral(DL_SRC_DIR "/tests/fixtures/cabio_frames_20261023.txt"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.startsWith("AA AA") || l.startsWith("AF AF") || l.startsWith("CC CC"))
            out << QByteArray::fromHex(QByteArray(l).replace(' ', ""));
    }
    return out;
}

// The real frame from `src` to `dest` with message `msg` and `seq`.
QByteArray find(const QVector<QByteArray> &frames, quint8 src, quint8 dest, quint8 msg, quint16 seq)
{
    for (const QByteArray &b : frames) {
        Frame f;
        if (parse(b, &f) && f.h.src == src && f.h.dest == dest && f.h.msgId == msg && f.h.seq == seq) return b;
    }
    return QByteArray();
}

// What the builders make of a parsed frame.
QByteArray rebuild(const Frame &f, const WireFormat &wf)
{
    if (f.isInputRequest())  return inputRequest(f.h.dest, f.h.seq, wf);
    if (f.isInputReply())    return inputReply(f.h.src, f.h.dest, f.h.seq, f.inputWord(), f.health(), wf);
    if (f.isOutputData())    return outputData(f.h.dest, f.h.seq, f.outputWord(), wf);
    if (f.isOutputRequest()) return outputRequest(f.h.dest, f.h.seq, wf);
    if (f.isReadback())      return readback(f.h.src, f.h.dest, f.h.seq, f.pins(), f.health(), wf);
    return QByteArray();
}

}  // namespace

TEST_SUITE(session325)
{
    const QVector<QByteArray> frames = realFrames();
    CHECK(frames.size() == 44, "fixture: 44 real frames");
    if (frames.size() != 44) return;
    const WireFormat wf;     // the defaults

    // ---- every real frame: found, its CRC the default, rebuilt byte for byte ---------------
    {
        int parsed = 0, known = 0, crcDefault = 0, lengthWhole = 0, rebuilt = 0;
        QSet<int> kinds;
        for (const QByteArray &b : frames) {
            Frame f;
            if (!parse(b, &f)) continue;
            ++parsed;
            known += f.known;
            crcDefault += f.crcMatches == QVector<Crc>{ Crc::DmiJam };
            lengthWhole += f.h.length == b.size();
            rebuilt += rebuild(f, wf) == b;
            kinds.insert(f.isInputRequest() ? 1 : f.isInputReply() ? 2 : f.isOutputData() ? 3
                         : f.isOutputRequest() ? 4 : f.isReadback() ? 5 : 0);
        }
        CHECK(parsed == 44 && known == 44, "all 44 parse, each at a size the scanner knows");
        CHECK(kinds == QSet<int>({ 1, 2, 3, 4, 5 }), "all five kinds: input request and data, output data, readback request and data");
        CHECK(crcDefault == 44, "every CRC is JAMCRC init 0 (the DMI frame's) over header + payload, LE -- and only that");
        CHECK(lengthWhole == 44, "message_length is the whole frame (14, 19, 16, 31)");
        CHECK(rebuilt == 44, "the builders, with the default wire format, rebuild every real frame byte for byte");

        Scanner s;
        QVector<Frame> got;
        QByteArray stream;
        for (const QByteArray &b : frames) stream += b;
        for (int i = 0; i < stream.size(); i += 7) got += s.feed(stream.mid(i, 7));
        CHECK(got.size() == 44 && s.skippedBytes() == 0 && s.pending() == 0,
              "the capture as one stream in 7-byte pieces: 44 frames, nothing skipped");
    }

    // ---- what the frames say about the protocol ------------------------------------------------
    {
        Frame req, rep, out, rbReq, rb;
        parse(frames.at(0), &req);
        parse(frames.at(1), &rep);
        CHECK(req.isInputRequest() && rep.isInputReply() && rep.h.seq == req.h.seq && rep.h.src == req.h.dest
                  && rep.h.dest == kVccId,
              "input data answers its request: from the card asked, to the VCC, the request's seq_num");
        CHECK(rep.health() == 0x13 && kHealthy == 0x13, "vic_health is 0x13 (the default now)");
        CHECK(rep.inputWord() == 0x100051F0u, "controller 5's input_data: 0x100051F0");

        parse(find(frames, kVccId, 13, kMsgVccOutputData, 0x1B), &out);
        parse(find(frames, kVccId, 17, kMsgVccOutputReq, 0x1B), &rbReq);
        parse(find(frames, 13, kVccId, kMsgOutputToVcc, 0x1B), &rb);
        CHECK(out.isOutputData() && out.outputWord() == 0x03E1, "output data to 13: 0x03E1 (data_byte1 E1, data_byte2 03)");
        CHECK(rbReq.isOutputRequest() && rb.isReadback() && readbackCard(rbReq.h.dest) == rb.h.src,
              "the readback request to 17 is answered by 13, with the same seq_num");
        CHECK(rb.pins() == readbackPins(0x03E1) && rb.health() == kHealthy,
              "the readback's pins are 0x0C where 0x03E1 has a 1 and 0x17 where it has a 0; health 0x13");
    }

#ifdef DL_HAVE_SERIAL
    // ---- the window answers the VCC's real requests with the real cards' bytes ------------------
    {
        CabIoSimWindow w(nullptr);
        QVector<QByteArray> sent;
        w.setWriteSink([&](const QByteArray &b) { sent << b; });
        w.setCab(Cab::Sifa);
        w.setWireFormat(WireFormat());
        w.setHealth(kHealthy);
        w.setRespond(CabIoSimWindow::Respond::Answer);
        w.setBankIds(0, { 5, 6, 7, 8 });
        w.setBankIds(1, { 9, 10, 11, 12 });
        w.setAnswerReadback(false);
        for (int b = 0; b < 2; ++b) w.bank(b)->stopFlicker();
        w.bank(0)->setBase(0x100051F0u);
        w.bank(1)->setBase(0);

        int exact = 0, asked = 0;
        for (int i = 0; i + 1 < frames.size(); ++i) {
            Frame f;
            if (!parse(frames.at(i), &f) || !f.isInputRequest()) continue;
            ++asked;
            sent.clear();
            w.feedBytes(frames.at(i));
            exact += sent.size() == 1 && sent.first() == frames.at(i + 1);
        }
        CHECK(asked == 16 && exact == 16,
              qPrintable(QStringLiteral("all 16 real input requests answered with the real card's bytes (%1 of %2)")
                             .arg(exact).arg(asked)));

        // Readback: off by default -- a real output card may be on the line.
        const QByteArray rbReq18 = find(frames, kVccId, 18, kMsgVccOutputReq, 0x1C);
        sent.clear();
        w.feedBytes(rbReq18);
        CHECK(sent.isEmpty(), "readback requests are not answered unless asked to");
        w.setAnswerReadback(true);
        w.feedBytes(rbReq18);
        CHECK(sent.isEmpty(), "nor for a card that has been sent no output data yet");

        w.feedBytes(find(frames, kVccId, 14, kMsgVccOutputData, 0x1C));
        w.feedBytes(rbReq18);
        CHECK(sent.size() == 1 && sent.first() == find(frames, 14, kVccId, kMsgOutputToVcc, 0x1C),
              "answering on: card 14's readback is the real card's, byte for byte");
        CHECK(w.readbacksSent() == 1, "and counted");
        w.close();
    }
#endif
}
