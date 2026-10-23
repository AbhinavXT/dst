#include "testutil.h"

#include "cabio.h"
#include "settings.h"

#ifdef DL_HAVE_SERIAL
#include "cabiosimwindow.h"
#endif

#include <QFile>
#include <QSettings>

// =============================================================================
//  Session 329 — Cab I/O Simulator: a Readback -> VCC tab, and the inputs'
//  own default.
//
//  Abhinav: "make a output readback reply window in cab io simulator", with
//  the rule "When a bit in loco output is 1 then its corresponding readback
//  will be a=0, b=0, fault=1, trans=1, output=0. When it is 0 readback will
//  be a=1, b=1, fault=1, trans=0, output=1"; the request comes to 17 / 18 and
//  the reply goes from 13 / 14, header included. And "Let user set a default
//  for all the inputs be it just 1 input or combination of no and nc".
//
//  Real frames: tests/fixtures/cabio_frames_20261023.txt (session 325).
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

QByteArray find(const QVector<QByteArray> &frames, quint8 src, quint8 dest, quint8 msg, quint16 seq)
{
    for (const QByteArray &b : frames) {
        Frame f;
        if (parse(b, &f) && f.h.src == src && f.h.dest == dest && f.h.msgId == msg && f.h.seq == seq) return b;
    }
    return QByteArray();
}

void clearDefaults()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    for (const char *k : { "cabio/default_sifa1", "cabio/default_sifa2", "cabio/default_old" })
        s.remove(QLatin1String(k));
}

}  // namespace

TEST_SUITE(session329)
{
    const QVector<QByteArray> frames = realFrames();
    CHECK(frames.size() == 44, "fixture: the 44 real frames");
    if (frames.size() != 44) return;

    // ---- the pin byte and the rule ------------------------------------------------------------
    {
        const PinReadback set = pinFromByte(0x0C), clear = pinFromByte(0x17);
        CHECK(!set.a && !set.b && set.fault && set.trans && !set.output,
              "0x0C reads a=0 b=0 fault=1 trans=1 output=0");
        CHECK(clear.a && clear.b && clear.fault && !clear.trans && clear.output,
              "0x17 reads a=1 b=1 fault=1 trans=0 output=1");
        CHECK(pinByte(set) == 0x0C && pinByte(clear) == 0x17, "and encodes back");
        CHECK(autoPinByte(true) == 0x0C && autoPinByte(false) == 0x17, "output bit 1 -> 0x0C, 0 -> 0x17");

        Frame rb;
        parse(find(frames, 13, kVccId, kMsgOutputToVcc, 0x1B), &rb);
        ReadbackSim sim;
        CHECK(sim.pins(0x03E1) == rb.pins() && sim.manualCount() == 0,
              "all Auto: the 16 pins the real card 13 sent for 0x03E1");
        sim.setManual(13, 0x1F);
        const QByteArray p = sim.pins(0x03E1);
        CHECK(quint8(p.at(13)) == 0x1F && p.left(13) == rb.pins().left(13) && p.mid(14) == rb.pins().mid(14),
              "a Manual pin sends its own byte; the other fifteen are unchanged");
        sim.allAuto();
        CHECK(sim.pins(0x03E1) == rb.pins(), "All Auto restores the rule");
    }

#ifdef DL_HAVE_SERIAL
    // ---- the window answers readback requests --------------------------------------------------
    {
        CabIoSimWindow w(nullptr);
        QVector<QByteArray> sent;
        w.setWriteSink([&](const QByteArray &b) { sent << b; });
        w.setCab(Cab::Sifa);
        w.setWireFormat(WireFormat());
        w.setRespond(CabIoSimWindow::Respond::Answer);
        CHECK(!w.answersReadback(), "Readback starts Off: a real output card may be on the line");
        w.setAnswerReadback(true);
        w.setReadbackCards({ 13 });
        w.setReadbackHealth(kHealthy);

        w.feedBytes(find(frames, kVccId, 13, kMsgVccOutputData, 0x1B));
        w.feedBytes(find(frames, kVccId, 17, kMsgVccOutputReq, 0x1B));
        CHECK(sent.size() == 1 && sent.last() == find(frames, 13, kVccId, kMsgOutputToVcc, 0x1B),
              "request to 17 -> reply from 13, header and all: the real card's bytes");

        w.feedBytes(find(frames, kVccId, 14, kMsgVccOutputData, 0x1C));
        w.feedBytes(find(frames, kVccId, 18, kMsgVccOutputReq, 0x1C));
        CHECK(sent.size() == 1, "card 14 not ticked: its request to 18 is not answered");
        w.setReadbackCards({ 13, 14 });
        w.feedBytes(find(frames, kVccId, 18, kMsgVccOutputReq, 0x1C));
        CHECK(sent.size() == 2 && sent.last() == find(frames, 14, kVccId, kMsgOutputToVcc, 0x1C),
              "ticked: answered as card 14, byte for byte");

        w.readbackSim()->setManual(2, pinByte(PinReadback{ true, true, false, false, true }));   // fault bit clear
        w.setReadbackHealth(0x03);
        w.feedBytes(outputRequest(17, 0x30, WireFormat()));
        Frame f;
        CHECK(sent.size() == 3 && parse(sent.last(), &f) && f.isReadback() && f.h.src == 13 && f.h.seq == 0x30
                  && quint8(f.pins().at(2)) == 0x13 && quint8(f.pins().at(0)) == 0x0C && f.health() == 0x03,
              "a Manual pin and voc_health 0x03 go out in the next readback");
        CHECK(w.readbacksSent() == 3, "readbacks counted");

        w.setAnswerReadback(false);
        w.feedBytes(outputRequest(17, 0x31, WireFormat()));
        CHECK(sent.size() == 3, "Off again: nothing sent");
        w.close();
    }
    {
        // Before any output data, Auto pins have nothing to follow: no reply,
        // unless every pin is Manual.
        CabIoSimWindow w(nullptr);
        QVector<QByteArray> sent;
        w.setWriteSink([&](const QByteArray &b) { sent << b; });
        w.setWireFormat(WireFormat());
        w.setAnswerReadback(true);
        w.setReadbackCards({ 13, 14 });
        w.feedBytes(outputRequest(17, 1, WireFormat()));
        CHECK(sent.isEmpty(), "no output data yet: not answered");
        for (int pin = 0; pin < kPins; ++pin) w.readbackSim()->setManual(pin, 0x17);
        w.feedBytes(outputRequest(17, 2, WireFormat()));
        CHECK(sent.size() == 1, "every pin Manual: answered anyway");
        w.close();
    }

    // ---- the inputs' default -------------------------------------------------------------------
    {
        clearDefaults();
        quint32 want0 = 0, want1 = 0;
        {
            CabIoSimWindow w(nullptr);
            w.setWriteSink([](const QByteArray &) {});
            w.setCab(Cab::Sifa);
            CHECK(!w.hasInputDefault() && w.bank(0)->word() == 0x5555u, "no default: every input Off");
            w.bank(0)->setState(0, 3);                  // dmi1_sos: fault both 1
            w.bank(0)->setState(1, 2);                  // dmi1_cancel: On
            w.bank(0)->setState(8, 1);                  // cab1_active: 1
            w.bank(0)->setBase(w.bank(0)->base() | 0x80000000u);   // padding: never saved
            w.bank(1)->setState(2, 1);                  // mr_cock_fb_no: 1
            w.saveInputDefault();
            want0 = w.bank(0)->base() & 0x7FFFFFFFu;
            want1 = w.bank(1)->base();
            CHECK(w.hasInputDefault(), "saved");

            w.bank(0)->reset();
            w.bank(1)->reset();
            w.bank(0)->setFlicker(4, true);
            w.resetInputsToDefault();
            CHECK(w.bank(0)->word() == want0 && w.bank(1)->word() == want1 && w.bank(0)->flickerCount() == 0,
                  "Reset returns to the saved default, plain inputs and NO/NC alike, padding left 0, no flicker");
            w.close();
        }
        {
            CabIoSimWindow w(nullptr);
            w.setWriteSink([](const QByteArray &) {});
            w.setCab(Cab::Sifa);
            CHECK(w.bank(0)->word() == want0 && w.bank(1)->word() == want1, "a new window opens with the default");
            w.setCab(Cab::Old);
            CHECK(w.bank(0)->word() == 0, "the old cab keeps its own default (none: every input Off)");
            w.setCab(Cab::Sifa);
            w.forgetInputDefault();
            w.resetInputsToDefault();
            CHECK(!w.hasInputDefault() && w.bank(0)->word() == 0x5555u, "forgotten: Reset puts every input Off");
            w.close();
        }
        clearDefaults();
    }
#endif
}
