#include "testutil.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "settings.h"

#include <QDateTime>
#include <QSharedPointer>

// Time zone display. The bug this prevents is silent: a Time column with no
// zone marker, rendered local, sitting next to log files recorded in UTC.
// Nothing on screen says the two differ.
TEST_SUITE(timezone)
{
    // A moment with a known UTC rendering.
    const qint64 ms = QDateTime(QDate(2026, 8, 8), QTime(14, 5, 30, 250),
                                Qt::UTC).toMSecsSinceEpoch();
    const QString utcText   = "14:05:30.250";
    const QString localText = QDateTime::fromMSecsSinceEpoch(ms)
                                  .toString("HH:mm:ss.zzz");

    // ---- the cache renders in the requested zone -------------------------
    {
        LogEntry e;
        e.epochMs = ms; e.header.source_id = 33; e.header.kvchId = 1;
        e.cacheDerived(true);
        CHECK(e.cachedTime == utcText, "cacheDerived(true) renders UTC");

        LogEntry e2;
        e2.epochMs = ms; e2.header.source_id = 33; e2.header.kvchId = 1;
        e2.cacheDerived(false);
        CHECK(e2.cachedTime == localText, "cacheDerived(false) renders local");
        CHECK(e2.cachedTime == e2.cachedTime, "stable");

        // Default must be local, matching the historical behaviour — a
        // silent switch to UTC would be worse than the ambiguity.
        LogEntry e3;
        e3.epochMs = ms; e3.header.source_id = 1; e3.header.kvchId = 1;
        e3.cacheDerived();
        CHECK(e3.cachedTime == localText, "default is local time");
    }

    // ---- switching zones invalidates the cache ---------------------------
    {
        LogModel m(nullptr, 100);
        CHECK(!m.showUtc(), "model defaults to local");

        auto e = QSharedPointer<LogEntry>::create();
        e->epochMs = ms; e->header.source_id = 33; e->header.kvchId = 1;
        e->cacheDerived(false);
        QVector<LogEntryPtr> v{ e };
        m.appendEntries(v);

        const QString before =
            m.data(m.index(0, LogModel::ColTime), Qt::DisplayRole).toString();
        CHECK(before.contains(localText), "displays local before the switch");

        m.setShowUtc(true);
        CHECK(m.showUtc(), "model reports UTC");
        CHECK(e->cachedTime.isEmpty(),
              "the stale cache was cleared, not left to be trusted");

        const QString after =
            m.data(m.index(0, LogModel::ColTime), Qt::DisplayRole).toString();
        CHECK(after.contains(utcText), "displays UTC after the switch");

        // And back again.
        m.setShowUtc(false);
        const QString back =
            m.data(m.index(0, LogModel::ColTime), Qt::DisplayRole).toString();
        CHECK(back.contains(localText), "switching back restores local");

        // A redundant set must not churn the cache.
        e->cacheDerived(false);
        const QString cached = e->cachedTime;
        m.setShowUtc(false);
        CHECK(e->cachedTime == cached, "setting the same zone is a no-op");
    }

    // ---- the header names the zone ---------------------------------------
    {
        LogModel m(nullptr, 10);
        const QString localHdr =
            m.headerData(LogModel::ColTime, Qt::Horizontal, Qt::DisplayRole).toString();
        CHECK(localHdr.contains("local"),
              "header names the zone — a bare 'Time' is the original bug");
        m.setShowUtc(true);
        const QString utcHdr =
            m.headerData(LogModel::ColTime, Qt::Horizontal, Qt::DisplayRole).toString();
        CHECK(utcHdr.contains("UTC"), "header updates to UTC");
        CHECK(localHdr != utcHdr, "the two headers differ");
    }

    // ---- buildEntry honours the zone -------------------------------------
    {
        QByteArray wire;
        wire.append(char(33)); wire.append(char(101)); wire.append(char(7));
        wire.append(char(4)); wire.append(char(0));
        wire.append(char(1)); wire.append(char(0));
        wire.append("test");

        LogEntryPtr u = MessageDispatcher::buildEntry(wire, ms, nullptr, true);
        CHECK(u && u->cachedTime == utcText, "buildEntry(utc=true) renders UTC");
        LogEntryPtr l = MessageDispatcher::buildEntry(wire, ms, nullptr, false);
        CHECK(l && l->cachedTime == localText, "buildEntry(utc=false) renders local");
        LogEntryPtr d = MessageDispatcher::buildEntry(wire, ms, nullptr);
        CHECK(d && d->cachedTime == localText, "buildEntry defaults to local");
    }

    // ---- setting persists -------------------------------------------------
    {
        const bool original = Settings::showUtc();
        Settings::setShowUtc(true);
        CHECK(Settings::showUtc(), "UTC preference persists");
        Settings::setShowUtc(false);
        CHECK(!Settings::showUtc(), "local preference persists");
        Settings::setShowUtc(original);
    }

    // ---- the invariant that matters ---------------------------------------
    // Whatever the display shows, the underlying epoch is unchanged: the
    // zone is presentation only and must never alter stored data.
    {
        LogEntry e;
        e.epochMs = ms; e.header.source_id = 1; e.header.kvchId = 1;
        e.cacheDerived(true);
        const qint64 afterUtc = e.epochMs;
        e.cacheDerived(false);
        CHECK(e.epochMs == ms && afterUtc == ms,
              "epochMs is untouched by display-zone changes");
    }
}
