#include "testutil.h"
#include "archivesearch.h"
#include "colorrules.h"
#include "logquery.h"
#include "sessionfile.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace {
// Write a .dlr containing `n` records for (src,kvch), one per second.
QString writeSession(const QString &dir, quint8 src, quint16 kv,
                     qint64 startMs, int n, const char *textPrefix)
{
    QDir().mkpath(dir);
    const QString path = QStringLiteral("%1/%2_%3.dlr").arg(dir).arg(src).arg(kv);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();

    SessionFile::FileHeader h;
    h.createdMs = startMs; h.sourceId = src; h.kvchId = kv;
    f.write(SessionFile::encodeHeader(h));

    for (int i = 0; i < n; ++i) {
        QByteArray body = QByteArray(textPrefix) + " " + QByteArray::number(i);
        // wire = 7-byte header (src,dst,mid,len,kvch LE) + payload
        QByteArray wire;
        wire.append(char(src)); wire.append(char(101)); wire.append(char(7));
        wire.append(char(body.size() & 0xFF)); wire.append(char(body.size() >> 8));
        wire.append(char(kv & 0xFF)); wire.append(char(kv >> 8));
        wire.append(body);
        f.write(SessionFile::encodeRecord(startMs + i * 1000, wire));
    }
    f.close();
    return path;
}
}  // namespace

TEST_SUITE(archivesearch)
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp root created");
    const QString root = tmp.path();

    const qint64 day1 = QDateTime(QDate(2026,8,5), QTime(10,0)).toMSecsSinceEpoch();
    const qint64 day2 = QDateTime(QDate(2026,8,6), QTime(10,0)).toMSecsSinceEpoch();
    const qint64 day3 = QDateTime(QDate(2026,8,7), QTime(10,0)).toMSecsSinceEpoch();

    writeSession(root + "/2026-08-05", 33, 1, day1, 10, "RAD IN Link 1 Error");
    writeSession(root + "/2026-08-06", 33, 1, day2, 10, "RAD IN Link 2 No Error");
    writeSession(root + "/2026-08-06", 21, 2, day2, 10, "CAN OUT status");
    writeSession(root + "/2026-08-07", 99, 7, day3, 10, "VCC IN nominal");
    // A directory that is not a date must be ignored, not guessed at.
    writeSession(root + "/not-a-date", 55, 5, day1, 5, "SHOULD NOT APPEAR");

    // ---- file discovery ---------------------------------------------------
    { const QStringList all = findSessionFiles(root);
      CHECK(all.size() == 4, "four dated sessions found, non-date dir skipped");
      bool anyBad = false;
      for (const QString &f : all) if (f.contains("not-a-date")) anyBad = true;
      CHECK(!anyBad, "non-date directory really is excluded"); }

    CHECK(findSessionFiles("").isEmpty(),                  "empty root -> nothing");
    CHECK(findSessionFiles(root + "/nope").isEmpty(),      "missing root -> nothing");

    { const QStringList d2 = findSessionFiles(root, QDate(2026,8,6), QDate(2026,8,6));
      CHECK(d2.size() == 2, "single-day filter returns that day's two files"); }
    { const QStringList span = findSessionFiles(root, QDate(2026,8,5), QDate(2026,8,6));
      CHECK(span.size() == 3, "two-day span"); }
    { const QStringList openEnd = findSessionFiles(root, QDate(2026,8,6), QDate());
      CHECK(openEnd.size() == 3, "open-ended 'to' includes later days"); }
    { const QStringList openStart = findSessionFiles(root, QDate(), QDate(2026,8,5));
      CHECK(openStart.size() == 1, "open-ended 'from' includes earlier days"); }
    { const QStringList none = findSessionFiles(root, QDate(2027,1,1), QDate(2027,1,2));
      CHECK(none.isEmpty(), "date range with no data returns nothing"); }

    // ---- scanning ---------------------------------------------------------
    ColorRules rules;
    rules.loadFromFile("/nonexistent");            // defaults
    const QStringList all = findSessionFiles(root);

    { LogQuery q; CHECK(q.parse("Link"), "query parses");
      const ArchiveScanResult r = scanArchive(all, q, &rules);
      CHECK(r.filesScanned == 4,     "all four files opened");
      CHECK(r.recordsScanned == 40,  "all forty records streamed");
      CHECK(r.hits.size() == 20,     "twenty records mention Link");
      CHECK(!r.hitCap,               "cap not reached");
      CHECK(!r.cancelled,            "not cancelled");
      CHECK(r.filesUnreadable == 0,  "no unreadable files");
      bool ordered = true;
      for (int i = 1; i < r.hits.size(); ++i)
          if (r.hits[i].epochMs < r.hits[i-1].epochMs) ordered = false;
      CHECK(ordered, "hits are interleaved by time across files"); }

    // Severity comes from re-classification at read time, not from the file.
    { LogQuery q; q.parse("sev:error");
      const ArchiveScanResult r = scanArchive(all, q, &rules);
      CHECK(r.hits.size() == 10, "ten error records (the 'Link 1 Error' set)");
      bool allErr = true, allDay1 = true;
      for (const ArchiveHit &h : r.hits) {
          if (h.severity != Severity::Error) allErr = false;
          if (h.epochMs < day1 || h.epochMs > day1 + 10000) allDay1 = false;
      }
      CHECK(allErr, "every hit really is an error");
      CHECK(allDay1, "and they are the day-1 session");
      CHECK(r.hits.first().tabKey == "33_1", "tab key recovered from the file header"); }

    // 'No Error' must not be classified as an error — the tier-1 fix, now
    // exercised through the archive path.
    { LogQuery q; q.parse("sev:error \"No Error\"");
      CHECK(scanArchive(all, q, &rules).hits.isEmpty(),
            "'No Error' records are not errors"); }

    // Field and time predicates work against archived records.
    { LogQuery q; q.parse("src:99_7");
      CHECK(scanArchive(all, q, &rules).hits.size() == 10, "src: filter"); }
    { LogQuery q; q.parse("dir:out");
      CHECK(scanArchive(all, q, &rules).hits.size() == 10, "dir: filter"); }
    { LogQuery q;
      q.parse(QStringLiteral("after:%1")
                  .arg(QDateTime::fromMSecsSinceEpoch(day3).toString(Qt::ISODate)));
      const ArchiveScanResult r = scanArchive(all, q, &rules);
      CHECK(r.hits.size() == 10, "after: excludes earlier days"); }

    // Empty query matches everything; a hit cap must be honoured and reported.
    { LogQuery q; q.parse("");
      const ArchiveScanResult r = scanArchive(all, q, &rules, 7);
      CHECK(r.hits.size() == 7, "hit cap honoured");
      CHECK(r.hitCap,           "and reported"); }

    // Cancellation stops early and says so.
    { LogQuery q; q.parse("");
      const ArchiveScanResult r = scanArchive(all, q, &rules, 5000,
                                              []() { return true; });
      CHECK(r.cancelled, "cancellation reported");
      CHECK(r.recordsScanned < 40, "and it actually stopped early"); }

    // Progress is reported once per file.
    { LogQuery q; q.parse("");
      int calls = 0, lastTotal = 0;
      scanArchive(all, q, &rules, 5000, {},
                  [&](int done, int total) { ++calls; lastTotal = total;
                                             CHECK(done <= total, "progress in range"); });
      CHECK(calls == all.size(), "one progress call per file");
      CHECK(lastTotal == all.size(), "total is the file count"); }

    // A truncated file yields its complete records and a warning, not a failure.
    { const QString victim = all.first();
      QFile f(victim); f.open(QIODevice::ReadWrite);
      f.resize(f.size() - 6); f.close();
      LogQuery q; q.parse("");
      const ArchiveScanResult r = scanArchive({ victim }, q, &rules);
      CHECK(r.filesScanned == 1,       "truncated file still counts as scanned");
      CHECK(r.filesUnreadable == 0,    "and is not counted unreadable");
      CHECK(r.hits.size() == 9,        "nine of ten records recovered");
      CHECK(!r.warnings.isEmpty(),     "truncation produces a warning"); }

    // A file that is not a .dlr at all is reported, and does not abort the scan.
    { const QString junk = root + "/2026-08-07/junk.dlr";
      QFile f(junk); f.open(QIODevice::WriteOnly); f.write("not a session"); f.close();
      LogQuery q; q.parse("");
      const QStringList withJunk = findSessionFiles(root);
      CHECK(withJunk.contains(junk), "junk file is enumerated");
      const ArchiveScanResult r = scanArchive(withJunk, q, &rules);
      CHECK(r.filesUnreadable == 1, "junk file reported unreadable");
      CHECK(r.filesScanned >= 3,    "other files still scanned");
      CHECK(!r.warnings.isEmpty(),  "and a warning recorded"); }

    CHECK(scanArchive({}, LogQuery(), &rules).hits.isEmpty(), "no files -> no hits");
}
