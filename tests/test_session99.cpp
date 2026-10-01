#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "trackdiagram.h"
#include "trackdiagramwindow.h"

#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

// =============================================================================
//  Session 99: the track diagram. One tab, one line against absolute
//  location -- RFID tags, signals and the movement authority end (both
//  derived: neither has an absolute-location field anywhere), events pinned
//  by the loco's last known location, and the loco riding a draggable time
//  cursor. Real frames from replay/ wherever one exists (CLAUDE.md's rule),
//  synthetic only for the parts a single real log does not happen to cover.
// =============================================================================

namespace {

const QString kDmi = QStringLiteral(
    "@dmi_1_1 2026-06-26T16:24:19 997 AA AA 74 02 01 0A 6F 00 C9 08 00 00 00 00 00 00 00 00 00 00 1A 06 EA 07 10 18 13 "
    "00 00 00 00 00 00 00 00 40 00 00 00 00 00 00 01 00 00 20 00 00 00 00 00 00 00 00 00 00 00 50 00 00 00 00 00 00 00 "
    "00 00 00 00 00 FA 00 00 00 00 00 01 00 00 0D 6A 0D 00 00 00 00 00 00 00 00 00 00 00 00 F0 3C 00 F4 01 00 00 00 00 "
    "08 06 0F 00 00 01 00 D3 89 1D 43 BB BB");
const QString kLsrp = QStringLiteral(
    "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
    "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B");
// Real capture (replay/loco_1_1_26062026_162418.cap): a normal tag (type 9),
// unique 560, absolute location 152720 m, CRC-30 verified.
const QString kRfid = QStringLiteral(
    "@rfid_1_1 2026-06-26T16:25:41 1869 01 19 8C 90 54 02 26 A6 FA 80 FA 00 00 AC 5C 63 33");
// Real capture (replay/loco_1_1_29062026_134128.cap): a named signal ("DN
// MAIN Dist"), 1250 m ahead, MA 3251 m past it, loco at 153749 m.
const QString kDmiSignal = QStringLiteral(
    "@dmi_1_1 2026-06-29T13:41:29 2942 AA AA 74 02 01 0A 6F 00 10 17 00 00 00 00 00 00 00 00 00 00 1D 06 EA 07 0D 29 1D "
    "00 00 00 00 02 00 00 00 00 00 56 66 63 00 00 01 00 00 60 95 58 02 00 60 21 64 19 00 00 00 0F 00 80 59 06 E2 04 4B "
    "01 F5 01 00 00 FA 00 00 00 00 00 91 41 14 2D 6A AD 00 00 00 00 00 00 00 00 00 00 00 00 F0 3C 00 F4 01 00 00 00 00 "
    "08 06 0F 00 00 01 00 27 8B EE 3C BB BB");

QString withFields(const QString &line, const QHash<QString, qint64> &values, bool msbFirst = false)
{
    CaptureLine cap = CaptureDecoder::parseLine(line);
    QByteArray bytes = cap.bytes;
    for (const FieldRow &r : CaptureDecoder::describe(cap)) {
        const QString f = r.field.trimmed();
        if (!values.contains(f) || !r.hasSpan()) continue;
        const qint64 v = values.value(f);
        for (int i = 0; i < r.bitLength; ++i) {
            const int pos = r.bitOffset + i;
            const int byte = pos >> 3, off = pos & 7;
            const int bit = msbFirst ? int((v >> (r.bitLength - 1 - i)) & 1) : int((v >> i) & 1);
            const int mask = msbFirst ? (0x80 >> off) : (1 << off);
            bytes[byte] = char(bit ? (uchar(bytes.at(byte)) | mask) : (uchar(bytes.at(byte)) & ~mask));
        }
    }
    const QStringList tok = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return QStringList{ tok.at(0), tok.at(1), tok.at(2) }.join(QLatin1Char(' ')) + QLatin1Char(' ')
           + QString::fromLatin1(bytes.toHex(' ')).toUpper();
}

LogEntryPtr entry(const QString &line, qint64 ms)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->text = line;
    e->epochMs = ms;
    e->cacheDerived();
    return e;
}

}  // namespace

TEST_SUITE(session99)
{
    const qint64 t0 = 1782480000000LL;
    LogModel run(nullptr, 2000);
    QVector<LogEntryPtr> v;

    // i=0..9: climbing from 150000 m, mode 2, permitted 80.
    for (int i = 0; i < 10; ++i) {
        const qint64 ms = t0 + i * 1000;
        v << entry(withFields(kDmi, { { "abs_loco_loc", 150000 + i * 100 }, { "train_speed", 60 },
                                      { "speed_limit_permissible", 80 } }), ms);
        v << entry(withFields(kLsrp, { { "LOCO_MODE", 2 } }, true), ms + 100);
    }
    // t0+2500: the real RFID tag.
    v << entry(kRfid, t0 + 2500);
    // t0+12000: the same signal, farther off (1800 m) and a different MA --
    // a worse (less accurate) reading that must NOT win the dedup.
    v << entry(withFields(kDmiSignal, { { "appr_sig_dist", 1800 }, { "ma_w_r_t_sig", 9999 } }), t0 + 12000);
    // t0+15000: the real, closer (1250 m) reading -- this one should win.
    v << entry(kDmiSignal, t0 + 15000);
    // i=10..19: continue past it, mode changes to 3 at i=15, overspeed at i=16..17.
    for (int i = 10; i < 20; ++i) {
        const qint64 ms = t0 + 16000 + (i - 10) * 1000;
        const qint64 speed = (i == 16 || i == 17) ? 95 : 60;
        v << entry(withFields(kDmi, { { "abs_loco_loc", 150000 + i * 100 }, { "train_speed", speed },
                                      { "speed_limit_permissible", 80 } }), ms);
        v << entry(withFields(kLsrp, { { "LOCO_MODE", i < 15 ? 2 : 3 } }, true), ms + 100);
    }
    run.appendEntries(v);

    // =================================================================================
    //  TrackDiagram::build()
    // =================================================================================
    const TrackDiagram::Diagram d = TrackDiagram::build(&run, QStringLiteral("1_1"), QStringLiteral("L1_V1"));
    CHECK(!d.isEmpty() && d.trace.direction == 1, "the trace extracts as before; climbing locations, direction +1");

    CHECK(d.tags.size() == 1, "one RFID tag");
    if (!d.tags.isEmpty()) {
        CHECK(d.tags.first().uniqueId == 560 && d.tags.first().locM == 152720.0,
              "the real tag's id and absolute location, straight off CRC-verified @rfid bytes");
    }

    CHECK(d.signalMarks.size() == 1, "the two readings of the same signal dedup to one");
    if (!d.signalMarks.isEmpty()) {
        const TrackDiagram::SignalMark &s = d.signalMarks.first();
        CHECK(s.name == QLatin1String("DN MAIN Dist"), "the real decoded signal name");
        CHECK(qFuzzyCompare(s.locM, 153749.0 + 1250.0), "placed at the CLOSER (1250 m) reading, not the 1800 m one");
        CHECK(s.hasMa && qFuzzyCompare(s.maEndLocM, 153749.0 + 1250.0 + 3251.0),
              "its MA end comes from the SAME (closer) frame -- 3251 m, not the other reading's 9999");
    }

    CHECK(d.events.size() >= 2, "at least the mode change and the overspeed episode");
    bool haveMode = false, haveOverspeed = false;
    for (const TrackDiagram::EventMark &e : d.events) {
        if (e.kind == QLatin1String("mode")) { haveMode = true; CHECK(e.locM >= 150000 && e.locM <= 153000, "the mode change pinned somewhere on the climb"); }
        if (e.kind == QLatin1String("overspeed")) { haveOverspeed = true; }
    }
    CHECK(haveMode && haveOverspeed, "both kinds found");

    CHECK(d.minLocM <= 150000.0 && d.maxLocM >= 158250.0,
          "the span widens to cover the tag, the signal and its MA end, not just the trace");

    // =================================================================================
    //  The canvas
    // =================================================================================
    {
        TrackDiagramCanvas canvas;
        canvas.resize(1000, 360);
        canvas.setDiagram(d);
        canvas.show();
        QTest::qWait(20);
        CHECK(canvas.cursorIndex() == d.trace.samples.size() - 1, "setDiagram() starts the cursor at the last sample");
        QSignalSpy spy(&canvas, &TrackDiagramCanvas::cursorIndexChanged);
        canvas.setCursorIndex(5);
        CHECK(canvas.cursorIndex() == 5 && spy.count() == 1, "the cursor moves and says so");
        canvas.setCursorIndex(-100);
        CHECK(canvas.cursorIndex() == -1, "clamped rather than going out of range");
        const QByteArray dirEnv = qgetenv("DL_SHOTS");
        if (!dirEnv.isEmpty()) canvas.grab().save(QDir(QString::fromLocal8Bit(dirEnv)).filePath(QStringLiteral("track_diagram.png")));
    }

    // =================================================================================
    //  The window
    // =================================================================================
    {
        TrackDiagramWindow w(&run, QStringLiteral("1_1"), QStringLiteral("L1_V1"));
        CHECK(w.diagram().tags.size() == 1 && w.diagram().signalMarks.size() == 1, "builds on construction");
        QSlider *slider = w.findChild<QSlider *>();
        CHECK(slider && slider->maximum() == d.trace.samples.size() - 1, "the slider spans every sample");

        QPushButton *play = nullptr;
        for (QPushButton *b : w.findChildren<QPushButton *>()) if (b->text() == QLatin1String("Play")) play = b;
        QTimer *timer = w.findChild<QTimer *>();
        CHECK(play && timer && !timer->isActive(), "not playing at rest");
        if (play) play->setChecked(true);
        CHECK(timer && timer->isActive(), "Play starts the timer");
        if (play) play->setChecked(false);
        CHECK(timer && !timer->isActive(), "and un-checking it stops the timer");

        QTemporaryDir dir;
        const QString png = dir.filePath(QStringLiteral("td.png"));
        w.resize(1040, 520);
        w.show();
        QTest::qWait(20);
        CHECK(w.saveImage(png) && QFile(png).size() > 500, "saves an image");
    }

    // =================================================================================
    //  No location in the tab at all
    // =================================================================================
    {
        LogModel empty(nullptr, 10);
        const TrackDiagram::Diagram e = TrackDiagram::build(&empty, QStringLiteral("x"), QString());
        CHECK(e.isEmpty() && e.tags.isEmpty() && e.signalMarks.isEmpty() && e.events.isEmpty(),
              "an empty tab gives an empty diagram, not a crash");
    }
}
