#include "testutil.h"

#include "cabio.h"

#include "brakebenchwindow.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QUdpSocket>

#include <cmath>

// =============================================================================
//  Session 346 — the Cab I/O Simulator plays the analog card: six 4-20 mA
//  channels as controller 15 (top) / 16 (bottom), message 0xF1 (no start or
//  end pair since session 347), with what the
//  VCC reads from them (Abhinav's Process_Analog_Card_Data_CAN and
//  current_to_pressure). No real analog frame has been captured yet: the
//  frame is checked against the firmware's struct, byte by byte.
//  Session 360: the analog card moved to the Brake Test Bench; its checks
//  follow it (the serial port it never used is no longer here to check).
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
    CHECK(f.size() == 34, qPrintable(QStringLiteral("34 bytes, no start or end pair (%1)").arg(f.size())));
    CHECK(f.left(6) == QByteArray::fromHex("220F01F10700"),
          qPrintable(QStringLiteral("length 34, src 15, dest 1, 0xF1, seq 7 LE (%1)").arg(hex(f.left(6)))));
    CHECK(f.mid(6, 4) == QByteArray::fromHex("01020304") && f.mid(26, 4) == QByteArray::fromHex("A0B0C0D0"),
          "channels big-endian (the VCC PP_HTONLs them)");
    const quint32 crc = quint32(quint8(f.at(30))) | quint32(quint8(f.at(31))) << 8
                        | quint32(quint8(f.at(32))) << 16 | quint32(quint8(f.at(33))) << 24;
    CHECK(crc == crcOf(Crc::DmiJam, f.left(30)),
          "JAMCRC over header and channels, little-endian, last");
    Frame got;
    CHECK(parseAnalog(f, &got) && got.known && got.isAnalog(), "parses as an analog frame");
    CHECK(got.analogRaw(0) == 0x01020304u && got.analogRaw(5) == 0xA0B0C0D0u, "and reads its channels back");
    CHECK(got.crcMatches.contains(Crc::DmiJam), "its CRC is the JAMCRC the other cards use");
    CHECK(unitName(kAnalogBottomId) == QLatin1String("analog bottom 16"), "16 is named");

    CHECK(!parseAnalog(f.left(33), nullptr) && !parseAnalog(inputRequest(5, 1, wf), nullptr),
          "nothing else parses as an analog frame");

    // ---- the window (the Brake Test Bench since session 360) ------------------------------------
    {
        BrakeBenchWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.setAnalogFollows(false);      // the channels as set here, not the bench's air
        // Session 350: analog frames go over UDP.
        QVector<QByteArray> sent;
        QUdpSocket rx;
        CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), 0), "fixture: a UDP receiver on localhost");
        w.setAnalogTarget(QStringLiteral("127.0.0.1"), rx.localPort());
        const auto drain = [&](int want) {
            QElapsedTimer t;
            t.start();
            while (sent.size() < want && t.elapsed() < 2000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                rx.waitForReadyRead(10);
                while (rx.hasPendingDatagrams()) sent << rx.receiveDatagram().data();
            }
        };
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
        drain(2);
        CHECK(sent.size() == 2 && w.analogSent() == 2, "one datagram per ticked controller");
        CHECK(!sent.isEmpty() && sent[0].size() == 34, "each datagram is the bare 34-byte frame");
        Frame a, b;
        if (sent.size() == 2 && parseAnalog(sent[0], &a) && parseAnalog(sent[1], &b)) {
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
        drain(3);
        w.setAnalogSending(false);
        CHECK(sent.size() >= 3, qPrintable(QStringLiteral("sends on its period (%1)").arg(sent.size())));
        Frame c;
        CHECK(!sent.isEmpty() && parseAnalog(sent.first(), &c) && c.h.src == kAnalogBottomId, "as 16 only");
        QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
        while (rx.hasPendingDatagrams()) rx.receiveDatagram();
        const int n = w.analogSent();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
        CHECK(w.analogSent() == n && !w.analogSending(), "and stops when Off");

        w.setAnalogTarget(QString(), 20000);
        w.sendAnalogNow();
        CHECK(w.analogSent() == n, "no host: nothing sent, nothing counted");
    }
}
