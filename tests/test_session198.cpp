#include "testutil.h"
#include "layoutaudit.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "planrun.h"
#include "rfidtag.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>

// =============================================================================
//  Session 198 — RFID Tag Builder, phase D: plan vs run.
//
//  REAL @rfid frames: replay/loco_1_1_27062026_140226.cap (62 reads). The
//  plans are made from that run (routeFromRun) and then changed one way at a
//  time, so every state is seen against real reads.
// =============================================================================

namespace {

LogModel *load(MessageDispatcher &d)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        d.ingestLocal(1, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    d.drainNow();
    return d.modelForKey(QStringLiteral("1_1"));
}

QByteArray withLoc(const QByteArray &tag, qint64 loc)
{
    QHash<QString, qint64> v = RfidTag::values(tag);
    v.insert(v.contains(QStringLiteral("abs_loc")) ? QStringLiteral("abs_loc") : QStringLiteral("abs_loc_1"), loc);
    return RfidTag::build(v);
}

}  // namespace

TEST_SUITE(session198)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher d;
    LogModel *m = load(d);
    CHECK(m != nullptr, "the real capture loads");
    if (!m) return;

    // ---- 1. the reads and a route made from them ----------------------------------------------
    const QVector<PlanRun::Read> reads = PlanRun::readsOf(m);
    CHECK(reads.size() > 20, QByteArray("its @rfid frames, the null tag left out: ") + QByteArray::number(reads.size()));
    const RfidTag::Route run = PlanRun::routeFromRun(reads);
    QStringList names;
    for (const RfidTag::Tag &t : run.tags) names << t.name;
    CHECK(run.tags.size() >= 3 && run.dir != RfidTag::DirUnset && names.removeDuplicates() == 0,
          QByteArray("a route from the run: each tag once, in the order first read, direction from the locations: ")
              + names.join(QLatin1Char(' ')).toUtf8());

    // ---- 2. every state -----------------------------------------------------------------------
    {
        const PlanRun::Result same = PlanRun::compare(run, reads);
        CHECK(same.count(PlanRun::State::Read) == run.tags.size() && same.notPlanned.isEmpty(),
              "the run against itself: every tag read as planned, nothing unplanned");

        RfidTag::Route plan = run;
        // (a) a tag the loco never read
        QHash<QString, qint64> v{ { QStringLiteral("type"), 9 }, { QStringLiteral("version"), 1 },
                                  { QStringLiteral("unique"), 1001 }, { QStringLiteral("abs_loc"), 100 } };
        plan.tags.append({ QString(), RfidTag::build(v) });
        // (b) a tag planned somewhere else than it is
        const qint64 own = RfidTag::summary(plan.tags.at(1).bytes).absLoc;
        plan.tags[1].bytes = withLoc(plan.tags.at(1).bytes, own + 50);
        // (c) a tag the plan leaves out
        const QString dropped = plan.tags.at(0).name;
        plan.tags.remove(0);
        // (d) two tags the other way round (rows 2 and 3 of what is left)
        std::swap(plan.tags[2], plan.tags[3]);

        const PlanRun::Result r = PlanRun::compare(plan, reads);
        CHECK(r.planned.last().state == PlanRun::State::NotRead && r.planned.last().firstMs == 0,
              "a planned tag with no frame: not read");
        CHECK(r.planned.at(0).state == PlanRun::State::Different
                  && r.planned.at(0).detail.contains(QStringLiteral("abs_loc %1 → %2").arg(own + 50).arg(own)),
              QByteArray("a tag read with other contents: different, and the field named: ") + r.planned.at(0).detail.toUtf8());
        CHECK(r.notPlanned == QStringList({ dropped }), QByteArray("a tag read but not planned: ") + r.notPlanned.join(QLatin1Char(' ')).toUtf8());
        CHECK(r.planned.at(3).state == PlanRun::State::OutOfOrder && r.planned.at(3).detail.contains(r.planned.at(2).name),
              QByteArray("two tags swapped: the second is read out of order, before the one it now follows: ")
                  + r.planned.at(3).detail.toUtf8());
    }

    // ---- 3. the window ------------------------------------------------------------------------
    {
        TagBuilderWindow w(&d);
        w.resize(1100, 680);
        w.show();
        CHECK(w.tabs()->count() == 4 && w.tabs()->tabText(3) == QLatin1String("Run"), "a Run tab");
        w.setRunSource(QStringLiteral("1_1"));
        CHECK(w.makeRouteFromRun() && w.route().tags.size() == run.tags.size() && w.isModified(),
              "Make a route from this run");
        RfidTag::Route plan = w.route();
        plan.tags.remove(0);
        w.setRoute(plan);
        CHECK(w.compareRun() && w.tabs()->currentIndex() == 3 && w.runTable()->rowCount() == plan.tags.size() + 1
                  && w.runTable()->item(plan.tags.size(), 2)->text() == QLatin1String("not planned")
                  && w.runSummary()->text().contains(QStringLiteral("%1 read as planned").arg(plan.tags.size())),
              QByteArray("Compare: a row per planned tag, the unplanned one last, and a summary: ") + w.runSummary()->text().toUtf8());
        QSignalSpy jump(&w, &TagBuilderWindow::jumpRequested);
        emit w.runTable()->cellDoubleClicked(0, 1);
        CHECK(jump.size() == 1 && jump.at(0).at(0).toString() == QLatin1String("1_1")
                  && jump.at(0).at(1).toLongLong() == w.runResult().planned.at(0).firstMs,
              "double-click: show the read in the log");

        TagBuilderWindow none;
        CHECK(!none.compareRun() && none.status()->kind() == StatusLine::Kind::Warn, "with no log to compare with, it says so");

        CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700,
              QByteArray("fits a laptop (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
                  + QByteArray::number(w.minimumSizeHint().height()) + ")");
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), QByteArray("no widget outside a layout: ") + loose.join(QLatin1Char(' ')).toUtf8());
        w.setRoute(RfidTag::Route());
    }
}
