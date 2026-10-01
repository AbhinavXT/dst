#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "twolocoview.h"
#include "twolocowindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// =============================================================================
//  Session 98: the two-loco view. Two tabs, one timeline -- location and
//  speed for both, the gap between them, and SoS/collision/head-on/rear-end
//  events, all read from already-decoded frames (@dmi/@lsrp via
//  SpeedDistance::extract, @lsos for the events -- @lsos itself gained a
//  CapType this session; it previously decoded to nothing but header rows).
// =============================================================================

namespace {

const QString kDmi = QStringLiteral(
    "@dmi_1_1 2026-06-26T16:24:19 997 AA AA 74 02 01 0A 6F 00 C9 08 00 00 00 00 00 00 00 00 00 00 1A 06 EA 07 10 18 13 "
    "00 00 00 00 00 00 00 00 40 00 00 00 00 00 00 01 00 00 20 00 00 00 00 00 00 00 00 00 00 00 50 00 00 00 00 00 00 00 "
    "00 00 00 00 00 FA 00 00 00 00 00 01 00 00 0D 6A 0D 00 00 00 00 00 00 00 00 00 00 00 00 F0 3C 00 F4 01 00 00 00 00 "
    "08 06 0F 00 00 01 00 D3 89 1D 43 BB BB");
// 31 zero bytes: LOCO_SOS is exactly 248 bits / 31 bytes, trailer_bytes="0".
const QString kLsos = QStringLiteral(
    "@lsos_1_1 2026-06-27T14:02:27 1 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00");

QString withFields(const QString &line, const QHash<QString, qint64> &values)
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
            const int bit = int((v >> i) & 1);   // lsb-first, both these packets
            const int mask = 1 << off;
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

// An @lsos line for a given loco key, fields otherwise zero until withFields()
// sets the ones a test cares about.
QString lsosBase(const QString &key)
{
    QString line = kLsos;
    return line.replace(QStringLiteral("1_1"), key);
}

}  // namespace

TEST_SUITE(session98)
{
    const qint64 t0 = 1782480000000LL;

    // ---- extractEvents(): the three episode kinds, from @lsos -------------------------
    {
        LogModel m(nullptr, 1000);
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 20; ++i) {
            const qint64 ms = t0 + i * 1000;
            const bool access = (i == 5);
            const bool headOn = (i >= 10 && i <= 12);
            const bool rearEnd = (i >= 15 && i <= 16);
            v << entry(withFields(lsosBase(QStringLiteral("1_1")),
                { { "is_access_sos_recvd", access ? 1 : 0 },
                  { "is_head_on_collision_recvd", headOn ? 1 : 0 },
                  { "is_rear_end_collision_recvd", rearEnd ? 1 : 0 },
                  { "sos_distance", access ? 300 : 0 },
                  { "collision_distance", (headOn || rearEnd) ? 150 : 0 } }), ms);
        }
        m.appendEntries(v);
        const TwoLocoView::Events e = TwoLocoView::extractEvents(&m);
        CHECK(e.sos.size() == 1, "one access-SOS episode");
        if (e.sos.size() == 1) {
            CHECK(e.sos.first().fromMs == t0 + 5000 && e.sos.first().worst == 300.0, "at i=5, with its distance");
            CHECK(e.sos.first().what.contains(QLatin1String("access SOS")), "named for what it was");
        }
        CHECK(e.headOn.size() == 1, "one head-on episode");
        if (e.headOn.size() == 1) {
            CHECK(e.headOn.first().fromMs == t0 + 10000 && e.headOn.first().toMs == t0 + 12000, "spanning i=10..12");
            CHECK(e.headOn.first().worst == 150.0, "collision_distance carried as the episode's worst");
        }
        CHECK(e.rearEnd.size() == 1, "one rear-end episode");
        if (e.rearEnd.size() == 1) {
            CHECK(e.rearEnd.first().fromMs == t0 + 15000 && e.rearEnd.first().toMs == t0 + 16000, "spanning i=15..16");
        }

        // Windowed: only the head-on episode falls inside [9500, 13000].
        const TwoLocoView::Events windowed = TwoLocoView::extractEvents(&m, t0 + 9500, t0 + 13000);
        CHECK(windowed.sos.isEmpty() && windowed.headOn.size() == 1 && windowed.rearEnd.isEmpty(),
              "extractEvents honours its window");
    }

    // ---- TwoLocoView::build(): two overlapping traces, matched by time -----------------
    LogModel runA(nullptr, 2000), runB(nullptr, 2000);
    {
        QVector<LogEntryPtr> va, vb;
        for (int i = 0; i < 30; ++i) {
            const qint64 ms = t0 + i * 1000;
            va << entry(withFields(kDmi, { { "abs_loco_loc", 100000 + i * 50 }, { "train_speed", 60 },
                                           { "speed_limit_permissible", 80 } }), ms);
            // B closes from 100500 down to 99050 -- overlaps A's [100000,101450].
            vb << entry(withFields(kDmi, { { "abs_loco_loc", 100500 - i * 50 }, { "train_speed", 50 },
                                           { "speed_limit_permissible", 80 } }), ms);
        }
        runA.appendEntries(va);
        runB.appendEntries(vb);
    }
    {
        const TwoLocoView::Pair p = TwoLocoView::build(&runA, QStringLiteral("1_1"), &runB, QStringLiteral("2_1"));
        CHECK(p.a.samples.size() == 30 && p.b.samples.size() == 30, "both traces extracted");
        CHECK(p.plausible, "the two locos' location ranges overlap");
        CHECK(p.gap.size() == 30, "a gap sample for every matched instant (same timestamps here)");
        if (!p.gap.isEmpty()) CHECK(p.gap.first().gapM == 100500.0 - 100000.0, "gap = B - A, at the first instant");
        if (p.gap.size() > 10) {
            CHECK(qFuzzyCompare(p.gap.at(10).gapM, (100500.0 - 10 * 50) - (100000.0 + 10 * 50)), "and at the tenth");
        }

        const QString csv = TwoLocoView::gapToCsv(p);
        CHECK(csv.startsWith(QLatin1String("time_local,epoch_ms,gap_m")) && csv.trimmed().split(QLatin1Char('\n')).size() == 31,
              "CSV: header plus one row per gap sample");
    }

    // ---- A gap tolerance that excludes a stale match ------------------------------------
    {
        LogModel sparse(nullptr, 100);
        sparse.appendEntries({ entry(withFields(kDmi, { { "abs_loco_loc", 100000 }, { "train_speed", 40 } }), t0) });
        const TwoLocoView::Pair p = TwoLocoView::build(&runA, QStringLiteral("1_1"), &sparse, QStringLiteral("sparse"),
                                                        0, 0, 2000);
        CHECK(p.gap.size() == 3, "only A's samples within 2 s of sparse's single frame at t0 are paired");
    }

    // ---- Non-overlapping locations: flagged, not silently plotted as fact ----------------
    {
        LogModel far(nullptr, 1000);
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 10; ++i)
            v << entry(withFields(kDmi, { { "abs_loco_loc", 900000 + i * 10 }, { "train_speed", 30 } }), t0 + i * 1000);
        far.appendEntries(v);
        const TwoLocoView::Pair p = TwoLocoView::build(&runA, QStringLiteral("1_1"), &far, QStringLiteral("far_1"));
        CHECK(!p.plausible && p.warning.contains(QLatin1String("1_1")) && p.warning.contains(QLatin1String("far_1")),
              "ranges nowhere near each other: flagged, naming both tabs");
    }

    // ---- The canvas: paints, tracks the cursor on mouse move -----------------------------
    {
        const TwoLocoView::Pair p = TwoLocoView::build(&runA, QStringLiteral("1_1"), &runB, QStringLiteral("2_1"));
        TwoLocoCanvas canvas;
        canvas.resize(960, 560);
        canvas.setPair(p);
        canvas.show();
        QTest::qWait(20);
        CHECK(canvas.pair().a.samples.size() == 30, "the canvas holds what it was given");
        // Sent straight to the widget (not QTest::mouseMove, which warps the
        // GLOBAL cursor and relies on window stacking under the offscreen
        // platform to route it -- flaky with other test windows still alive
        // in the same process; a direct QMouseEvent exercises the same
        // mouseMoveEvent() without depending on compositing at all).
        QSignalSpy spy(&canvas, &TwoLocoCanvas::cursorChanged);
        const QPoint pos(canvas.width() / 2, canvas.height() / 3);
        QMouseEvent move(QEvent::MouseMove, pos, canvas.mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &move);
        CHECK(spy.count() >= 1 && canvas.cursorMs() >= 0, "hovering over the plot reports a cursor instant");
        const QByteArray dirEnv = qgetenv("DL_SHOTS");
        if (!dirEnv.isEmpty()) canvas.grab().save(QDir(QString::fromLocal8Bit(dirEnv)).filePath(QStringLiteral("two_loco.png")));
    }

    // ---- The window: picks sources via the dispatcher, builds, saves -------------------
    {
        MessageDispatcher dispatcher;
        LogModel *ma = dispatcher.modelForKey(QStringLiteral("1_1"));
        LogModel *mb = dispatcher.modelForKey(QStringLiteral("2_1"));
        QVector<LogEntryPtr> va, vb;
        for (int i = 0; i < 10; ++i) {
            const qint64 ms = t0 + i * 1000;
            va << entry(withFields(kDmi, { { "abs_loco_loc", 100000 + i * 50 }, { "train_speed", 60 } }), ms);
            vb << entry(withFields(kDmi, { { "abs_loco_loc", 100300 - i * 50 }, { "train_speed", 55 } }), ms);
        }
        ma->appendEntries(va);
        mb->appendEntries(vb);

        TwoLocoWindow w(&dispatcher);
        w.setSources(QStringLiteral("1_1"), QStringLiteral("2_1"));
        CHECK(w.pair().a.samples.size() == 10 && w.pair().b.samples.size() == 10, "builds from the dispatcher's models");
        CHECK(w.pair().plausible, "overlapping ranges here too");

        QTemporaryDir dir;
        const QString png = dir.filePath(QStringLiteral("tl.png")), csv = dir.filePath(QStringLiteral("tl.csv"));
        w.resize(960, 700);
        w.show();
        QTest::qWait(20);
        CHECK(w.saveImage(png) && QFile(png).size() > 500, "saves an image");
        CHECK(w.saveCsv(csv) && QFile(csv).size() > 0, "and the gap CSV");
    }
}
