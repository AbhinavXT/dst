#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "rfidtag.h"
#include "simpreview.h"
#include "tagbuilderwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QLabel>
#include <QtEndian>

#include <cmath>

// =============================================================================
//  Session 206 — @speed pulses read by the RFID simulator's arithmetic. NO
//  REAL @speed FRAME exists in replay/: the frames here are built in the
//  simulator's layout (reader 1, direction, tachometer 1, tachometer 2, CRC
//  0), 14 bytes as the schema's SPEED packet has them.
// =============================================================================

namespace {

QByteArray speedLine(int seq, quint32 pulses, quint32 crc = 0)
{
    QByteArray b(14, '\0');
    b[0] = 1;
    b[1] = 1;
    qToLittleEndian<quint32>(pulses, reinterpret_cast<uchar *>(b.data()) + 2);
    qToLittleEndian<quint32>(crc, reinterpret_cast<uchar *>(b.data()) + 10);
    return "@speed_1_1 2026-10-11T10:00:" + QByteArray::number(10 + seq / 10).rightJustified(2, '0') + ' '
        + QByteArray::number(36000 + seq) + ' ' + b.toHex(' ').toUpper();
}

}  // namespace

TEST_SUITE(session206)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- 1. the arithmetic, both ways ---------------------------------------------------------
    bool back = true;
    for (double v : { 5.0, 30.0, 60.0, 110.0, 160.0 })
        back = back && std::fabs(SimPreview::kmhOfPulses(SimPreview::pulsesPerTick(v)) - v) < 0.01;
    CHECK(back, "pulses for a speed read back as that speed (within 0.01 km/h: the count is truncated)");
    CHECK(std::fabs(SimPreview::kmhOfPulses(16746) - 60) < 0.01 && SimPreview::kmhOfPulses(0) == 0,
          QByteArray("16746 pulses: 60 km/h; ") + QByteArray::number(SimPreview::kmhOfPulses(16746), 'f', 3));

    // ---- 2. a log of the simulator's speed frames ---------------------------------------------
    CHECK(CaptureDecoder::parseLine(QString::fromLatin1(speedLine(0, 16746))).bytes.size() == 14, "a built @speed line parses: 14 bytes");
    MessageDispatcher d;
    const qint64 t0 = QDateTime::fromString(QStringLiteral("2026-10-11T10:00:10"), Qt::ISODate).toMSecsSinceEpoch();
    int seq = 0;
    for (int i = 0; i < 5; ++i, ++seq) d.ingestLocal(1, 1, speedLine(seq, 0), t0 + seq * 100, QString());
    for (int i = 0; i < 20; ++i, ++seq) d.ingestLocal(1, 1, speedLine(seq, 16746), t0 + seq * 100, QString());
    for (int i = 0; i < 4; ++i, ++seq) d.ingestLocal(1, 1, speedLine(seq, SimPreview::pulsesPerTick(80)), t0 + seq * 100, QString());
    d.ingestLocal(1, 1, speedLine(seq, 16746, 0x12345678), t0 + seq * 100, QString());
    d.drainNow();
    const LogModel *m = d.modelForKey(QStringLiteral("1_1"));
    const SimPreview::SpeedSeen s = SimPreview::speedsOf(m);
    CHECK(s.frames == 30 && s.moving == 25 && s.crcZero == 29,
          QByteArray("30 frames, 25 moving, 29 with CRC 0: ") + QByteArray::number(s.frames) + " / " + QByteArray::number(s.moving)
              + " / " + QByteArray::number(s.crcZero));
    CHECK(std::fabs(s.medianKmh - 60) < 0.01 && std::fabs(s.minKmh - 60) < 0.01 && std::fabs(s.maxKmh - 80) < 0.01,
          "tachometer 1: median 60 km/h, from 60 to 80");
    CHECK(SimPreview::speedsOf(nullptr).frames == 0, "no log: nothing");

    // ---- 3. the window ------------------------------------------------------------------------
    {
        TagBuilderWindow w(&d);
        w.resize(1100, 680);
        RfidTag::Route r;
        r.dir = RfidTag::DirNominal;
        for (int i = 0; i < 2; ++i)
            r.tags.append({ QString(), RfidTag::build({ { QStringLiteral("type"), 9 }, { QStringLiteral("unique"), 100 + i },
                                                        { QStringLiteral("abs_loc"), 1000 + 300 * i } }) });
        w.setRoute(r);
        w.setRunSource(QStringLiteral("1_1"));
        w.previewAgainstRun();
        const QString t = w.simSummary()->text();
        CHECK(t.contains(QStringLiteral("Its @speed frames: 30, 25 moving; tachometer 1 at 60.0 km/h (median; 60.0 to 80.0)"))
                  && t.contains(QStringLiteral("against the previewed 60 km/h; 29 with CRC 0")),
              QByteArray("Against the run: the log's speed beside the previewed one: ") + t.toUtf8());
        w.setRoute(RfidTag::Route());
    }
}
