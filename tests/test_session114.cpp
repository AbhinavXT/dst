#include "testutil.h"

#include "archivesearch.h"
#include "colorrules.h"
#include "dlrplayer.h"
#include "logmodel.h"
#include "sessionfile.h"

#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>

// =============================================================================
//  Session 114 — time, read the same way everywhere.
//
//  6. The filter bar scanned every row on each keystroke to find the newest
//     time for last:. LogModel now keeps it as rows go in.
//  7. Archive search did not get the display zone or a data end, so time:
//     and last: meant something else there than in a tab's filter. It now
//     reads clock times in the zone the tables show, and counts last: back
//     from the newest record in the files searched.
//
//  Real line: @lsrp from replay/loco_1_1_27062026_140226.cap.
// =============================================================================

namespace {

const QByteArray kLsrp =
    "@lsrp_1_1 2026-06-27T14:02:27 21441 02 07 0A 00 27 00 00 00 0F 02 A3 AC 57 "
    "40 00 01 40 9F FB 41 E0 F0 7D 00 10 FC 30 15 20 8D 00 F2 F3 26 DD C6 ED 59 9B";

LogEntryPtr at(qint64 ms)
{
    auto e = QSharedPointer<LogEntry>::create();
    e->epochMs = ms;
    e->text = QString::fromLatin1(kLsrp);
    return e;
}

// One .dlr, one record a minute from `startMs`.
QString writeSession(const QString &dir, qint64 startMs, int n)
{
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/21_1.dlr");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    SessionFile::FileHeader h;
    h.createdMs = startMs; h.sourceId = 21; h.kvchId = 1;
    f.write(SessionFile::encodeHeader(h));
    for (int i = 0; i < n; ++i) {
        QByteArray wire;
        wire.append(char(21)); wire.append(char(101)); wire.append(char(7));
        wire.append(char(kLsrp.size() & 0xFF)); wire.append(char(kLsrp.size() >> 8));
        wire.append(char(1)); wire.append(char(0));
        wire.append(kLsrp);
        f.write(SessionFile::encodeRecord(startMs + i * 60000, wire));
    }
    return path;
}

// Received the way ArchiveSearchWindow receives it: by a receiver on THIS
// (the GUI) thread, so the signal is queued across threads. QSignalSpy
// connects directly and would have hidden the bug this session fixed.
ArchiveScanResult search(const QStringList &files, const QString &q, bool utc, bool *delivered = nullptr)
{
    ArchiveSearcher w(files, q, ColorRules(), 1000, nullptr, utc);
    QObject receiver;
    ArchiveScanResult got;
    bool arrived = false;
    QObject::connect(&w, &ArchiveSearcher::finished, &receiver,
                     [&](ArchiveScanResult r) { got = r; arrived = true; });
    w.start();
    w.wait(10000);
    QElapsedTimer t;
    t.start();
    while (!arrived && t.elapsed() < 2000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (delivered) *delivered = arrived;
    return got;
}

}  // namespace

TEST_SUITE(session114)
{
    // ---- 6. the newest time, kept as rows go in --------------------------------------
    {
        LogModel m(nullptr, 10, 0.5);
        CHECK(m.newestMs() == 0, "empty: 0");
        m.appendEntry(at(5000));
        m.appendEntries({ at(9000), at(7000) });        // out of order, as a replay can be
        CHECK(m.newestMs() == 9000, "the newest, not the last appended");
        for (int i = 0; i < 12; ++i) m.appendEntry(at(1000 + i));   // trims the oldest
        CHECK(m.newestMs() == 9000, "trimming the oldest rows does not lose it");
        m.clear();
        CHECK(m.newestMs() == 0, "clear resets it");
        m.appendEntry(at(4000));
        m.restoreOlder({ at(20000) });          // room now: restored rows go in
        CHECK(m.newestMs() == 20000, "restored rows count too");
        m.appendEntry(at(3000));
        m.takeAll();
        CHECK(m.newestMs() == 0, "take-all resets it");
    }

    // ---- 7. archive search reads time like the tab filter ------------------------------
    QTemporaryDir tmp;
    // 27 June 2026, 14:00-14:09 local: ten records, one a minute.
    const qint64 start = QDateTime(QDate(2026, 6, 27), QTime(14, 0)).toMSecsSinceEpoch();
    const QString file = writeSession(tmp.path() + QStringLiteral("/2026-06-27"), start, 10);
    CHECK(!file.isEmpty(), "fixture: a .dlr of real @lsrp lines from 27 June");

    CHECK(ArchiveSearcher::newestRecordMs({ file }) == start + 9 * 60000, "the newest record is found");

    {
        bool delivered = false;
        const ArchiveScanResult r0 = search({ file }, QStringLiteral("@lsrp"), false, &delivered);
        CHECK(delivered && r0.hits.size() == 10,
              "the result reaches a receiver on the GUI thread (before: dropped, the window showed nothing)");
    }
    {
        const ArchiveScanResult r = search({ file }, QStringLiteral("last:3m"), false);
        CHECK(r.hits.size() == 4, QByteArray("last:3m in a June archive: the last 3 minutes OF THE ARCHIVE (")
                                     + QByteArray::number(r.hits.size()) + " hits; counted from now it was 0)");
    }
    {
        const ArchiveScanResult r = search({ file }, QStringLiteral("time:14:02..14:04"), false);
        CHECK(r.hits.size() == 3, "a clock window matches by local clock on a past day");
    }
    {
        // The same instants by their UTC clock, with the tables in UTC.
        const QString utcFrom = QDateTime::fromMSecsSinceEpoch(start + 2 * 60000, Qt::UTC).toString(QStringLiteral("HH:mm"));
        const QString utcTo   = QDateTime::fromMSecsSinceEpoch(start + 4 * 60000, Qt::UTC).toString(QStringLiteral("HH:mm"));
        const ArchiveScanResult r = search({ file }, QStringLiteral("time:%1..%2").arg(utcFrom, utcTo), true);
        CHECK(r.hits.size() == 3, "with the tables in UTC, archive clock times are UTC too");
    }
    {
        const ArchiveScanResult r = search({ file }, QStringLiteral("FRAME_NUM=50548"), false);
        CHECK(r.hits.size() == 10, "a query without last: is unchanged (no extra pass needed)");
    }
    // ---- the DLR player's progress reaches the dialog ----------------------------------
    {
        DlrPlayer::Source src;
        QString err;
        CHECK(DlrPlayer::probe(file, &src, &err), "the archive plays");
        DlrPlayer p;
        p.setSources({ src });
        p.setTarget(QHostAddress::LocalHost, 50998);
        p.setSpeed(1000);                        // ten minutes of records in well under a second
        QObject receiver;                        // on this (the GUI) thread, as the dialog is
        int updates = 0;
        bool finished = false;
        QObject::connect(&p, &DlrPlayer::progress, &receiver, [&](DlrPlayer::Stats) { ++updates; });
        QObject::connect(&p, &DlrPlayer::finished, &receiver, [&](bool) { finished = true; });
        p.play();
        QElapsedTimer t;
        t.start();
        while (!finished && t.elapsed() < 5000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        p.wait(3000);
        QCoreApplication::processEvents();
        CHECK(finished && updates > 0,
              QByteArray("progress updates reach the GUI thread (before: dropped, the readout never moved): ")
                  + QByteArray::number(updates));
    }

    {
        LogQuery q;
        q.parse(QStringLiteral("last:5m"));
        CHECK(q.usesDataEnd(), "a query says when it needs the data end");
        q.parse(QStringLiteral("time:14:02"));
        CHECK(!q.usesDataEnd(), "…and when it does not");
    }
}
