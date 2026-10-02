#include "testutil.h"
#include "layoutaudit.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "settings.h"
#include "statusline.h"
#include "twolocoview.h"
#include "twolocowindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QMouseEvent>
#include <QSignalSpy>

// =============================================================================
//  Session 132 — UI revamp, tool windows 8: the Two-loco view.
//
//  0 m is "location not known" here too (Abhinav, 2026-10-02): no gap unless
//  both locations are known, the location scale from known samples, the
//  location line broken rather than diving to 0. The same-section warning
//  takes a distance, configurable in the window (default 10 km; 0 = the old
//  any-non-overlap rule). The canvas: lane titles, km and km/h on round
//  ticks, a time axis, a header readout of both locos at the cursor.
//  Real captures: replay/loco_1_1_27062026_151052.cap and loco_2_1_ of the
//  same run, recorded together.
// =============================================================================

namespace {

void ingestCapture(MessageDispatcher *disp, quint8 src, const QString &name, int maxLines = 1 << 30)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/") + name);
    if (!f.open(QIODevice::ReadOnly)) return;
    int n = 0;
    while (!f.atEnd() && n < maxLines) {
        const QByteArray l = f.readLine().trimmed();
        if (!l.startsWith('@')) continue;
        const qint64 ms = QDateTime::fromString(QString::fromLatin1(l.split(' ').value(1)), Qt::ISODate)
                              .toMSecsSinceEpoch();
        disp->ingestLocal(src, 1, l, ms, QString());
        ++n;
    }
}

}  // namespace

TEST_SUITE(session132)
{
    const double savedWarn = Settings::twoLocoWarnApartKm();
    Settings::setTwoLocoWarnApartKm(10.0);

    MessageDispatcher disp;
    ingestCapture(&disp, 21, QStringLiteral("loco_1_1_27062026_151052.cap"));
    ingestCapture(&disp, 22, QStringLiteral("loco_2_1_27062026_151052.cap"));
    disp.drainNow();
    LogModel *ma = disp.modelForKey(QStringLiteral("21_1"));
    LogModel *mb = disp.modelForKey(QStringLiteral("22_1"));
    CHECK(ma && mb && ma->count() > 0 && mb->count() > 0, "fixture: two real locos, recorded together");
    if (!ma || !mb) { Settings::setTwoLocoWarnApartKm(savedWarn); return; }

    // ---- 0 m: not known -----------------------------------------------------------------
    const TwoLocoView::Pair p = TwoLocoView::build(ma, QStringLiteral("21_1"), mb, QStringLiteral("22_1"),
                                                    0, 0, 2000, 10000.0);
    CHECK(p.unknownA == 132 && p.unknownB == 45,
          QByteArray("the 0 m frames are counted (A ") + QByteArray::number(p.unknownA) + ", B "
              + QByteArray::number(p.unknownB) + ")");
    bool known = true, small = true;
    for (const TwoLocoView::GapSample &g : p.gap) {
        known = known && g.aLocM > 0.0 && g.bLocM > 0.0;
        small = small && qAbs(g.gapM) < 2000.0;
    }
    CHECK(p.gap.size() == 25, QByteArray("a gap only where both locations are known (")
                                  + QByteArray::number(p.gap.size()) + " of 157 instants)");
    CHECK(known, "every gap sample is between two known locations");
    CHECK(small, "no 161 km \"gap\" between a localised loco and one at 0 m");
    CHECK(p.minLocM > 160000.0 && p.maxLocM < 162000.0,
          QByteArray("the location scale is the run's (") + QByteArray::number(p.minLocM, 'f', 0) + ".."
              + QByteArray::number(p.maxLocM, 'f', 0) + " m), not from 0");
    const QString csv = TwoLocoView::gapToCsv(p);
    CHECK(csv.trimmed().split(QLatin1Char('\n')).size() == 26, "the CSV has the 25 real gaps and nothing else");

    // ---- the same-section warning takes a distance ----------------------------------------
    CHECK(p.apartM > 0.0 && p.apartM < 1000.0,
          QByteArray("following each other, the ranges are a few hundred metres apart (")
              + QByteArray::number(p.apartM, 'f', 0) + " m)");
    CHECK(p.plausible, "within 10 km: no warning on the case the view is for");
    const TwoLocoView::Pair strict = TwoLocoView::build(ma, QStringLiteral("21_1"), mb, QStringLiteral("22_1"));
    CHECK(!strict.plausible && strict.warning.contains(QLatin1String("do not overlap")),
          "0 (the default for direct callers) keeps the old any-non-overlap rule");
    const TwoLocoView::Pair tight = TwoLocoView::build(ma, QStringLiteral("21_1"), mb, QStringLiteral("22_1"),
                                                        0, 0, 2000, 100.0);
    CHECK(!tight.plausible && tight.warning.contains(QLatin1String("km apart")),
          "a limit below the distance warns, saying how far apart");

    // ---- a loco that never localises -------------------------------------------------------------
    {
        MessageDispatcher early;
        ingestCapture(&early, 21, QStringLiteral("loco_1_1_27062026_151052.cap"), 200);
        ingestCapture(&early, 22, QStringLiteral("loco_2_1_27062026_151052.cap"));
        early.drainNow();
        const TwoLocoView::Pair e = TwoLocoView::build(early.modelForKey(QStringLiteral("21_1")), QStringLiteral("21_1"),
                                                        early.modelForKey(QStringLiteral("22_1")), QStringLiteral("22_1"),
                                                        0, 0, 2000, 10000.0);
        CHECK(!e.a.isEmpty() && !e.hasLocA && e.hasLocB, "fixture: A's first 200 lines, all at 0 m");
        CHECK(!e.plausible && e.warning.contains(QLatin1String("21_1 reports 0 m throughout")) && e.gap.isEmpty(),
              "said by name, and no gap");
    }

    // ---- the canvas -------------------------------------------------------------------------------
    {
        TwoLocoCanvas canvas;
        canvas.resize(1000, 560);
        canvas.setPair(p);
        canvas.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

        const qint64 both = p.gap.isEmpty() ? 0 : p.gap.last().ms;
        const QString r1 = canvas.readoutAt(both);
        CHECK(r1.count(QLatin1String(" km,")) == 2 && r1.contains(QLatin1String("gap ")),
              QByteArray("readout with both known: two locations and the gap (") + r1.toUtf8() + ")");
        qint64 aUnknown = -1;
        for (const SpeedDistance::Sample &s : p.a.samples)
            if (!SpeedDistance::locationKnown(s)) { aUnknown = s.epochMs; break; }
        const QString r2 = canvas.readoutAt(aUnknown);
        CHECK(r2.contains(QLatin1String("A not localised (0 m)")) && r2.contains(QLatin1String("no gap")),
              QByteArray("readout with A at 0 m: says so, no gap (") + r2.toUtf8() + ")");

        QSignalSpy spy(&canvas, &TwoLocoCanvas::cursorChanged);
        const QPoint pos(canvas.width() / 2, canvas.height() / 3);
        QMouseEvent move(QEvent::MouseMove, pos, canvas.mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &move);
        CHECK(spy.count() == 1 && canvas.cursorMs() > 0, "pointing at the plot sets the cursor the header reads");
        CHECK(canvas.minimumSizeHint().height() <= 400, "the canvas does not demand a tall window");
    }

    // ---- the window -------------------------------------------------------------------------------
    {
        TwoLocoWindow w(&disp);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.setSources(QStringLiteral("21_1"), QStringLiteral("22_1"));
        w.resize(1100, 680);
        w.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

        auto *spin = w.findChild<QDoubleSpinBox *>(QStringLiteral("warnApart"));
        auto *status = w.findChild<StatusLine *>();
        CHECK(spin && qFuzzyCompare(spin->value(), 10.0), "the limit box shows the setting (10 km)");
        const QString st = status ? status->text() : QString();
        CHECK(w.pair().plausible && st.startsWith(QLatin1String("25 gap samples")),
              QByteArray("no warning at 10 km; the count reads as English (") + st.toUtf8() + ")");
        CHECK(st.contains(QLatin1String("A: 132 of 157 frames not localised (0 m)")),
              "the status says what was left out");
        CHECK(status && !status->toolTip().isEmpty(), "and its tooltip says why");
        if (spin) {
            spin->setValue(0.0);
            CHECK(!w.pair().plausible && status->text().contains(QLatin1String("may not be on the same section")),
                  "\"any gap\" (0): the old rule warns, in one short line");
            CHECK(status->toolTip().contains(QLatin1String("do not overlap")), "the full sentence is the tooltip");
            CHECK(qFuzzyIsNull(Settings::twoLocoWarnApartKm()), "the limit is remembered");
            CHECK(status->fontMetrics().horizontalAdvance(status->text()) < w.width() - 260,
                  "the warning fits one line beside the buttons");
        }
        CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366,
              QByteArray("fits a 1366 x 768 laptop (minimum ")
                  + QByteArray::number(w.minimumSizeHint().width()) + " x "
                  + QByteArray::number(w.minimumSizeHint().height()) + ")");
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (")
                                   + loose.join(QLatin1String(", ")).toUtf8() + ")");
    }

    Settings::setTwoLocoWarnApartKm(savedWarn);
}
