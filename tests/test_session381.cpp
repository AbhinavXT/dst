#include "testutil.h"

#include "bookmarks.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "notessidecar.h"
#include "reviewpack.h"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 381 — reopening a review pack keeps the reviewer's notes: the
//  notes files beside the unpacked captures are not put back to the site's.
//  Real frames: 14:02-14:03 of replay/loco_1_1_27062026_140226.cap.
// =============================================================================

namespace {
qint64 at381(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-06-27T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session381)
{
    QTemporaryDir tmp;
    MessageDispatcher disp;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: the capture opens");
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3 || !l.startsWith('@')) continue;
            const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
            if (ms < at381("14:02:00")) continue;
            if (ms >= at381("14:03:00")) break;
            disp.ingestLocal(21, 1, l, ms, QString());
        }
        disp.drainNow();
    }
    LogModel *radio = disp.modelForKey(QStringLiteral("21_1"));
    CHECK(radio && radio->count() > 100, "fixture: a tab of real frames");
    if (!radio) return;

    BookmarkStore site;
    site.load(tmp.filePath(QStringLiteral("site.json")));
    site.toggle(QStringLiteral("21_1"), at381("14:02:30"), QStringLiteral("@slrp …"));
    site.setNote(0, QStringLiteral("site: EB here"));

    ReviewPack::Input in;
    in.site = QStringLiteral("Yard");
    in.captures = { { QStringLiteral("21_1"), QString(), radio->snapshot(), site.all() } };
    in.createdMs = at381("15:00:00");
    const QString zip = tmp.filePath(QStringLiteral("Pack.zip"));
    CHECK(ReviewPack::write(zip, in).ok, "fixture: the pack");

    const QString dir = ReviewPack::defaultDir(zip);
    const ReviewPack::Opened first = ReviewPack::open(zip, dir);
    CHECK(first.ok && !first.reused && !first.summary().contains(QLatin1String("before")),
          "the first open unpacks it: " + first.summary().toUtf8());

    // The reviewer's note goes into the notes file beside the unpacked .dlr,
    // as the viewer writes it (session 369).
    QVector<Bookmark> marks = NotesSidecar::read(first.dlrs.first());
    CHECK(marks.size() == 1, "the site's note is beside the capture");
    Bookmark mine = marks.first();
    mine.epochMs = at381("14:02:45");
    mine.note = QStringLiteral("reviewer: the DMI shows it 2 s later");
    marks.append(mine);
    CHECK(NotesSidecar::write(first.dlrs.first(), marks), "fixture: the reviewer's note written beside it");

    const ReviewPack::Opened again = ReviewPack::open(zip, dir);
    CHECK(again.ok && again.reused && again.summary().contains(QLatin1String("notes made since kept")),
          "the second open says it was unpacked before: " + again.summary().toUtf8());
    CHECK(NotesSidecar::read(again.dlrs.first()).size() == 2, "and the reviewer's note is still beside the capture");
    CHECK(QFile::exists(again.dlrs.first()), "the capture itself is still there");

    // Another pack (a new export) into the same folder is a new pack: its
    // notes are the site's.
    in.createdMs = at381("16:00:00");
    CHECK(ReviewPack::write(zip, in).ok, "fixture: the site exports again");
    const ReviewPack::Opened other = ReviewPack::open(zip, dir);
    CHECK(other.ok && !other.reused && NotesSidecar::read(other.dlrs.first()).size() == 1,
          "a different pack replaces the folder's notes with its own");
}
