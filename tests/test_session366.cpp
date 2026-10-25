#include "testutil.h"

#include "bookmarks.h"
#include "colorrules.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "notessidecar.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QTableView>
#include <QTemporaryDir>

// =============================================================================
//  Session 366 — notes beside the capture: `x.notes.json` next to `x.dlr`, so
//  a recording sent from the test site carries the site's notes. Real frames:
//  replay/loco_1_1_27062026_151052.cap (the rear-end capture of sessions 133
//  and 343), 300 lines, written to a .dlr the way Save writes it.
// =============================================================================

namespace {

QString writeDlr366(const QString &path, LogModel *m)
{
    QFile f(path);
    if (!m || !f.open(QIODevice::WriteOnly)) return QString();
    SessionFile::FileHeader h;
    h.createdMs = 1782566000000LL;
    h.sourceId = 21;
    h.kvchId = 1;
    f.write(SessionFile::encodeHeader(h));
    for (int r = 0; r < m->rowCount(); ++r) {
        const LogEntryPtr e = m->entryAt(r);
        if (e && !e->rawBytes.isEmpty()) f.write(SessionFile::encodeRecord(e->epochMs, e->rawBytes));
    }
    return path;
}

Bookmark mark366(const char *tab, qint64 ms, const char *note)
{
    Bookmark b;
    b.tabKey = QLatin1String(tab);
    b.epochMs = ms;
    b.note = QString::fromUtf8(note);
    b.messageSnippet = QStringLiteral("@slrp …");
    b.createdMs = 1782566000000LL;
    return b;
}

QString noteIn(const BookmarkStore &s, const QString &tab, qint64 ms)
{
    for (const Bookmark &b : s.all())
        if (b.tabKey == tab && b.epochMs == ms) return b.note;
    return QStringLiteral("<none>");
}

QAction *menuAction(SessionWindow &w, const QString &text)
{
    for (QAction *m : w.menuBar()->actions())
        if (m->menu())
            for (QAction *a : m->menu()->actions()) {
                if (a->text() == text) return a;
                if (a->menu())
                    for (QAction *b : a->menu()->actions())
                        if (b->text() == text) return b;
            }
    return nullptr;
}

}  // namespace

TEST_SUITE(session366)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    QTemporaryDir tmp;

    // ---- the file itself ---------------------------------------------------------------------
    CHECK(NotesSidecar::pathFor(QStringLiteral("/a/b/Loco_1_25102026_101010.dlr"))
                  == QLatin1String("/a/b/Loco_1_25102026_101010.notes.json")
              && NotesSidecar::pathFor(QStringLiteral("/a/b/Loco_1_25102026_101010.log"))
                     == QLatin1String("/a/b/Loco_1_25102026_101010.notes.json"),
          "x.dlr and x.log share x.notes.json");
    const QString dlrA = tmp.filePath(QStringLiteral("a.dlr"));
    const QVector<Bookmark> all = { mark366("21_1", 3000, "late"), mark366("21_1", 1000, "first <b>"),
                                    mark366("21_2", 2000, "other tab"), mark366("21_1", 9000, "outside") };
    const QVector<Bookmark> in = NotesSidecar::marksIn(all, QStringLiteral("21_1"), 1000, 5000);
    CHECK(in.size() == 2 && in.at(0).epochMs == 1000 && in.at(1).epochMs == 3000,
          "the tab's marks inside the span, in time order");
    QString err;
    CHECK(NotesSidecar::write(dlrA, in, &err) && QFile::exists(tmp.filePath(QStringLiteral("a.notes.json"))),
          "written beside the capture: " + err.toUtf8());
    {
        QFile f(tmp.filePath(QStringLiteral("a.notes.json")));
        f.open(QIODevice::ReadOnly);
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        CHECK(root.value(QStringLiteral("format")).toString() == QLatin1String("dlconsole-notes")
                  && root.value(QStringLiteral("capture")).toString() == QLatin1String("a.dlr"),
              "it names its format and its capture");
    }
    const QVector<Bookmark> back = NotesSidecar::read(dlrA, &err);
    CHECK(back.size() == 2 && back.at(0).note == QLatin1String("first <b>") && back.at(1).epochMs == 3000
              && back.at(1).tabKey == QLatin1String("21_1") && back.at(0).messageSnippet == QStringLiteral("@slrp …"),
          "read back: tab, time, note and row text");
    CHECK(NotesSidecar::read(tmp.filePath(QStringLiteral("none.dlr")), &err).isEmpty(), "no sidecar: no marks");
    {
        QFile junk(tmp.filePath(QStringLiteral("j.notes.json")));
        junk.open(QIODevice::WriteOnly);
        junk.write("[1, 2]");
    }
    err.clear();
    CHECK(NotesSidecar::read(tmp.filePath(QStringLiteral("j.dlr")), &err).isEmpty() && err.contains(QLatin1String("not a notes file")),
          "another JSON there: refused, with why");
    CHECK(NotesSidecar::write(dlrA, {}, &err) && !QFile::exists(tmp.filePath(QStringLiteral("a.notes.json"))),
          "no marks left: the sidecar is removed, not left stale");

    // ---- merging into a store ----------------------------------------------------------------
    {
        BookmarkStore s;
        s.load(tmp.filePath(QStringLiteral("merge.json")));
        s.toggle(QStringLiteral("21_1"), 1000, QStringLiteral("x"));      // marked, no note
        s.toggle(QStringLiteral("21_1"), 3000, QStringLiteral("x"));
        s.setNote(1, QStringLiteral("mine"));                              // marked, another note
        const NotesSidecar::Merge m = NotesSidecar::mergeInto(&s, { mark366("21_1", 1000, "site"), mark366("21_1", 3000, "site's"),
                                                                    mark366("21_1", 5000, "new"), mark366("21_1", 7000, "") });
        CHECK(m.added == 2 && m.filled == 1 && m.differ == 1, "2 added, 1 filled in, 1 differs");
        CHECK(noteIn(s, QStringLiteral("21_1"), 1000) == QLatin1String("site")
                  && noteIn(s, QStringLiteral("21_1"), 3000) == QLatin1String("mine")
                  && noteIn(s, QStringLiteral("21_1"), 5000) == QLatin1String("new")
                  && noteIn(s, QStringLiteral("21_1"), 7000).isEmpty() && s.count() == 4,
          "the reader's own note is never overwritten");
        const NotesSidecar::Merge again = NotesSidecar::mergeInto(&s, { mark366("21_1", 5000, "new") });
        CHECK(again.added == 0 && again.filled == 0 && again.differ == 0, "merging the same file twice changes nothing");
    }

    // ---- a recording with its sidecar, in the viewer -----------------------------------------
    MessageDispatcher disp;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_151052.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: the capture opens");
        int n = 0;
        while (!f.atEnd() && n < 300) {
            const QByteArray l = f.readLine().trimmed();
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3 || !l.startsWith('@')) continue;
            const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
            disp.ingestLocal(21, 1, l, ms, QString());
            ++n;
        }
        disp.drainNow();
    }
    LogModel *src = disp.modelForKey(QStringLiteral("21_1"));
    CHECK(src && src->count() == 300, "fixture: 300 rows");
    if (!src) return;
    const QString dlr = writeDlr366(tmp.filePath(QStringLiteral("Loco_1_27062026_151052.dlr")), src);
    const qint64 t10 = src->entryAt(10)->epochMs, t200 = src->entryAt(200)->epochMs;
    CHECK(NotesSidecar::write(dlr, { mark366("21_1", t10, "site: SLRP stopped here"), mark366("21_1", t200, "") }),
          "the site's sidecar, beside the .dlr");

    ColorRules rules;
    NameMap names;
    BookmarkStore marks;
    marks.load(tmp.filePath(QStringLiteral("reviewer.json")));
    {
        SessionWindow w(&rules, &names, Theme::Light);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.setBookmarkStore(&marks);
        CHECK(w.loadFiles({ dlr }) == 300, "the recording loads");
        CHECK(w.lastMerge().added == 2 && marks.count() == 2
                  && noteIn(marks, QStringLiteral("21_1"), t10) == QLatin1String("site: SLRP stopped here"),
              "the site's notes come in with the recording");
        w.show();
        QCoreApplication::processEvents();
        QTableView *view = nullptr;
        for (QTableView *tv : w.findChildren<QTableView *>())
            if (tv->model() && tv->model()->rowCount() == 300) view = tv;
        CHECK(view != nullptr, "the tab's table");
        if (!view) return;
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
        auto *model = qobject_cast<LogModel *>(proxy->sourceModel());
        // The capture's times are whole seconds: a mark is on its moment, so
        // every row of that second shows it (BookmarkStore, "a moment").
        int other = 11;
        while (other < 200 && model->entryAt(other)->epochMs == t10) ++other;
        CHECK(model->entryAt(10)->bookmarked && model->entryAt(200)->bookmarked && !model->entryAt(other)->bookmarked,
              "their rows are marked, and a row of another second is not");
        CHECK(w.statusBar()->currentMessage().contains(QLatin1String("2 marks")),
              "the status bar says what came in: " + w.statusBar()->currentMessage().toUtf8());

        // A note made in the viewer goes back into the file beside the .dlr.
        CHECK(w.setRowNote(QStringLiteral("21_1"), model->entryAt(50), QStringLiteral("reviewer: why 4 s?")),
              "the reviewer adds a note");
        QVector<Bookmark> side = NotesSidecar::read(dlr);
        CHECK(side.size() == 3 && side.at(1).epochMs == model->entryAt(50)->epochMs
                  && side.at(1).note == QLatin1String("reviewer: why 4 s?"),
              "the sidecar now holds the reviewer's note too, in time order");

        // Ctrl+B off on a mark: gone from the file as well.
        QAction *tog = menuAction(w, QStringLiteral("Toggle &bookmark"));
        CHECK(tog != nullptr, "Edit ▸ Bookmarks ▸ Toggle bookmark");
        if (!tog) return;
        view->setCurrentIndex(proxy->mapFromSource(model->index(200, 0)));
        tog->trigger();
        side = NotesSidecar::read(dlr);
        CHECK(side.size() == 2 && !model->entryAt(200)->bookmarked, "a mark taken off leaves the sidecar too");
    }

    // ---- opened again, with a note of the reader's own that differs ---------------------------
    {
        for (int i = 0; i < marks.count(); ++i)
            if (marks.all().at(i).epochMs == t10) marks.setNote(i, QStringLiteral("reader: mine"));
        NotesSidecar::write(dlr, { mark366("21_1", t10, "site: SLRP stopped here") });
        SessionWindow w(&rules, &names, Theme::Light);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.setBookmarkStore(&marks);
        w.loadFiles({ dlr });
        CHECK(w.lastMerge().differ == 1 && noteIn(marks, QStringLiteral("21_1"), t10) == QLatin1String("reader: mine"),
              "a differing note: the reader's kept, and counted");
        CHECK(w.statusBar()->currentMessage().contains(QLatin1String("differ")),
              "the status bar says so: " + w.statusBar()->currentMessage().toUtf8());
    }
}
