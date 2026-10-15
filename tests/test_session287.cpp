#include "testutil.h"

#include "fieldcatalog.h"
#include "mainwindow.h"
#include "recentfiles.h"
#include "replaywindow.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "settings.h"
#include "tagbuilderwindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QMenu>
#include <QMimeData>
#include <QTemporaryDir>
#include <QUrl>

// =============================================================================
//  Session 287 — File ▸ Open Recent, and files dropped on the window. Real
//  files: a replay/ capture, a .dlr of its lines, a tag_scenarios route.
// =============================================================================

namespace {
QList<ReplayWindow *> replayWindows()
{
    QList<ReplayWindow *> out;
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *r = qobject_cast<ReplayWindow *>(w)) out << r;
    return out;
}

// Qt delivers a Drop only to the widget that accepted the DragEnter, so the
// enter goes first, as a real drag's does. Returns whether it was accepted.
bool dropOn(QWidget *w, const QStringList &paths)
{
    QMimeData mime;
    QList<QUrl> urls;
    for (const QString &p : paths) urls << QUrl::fromLocalFile(p);
    mime.setUrls(urls);
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &enter);
    if (!enter.isAccepted()) return false;
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &drop);
    return true;
}
}  // namespace

TEST_SUITE(session287)
{
    using RecentFiles::Kind;
    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");
    QTemporaryDir where;
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    { QFile f(exe); CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the .exe"); }
    qputenv("DLCONSOLE_PACKAGE", exe.toLocal8Bit());

    // ---- kinds --------------------------------------------------------
    CHECK(RecentFiles::kindOf(QStringLiteral("a/21_1.DLR")) == Kind::Session, ".dlr (any case): a recorded session");
    CHECK(RecentFiles::kindOf(QStringLiteral("loco_1_1.cap")) == Kind::Capture, ".cap: a capture");
    CHECK(RecentFiles::kindOf(QStringLiteral("3.40.tagroute.xml")) == Kind::TagRoute, ".tagroute.xml: a tag route");
    CHECK(RecentFiles::kindOf(QStringLiteral("kavach.xml")) == Kind::Unknown
              && RecentFiles::kindOf(QStringLiteral("route.xml")) == Kind::Unknown
              && RecentFiles::kindOf(QStringLiteral("notes.txt")) == Kind::Unknown,
          "any other .xml, and other files: unknown");

    // ---- the list -----------------------------------------------------
    const QString a = where.filePath(QStringLiteral("a.dlr")), b = where.filePath(QStringLiteral("b.cap"));
    QStringList l = RecentFiles::added({}, a);
    l = RecentFiles::added(l, b);
    CHECK(l == QStringList({ b, a }), "newest first");
    l = RecentFiles::added(l, where.filePath(QStringLiteral("./a.dlr")));
    CHECK(l == QStringList({ a, b }), "the same file again moves to the front, once");
    QStringList many;
    for (int i = 0; i < 15; ++i) many = RecentFiles::added(many, where.filePath(QStringLiteral("%1.cap").arg(i)));
    CHECK(many.size() == RecentFiles::kMax && many.first().endsWith(QLatin1String("14.cap"))
              && many.last().endsWith(QLatin1String("5.cap")),
          "at most ten, the oldest dropped");

    RecentFiles::clear();
    CHECK(RecentFiles::list().isEmpty(), "a fresh INI: no recent files");
    RecentFiles::addAll({ a, b });
    CHECK(RecentFiles::list() == QStringList({ a, b }), "addAll keeps the order given, first in front");

    // ---- real files ---------------------------------------------------
    const QString cap = QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap");
    const QString route = QStringLiteral(DL_SRC_DIR "/tag_scenarios/3.40.tagroute.xml");
    const QString dlr = where.filePath(QStringLiteral("21_1.dlr"));
    {
        QStringList lines;
        QFile f(cap);
        if (f.open(QIODevice::ReadOnly))
            while (!f.atEnd() && lines.size() < 50) {
                const QString line = QString::fromUtf8(f.readLine()).trimmed();
                if (line.startsWith(QLatin1Char('@'))) lines << line;
            }
        QFile out(dlr);
        if (out.open(QIODevice::WriteOnly)) {
            const qint64 start = QDateTime(QDate(2026, 6, 27), QTime(14, 2, 26)).toMSecsSinceEpoch();
            SessionFile::FileHeader h;
            h.createdMs = start; h.sourceId = 21; h.kvchId = 1;
            out.write(SessionFile::encodeHeader(h));
            for (int i = 0; i < lines.size(); ++i) {
                const QByteArray body = lines.at(i).toUtf8();
                QByteArray wire;
                wire.append(char(21)); wire.append(char(101)); wire.append(char(7));
                wire.append(char(body.size() & 0xFF)); wire.append(char(body.size() >> 8));
                wire.append(char(1)); wire.append(char(0));
                wire.append(body);
                out.write(SessionFile::encodeRecord(start + i * 137, wire));
            }
        }
        CHECK(lines.size() == 50, "fixture: a .dlr of 50 real capture lines");
    }

    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    {
        RecentFiles::clear();
        MainWindow w;
        QMenu *menu = w.recentMenu();
        CHECK(menu && menu->actions().size() == 1 && !menu->actions().first()->isEnabled(),
              "File > Open Recent: \"(none yet)\", greyed out");

        // Unknown and missing files are refused, by name.
        const QString missing = where.filePath(QStringLiteral("gone.cap"));
        const QString txt = where.filePath(QStringLiteral("notes.txt"));
        { QFile f(txt); f.open(QIODevice::WriteOnly); }
        CHECK(w.openFiles({ txt, missing }) == QStringList({ txt, missing }), "an unknown and a missing file: not opened");
        CHECK(RecentFiles::list().isEmpty(), "and not added to Open Recent");

        // Each kind opens where it belongs.
        const int replaysBefore = replayWindows().size();
        CHECK(w.openFiles({ cap }).isEmpty() && replayWindows().size() == replaysBefore + 1, "a .cap opens a replay window");
        CHECK(w.openFiles({ dlr }).isEmpty() && w.findChildren<SessionWindow *>().size() == 1, "a .dlr opens a session viewer");
        CHECK(w.openFiles({ route }).isEmpty() && w.findChildren<TagBuilderWindow *>().size() == 1
                  && w.findChildren<TagBuilderWindow *>().first()->route().tags.size() > 0,
              "a .tagroute.xml opens the Tag Builder on that route");

        const QStringList recent = RecentFiles::list();
        CHECK(recent.size() == 3 && recent.at(0).endsWith(QLatin1String("3.40.tagroute.xml"))
                  && recent.at(1).endsWith(QLatin1String("21_1.dlr")) && recent.at(2).endsWith(QLatin1String(".cap")),
              QByteArray("all three on Open Recent, newest first: ") + recent.join(QStringLiteral(", ")).toUtf8());

        // The menu: one row per file, a missing one greyed out, and Clear.
        RecentFiles::add(missing);
        w.rebuildRecentMenu();
        QList<QAction *> rows;
        for (QAction *act : menu->actions())
            if (!act->isSeparator() && act->data().isValid()) rows << act;
        CHECK(rows.size() == 4, "four files listed");
        CHECK(rows.first()->text().contains(QStringLiteral("gone.cap")) && rows.first()->text().contains(QStringLiteral("(missing)"))
                  && !rows.first()->isEnabled(),
              "a file deleted since: \"(missing)\", greyed out");
        CHECK(rows.at(1)->isEnabled() && rows.at(1)->text().startsWith(QLatin1String("&2")), "the rest numbered &1..&0, enabled");
        CHECK(menu->actions().last()->text() == QLatin1String("Clear list"), "Clear list at the bottom");

        // Choosing a row opens it.
        const int sessionsBefore = w.findChildren<SessionWindow *>().size();
        rows.at(2)->trigger();   // 21_1.dlr
        CHECK(w.findChildren<SessionWindow *>().size() == sessionsBefore + 1, "choosing a row opens it");

        menu->actions().last()->trigger();
        CHECK(RecentFiles::list().isEmpty() && menu->actions().size() == 1, "Clear list empties it");

        // Dropped on the window.
        CHECK(w.acceptDrops(), "the window takes drops");
        const int tbBefore = w.findChildren<TagBuilderWindow *>().size();
        CHECK(!dropOn(&w, { txt }), "a drag of only a .txt is refused at the door");
        CHECK(dropOn(&w, { route, txt }), "a drag holding a route is taken");
        CHECK(w.findChildren<TagBuilderWindow *>().size() == tbBefore + 1, "a dropped route opens; the .txt beside it does not");
        CHECK(RecentFiles::list().size() == 1, "the dropped route on Open Recent");

        for (ReplayWindow *r : replayWindows()) delete r;
    }
    if (!catalogWasLoaded) FieldCatalog::instance().clear();

    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);
}
