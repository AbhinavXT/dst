#include "testutil.h"

#include "cabio.h"

#ifdef DL_HAVE_SERIAL
#include "cabiosimwindow.h"
#endif

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cmath>

// =============================================================================
//  Session 346 — the Cab I/O Simulator plays the analog card: six 4-20 mA
//  channels as controller 15 (top) / 16 (bottom), message 0xF1, with what the
//  VCC reads from them (Abhinav's Process_Analog_Card_Data_CAN and
//  current_to_pressure). No real analog frame has been captured yet: the
//  frame is checked against the firmware's struct, byte by byte.
// =============================================================================

using namespace CabIo;

namespace {
bool near346(double a, double b, double tol = 1e-3) { return std::fabs(a - b) <= tol; }
}

TEST_SUITE(session346)
{
    // ---- the VCC's reading ------------------------------------------------------------------
    CHECK(near346(analogMa(analogRawFor(12.0)), 12.0, 1e-6), "12 mA survives raw and back");
    CHECK(analogRawFor(12.0) == 343597384u, "12 mA is raw 12/75 * 2^31 (2500/33 is 75 in C)");
    CHECK(analogMa(0x40000000u) == 37.5, "raw 2^30 reads as 37.5 mA");
    CHECK(analogRawFor(0) == 0, "0 mA is raw 0");

    CHECK(currentToPressure(12, 10).status == PressureReading::Ok
              && near346(currentToPressure(12, 10).kgcm2, 5.1), "12 mA on 10 bar: 5.10 kg/cm2");
    CHECK(near346(currentToPressure(20, 16).kgcm2, 16.32), "20 mA on 16 bar: 16.32 kg/cm2");
    CHECK(near346(currentToPressure(20.4, 10).kgcm2, 10.2), "above 20 mA is clamped to full scale");
    CHECK(currentToPressure(3.6, 10).status == PressureReading::Ok
              && currentToPressure(3.6, 10).kgcm2 == 0, "3.6 mA: still read, clamped to 0");
    CHECK(currentToPressure(3.4, 10).status == PressureReading::Open, "below 3.5 mA: open loop");
    CHECK(currentToPressure(20.6, 10).status == PressureReading::Short, "above 20.5 mA: short");
    CHECK(currentToPressure(analogMa(0x40000000u), 10).status == PressureReading::Short, "37.5 mA: short");
    CHECK(analogChannelName(0) == QLatin1String("BP") && analogChannelName(2) == QLatin1String("MR")
              && analogChannelName(5) == QLatin1String("Channel 6"), "channels named as the firmware uses them");

    // ---- the frame --------------------------------------------------------------------------
    const WireFormat wf;
    const QVector<quint32> raw{ 0x01020304u, 0, 0, 0, 0, 0xA0B0C0D0u };
    const QByteArray f = analogData(kAnalogTopId, 7, raw, wf);
    CHECK(f.size() == 38, qPrintable(QStringLiteral("38 bytes (%1)").arg(f.size())));
    CHECK(f.left(8) == QByteArray::fromHex("AFAF260F01F10700"),
          qPrintable(QStringLiteral("AF AF, length 38, src 15, dest 1, 0xF1, seq 7 LE (%1)").arg(hex(f.left(8)))));
    CHECK(f.mid(8, 4) == QByteArray::fromHex("01020304") && f.mid(28, 4) == QByteArray::fromHex("A0B0C0D0"),
          "channels big-endian (the VCC PP_HTONLs them)");
    CHECK(f.right(2) == QByteArray::fromHex("FAFA"), "ends FA FA");
    Frame got;
    CHECK(parse(f, &got) && got.known && got.isAnalog(), "parses as a known analog frame");
    CHECK(got.analogRaw(0) == 0x01020304u && got.analogRaw(5) == 0xA0B0C0D0u, "and reads its channels back");
    CHECK(got.crcMatches.contains(Crc::DmiJam), "its CRC is the JAMCRC the other cards use");
    CHECK(unitName(kAnalogBottomId) == QLatin1String("analog bottom 16"), "16 is named");

    // A payload holding FA FA is not cut short: the frame's size is known.
    const QByteArray fa = analogData(kAnalogBottomId, 1, QVector<quint32>(6, 0xFAFAFAFAu), wf);
    Scanner s;
    QVector<Frame> frames;
    for (char c : fa + fa) frames += s.feed(QByteArray(1, c));
    CHECK(frames.size() == 2 && frames[0].isAnalog() && frames[1].analogRaw(3) == 0xFAFAFAFAu
              && s.skippedBytes() == 0, "FA FA inside the channels: two whole frames, nothing skipped");

#ifdef DL_HAVE_SERIAL
    // ---- the window --------------------------------------------------------------------------
    {
        CabIoSimWindow w(nullptr);
        QVector<QByteArray> sent;
        w.setWriteSink([&](const QByteArray &b) { sent << b; });
        w.setWireFormat(WireFormat());
        CHECK(!w.analogSending(), "opens Off");
        for (int ch = 0; ch < kAnalogChannels; ++ch) { w.setAnalogScale(ch, 10); w.setAnalogMa(ch, 4); }
        w.setAnalogPressure(0, 5.0);
        CHECK(near346(w.analogMa(0), 4 + 16 * 5.0 / 10.2), "BP 5.0 kg/cm2 sets 11.843 mA");
        w.setAnalogScale(2, 16);
        w.setAnalogPressure(2, 9.0);
        w.setAnalogMa(1, 2.0);                           // an open loop on BC
        w.setAnalogCards({ kAnalogTopId, kAnalogBottomId });
        w.sendAnalogNow();
        CHECK(sent.size() == 2 && w.analogSent() == 2, "one frame per ticked controller");
        Frame a, b;
        if (sent.size() == 2 && parse(sent[0], &a) && parse(sent[1], &b)) {
            CHECK(a.h.src == 15 && b.h.src == 16 && a.h.dest == kVccId && b.h.seq == quint16(a.h.seq + 1),
                  "as 15 then 16, to the VCC, seq counting up");
            CHECK(near346(currentToPressure(analogMa(a.analogRaw(0)), 10).kgcm2, 5.0, 0.01),
                  "the VCC reads BP 5.0");
            CHECK(near346(currentToPressure(analogMa(a.analogRaw(2)), 16).kgcm2, 9.0, 0.01),
                  "and MR 9.0 on its 16 bar sensor");
            CHECK(currentToPressure(analogMa(a.analogRaw(1)), 10).status == PressureReading::Open,
                  "and BC as an open loop");
            CHECK(a.analogRaw(3) == analogRawFor(4), "the rest 4 mA");
        } else {
            CHECK(false, "both frames parse");
        }

        sent.clear();
        w.setAnalogCards({ kAnalogBottomId });
        w.setAnalogPeriodMs(20);
        w.setAnalogSending(true);
        QElapsedTimer t;
        t.start();
        while (sent.size() < 3 && t.elapsed() < 2000) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        w.setAnalogSending(false);
        CHECK(sent.size() >= 3, qPrintable(QStringLiteral("sends on its period (%1 in %2 ms)").arg(sent.size()).arg(t.elapsed())));
        Frame c;
        CHECK(!sent.isEmpty() && parse(sent.first(), &c) && c.h.src == kAnalogBottomId, "as 16 only");
        const int n = sent.size();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
        CHECK(sent.size() == n && !w.analogSending(), "and stops when Off");
    }
#endif
}
