#include "testutil.h"

#include "sessionwindow.h"
#include "sessionfile.h"
#include "colorrules.h"
#include "namemap.h"
#include "theme.h"
#include "logentry.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>

// =============================================================================
//  Tools in the recorded-session window.
//
//  The archive is where the careful work happens — the live window is
//  watched, a recording is read. Until now, decoding one frame out of a
//  capture meant re-opening it in the live console, which is both the wrong
//  way round and the dangerous way round: the live models are wired to the
//  disk writer, so anything loaded into them is re-recorded.
//
//  These checks are about WHICH tools are offered, because that is the part
//  that carries a judgement. Field Sweep and Packet Sequence transmit
//  repeatedly against live equipment, and a window whose whole premise is
//  "this is yesterday" is the last place to start one by accident.
// =============================================================================

namespace {

QString writeSession(const QString &dir, quint8 src, quint16 kv,
                     qint64 startMs, int n)
{
    QDir().mkpath(dir);
    const QString path = QStringLiteral("%1/%2_%3.dlr").arg(dir).arg(src).arg(kv);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { return QString(); }

    SessionFile::FileHeader h;
    h.createdMs = startMs;
    h.sourceId  = src;
    h.kvchId    = kv;
    f.write(SessionFile::encodeHeader(h));

    for (int i = 0; i < n; ++i) {
        const QByteArray body = "RAD IN Link 1 No Error " + QByteArray::number(i);
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

QMenu *menuNamed(QMenuBar *bar, const QString &name)
{
    for (QAction *a : bar->actions()) {
        if (a->menu() && a->text().remove(QLatin1Char('&')) == name) { return a->menu(); }
    }
    return nullptr;
}

QAction *actionContaining(QMenu *m, const QString &needle)
{
    if (!m) { return nullptr; }
    for (QAction *a : m->actions()) {
        if (a->isSeparator()) { continue; }
        if (a->text().remove(QLatin1Char('&')).contains(needle, Qt::CaseInsensitive)) {
            return a;
        }
    }
    return nullptr;
}

}  // namespace

TEST_SUITE(sessiontools)
{
    ColorRules rules;
    NameMap    names;
    SessionWindow win(&rules, &names, Theme::Light);

    QMenu *tools = menuNamed(win.menuBar(), QStringLiteral("Tools"));
    CHECK(tools != nullptr, "the session window has a Tools menu");

    // ---- what is offered ---------------------------------------------------
    CHECK(actionContaining(tools, QStringLiteral("Decode Workbench")),
          "Decode Workbench");
    CHECK(actionContaining(tools, QStringLiteral("Selected row")),
          "and taking the selected row to it");
    CHECK(actionContaining(tools, QStringLiteral("Frame Diff")), "Frame Diff");
    CHECK(actionContaining(tools, QStringLiteral("Plot field")),
          "Plot field over time");
    CHECK(actionContaining(tools, QStringLiteral("Packet Maker")), "Packet Maker");

    // ---- what is deliberately not ------------------------------------------
    CHECK(!actionContaining(tools, QStringLiteral("Field Sweep")),
          "Field Sweep is NOT offered — it transmits repeatedly, and this "
          "window is a recording");
    CHECK(!actionContaining(tools, QStringLiteral("Packet Sequence")),
          "nor is Packet Sequence, for the same reason");
    CHECK(!actionContaining(tools, QStringLiteral("Round-trip")),
          "nor the round-trip validator, which already reads .dlr directly");

    // ---- the one that can transmit is set apart ----------------------------
    {
        int sepBefore = 0;
        bool seenPm = false;
        for (QAction *a : tools->actions()) {
            if (a->isSeparator()) { ++sepBefore; continue; }
            if (a->text().contains(QLatin1String("Packet Ma"))) { seenPm = true; break; }
        }
        CHECK(seenPm, "the Packet Maker entry is there");
        CHECK(sepBefore >= 1,
              "and sits below a separator: it is the only entry here that "
              "leads somewhere that can send");
    }

    // ---- shortcuts do not collide inside this window -----------------------
    {
        QStringList seen, clashes;
        for (QAction *a : tools->actions()) {
            const QString sc = a->shortcut().toString();
            if (sc.isEmpty()) { continue; }
            if (seen.contains(sc)) { clashes << sc; } else { seen << sc; }
        }
        CHECK(clashes.isEmpty(), "no two tools here share a shortcut");
        CHECK(seen.contains(QLatin1String("Ctrl+Shift+D")),
              "and the ones it shares with the live window keep their keys, so "
              "the same fingers work in both");
    }

    // ---- it still loads a session ------------------------------------------
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid(), "temp dir");
        const qint64 t0 = QDateTime(QDate(2026, 8, 5), QTime(9, 0)).toMSecsSinceEpoch();
        const QString path = writeSession(tmp.path(), 33, 1, t0, 12);
        CHECK(!path.isEmpty(), "a session file was written");

        SessionWindow loader(&rules, &names, Theme::Light);
        const qint64 n = loader.loadFiles({ path });
        CHECK(n == 12, "every record loads");
        CHECK(menuNamed(loader.menuBar(), QStringLiteral("Tools")) != nullptr,
              "and the menu is there whether or not a file was readable");
    }
}

// =============================================================================
//  Ctrl+F in a recording.
//
//  Reported from use: the recorded-session window had no find. It is the
//  window where the careful work happens — a live log is watched, a recording
//  is read — and it had every filtering tool except the one for "where does
//  this word appear".
// =============================================================================

#include "findbar.h"
#include <QTableView>

TEST_SUITE(sessionfind)
{
    ColorRules rules;
    NameMap    names;

    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir");
    const qint64 t0 = QDateTime(QDate(2026, 8, 5), QTime(9, 0)).toMSecsSinceEpoch();
    const QString path = writeSession(tmp.path(), 33, 1, t0, 10);
    CHECK(!path.isEmpty(), "a session file was written");

    SessionWindow win(&rules, &names, Theme::Light);
    CHECK(win.loadFiles({ path }) == 10, "and loaded");

    // The menu entry, because a shortcut nobody can find in a menu is a
    // shortcut only its author knows about.
    QMenu *edit = menuNamed(win.menuBar(), QStringLiteral("Edit"));
    CHECK(edit != nullptr, "the session window has an Edit menu");
    QAction *find = actionContaining(edit, QStringLiteral("Find"));
    CHECK(find != nullptr, "with Find in it");
    CHECK(find && find->shortcut() == QKeySequence::Find,
          "on Ctrl+F, the same key as the live window");

    // And a real find bar behind it, attached to the tab's view.
    const QList<FindBar *> bars = win.findChildren<FindBar *>();
    CHECK(bars.size() == 1, "one find bar for the one loaded source");
}

// =============================================================================
//  Navigation in a recording: F4, bookmarks, Ctrl+G.
//
//  All three walk the same proxy rows the live window walks, through the same
//  free functions — nextMarkedRow, nextBookmarkedRow, GotoTimestamp::resolveRow
//  — so the two windows cannot answer differently about where the next error
//  is or which row a timestamp lands on. That sharing is the point of these
//  checks: a second implementation would look right and drift.
// =============================================================================

#include "bookmarks.h"
#include "markerscrollbar.h"
#include "logmodel.h"
#include "gototimestampdialog.h"
#include <QSortFilterProxyModel>

TEST_SUITE(sessionnavigation)
{
    ColorRules rules;
    NameMap    names;

    QTemporaryDir tmp;
    const qint64 t0 = QDateTime(QDate(2026, 8, 5), QTime(9, 0)).toMSecsSinceEpoch();
    const QString path = writeSession(tmp.path(), 33, 1, t0, 20);
    CHECK(!path.isEmpty(), "a session file was written");

    SessionWindow win(&rules, &names, Theme::Light);
    BookmarkStore store;
    win.setBookmarkStore(&store);
    CHECK(win.loadFiles({ path }) == 20, "and loaded");

    QMenu *edit = menuNamed(win.menuBar(), QStringLiteral("Edit"));
    CHECK(edit != nullptr, "there is an Edit menu");

    // ---- the keys match the live window -------------------------------------
    {
        QAction *goTo = actionContaining(edit, QStringLiteral("Go to timestamp"));
        CHECK(goTo && goTo->shortcut() == QKeySequence("Ctrl+G"), "Ctrl+G goes to a time");

        QAction *nextProb = actionContaining(edit, QStringLiteral("Next problem"));
        QAction *prevProb = actionContaining(edit, QStringLiteral("Previous problem"));
        CHECK(nextProb && nextProb->shortcut() == QKeySequence("F4"),
              "F4 steps to the next error or warning");
        CHECK(prevProb && prevProb->shortcut() == QKeySequence("Shift+F4"),
              "and Shift+F4 back");

        QAction *bmAction = actionContaining(edit, QStringLiteral("Bookmarks"));
        QMenu *bm = bmAction ? bmAction->menu() : nullptr;
        CHECK(bm != nullptr, "bookmarks have their own submenu, as in the live window");
        if (bm) {
            QAction *tog  = actionContaining(bm, QStringLiteral("Toggle"));
            QAction *next = actionContaining(bm, QStringLiteral("Next"));
            QAction *prev = actionContaining(bm, QStringLiteral("Previous"));
            CHECK(tog  && tog->shortcut()  == QKeySequence("Ctrl+B"),   "Ctrl+B toggles");
            CHECK(next && next->shortcut() == QKeySequence("F2"),       "F2 forward");
            CHECK(prev && prev->shortcut() == QKeySequence("Shift+F2"), "Shift+F2 back");
        }
    }

    // ---- a bookmark set here is the same bookmark ---------------------------
    // Keyed by (tabKey, epochMs), so the row marked while reviewing a
    // recording is the row marked in the live log — anything else would be a
    // surprise nobody asked for.
    {
        const QString key = QStringLiteral("33_1");
        CHECK(!store.has(key, t0), "nothing marked yet");
        const bool added = store.toggle(key, t0, QStringLiteral("row"));
        CHECK(added && store.has(key, t0), "marking one records it");
        CHECK(!store.toggle(key, t0, QStringLiteral("row")),
              "and marking it again takes it off");
    }

    // ---- the shared walks, on a model ---------------------------------------
    // Exercised directly: these are the functions both windows call, and a
    // check here covers the session window's use of them without driving a
    // menu action that would open a modal dialog.
    {
        LogModel m(nullptr, 100);
        QVector<LogEntryPtr> v;
        for (int i = 0; i < 10; ++i) {
            auto e = QSharedPointer<LogEntry>::create();
            e->header.source_id = 33; e->header.kvchId = 1;
            e->epochMs = t0 + i * 1000;
            e->severity = (i == 6) ? Severity::Error : Severity::Info;
            e->bookmarked = (i == 3);
            e->text = QStringLiteral("row %1").arg(i);
            e->cacheDerived();
            v << e;
        }
        m.appendEntries(v);

        QSortFilterProxyModel proxy;
        proxy.setSourceModel(&m);

        CHECK(nextMarkedRow(&proxy, &m, -1, +1) == 6, "F4 finds the one error");
        CHECK(nextBookmarkedRow(&proxy, &m, -1, +1) == 3, "F2 finds the one bookmark");
        CHECK(nextBookmarkedRow(&proxy, &m, 3, +1) == -1, "and nothing after it");

        const GotoTimestamp::Span span = GotoTimestamp::spanOf(&proxy, &m);
        CHECK(span.ok && span.minMs == t0 && span.maxMs == t0 + 9000,
              "the span is the range actually present");
        CHECK(GotoTimestamp::resolveRow(&proxy, &m, t0 + 4500,
                  int(GotoTimestampDialog::Mode::AtOrAfter)) == 5,
              "at-or-after lands on the first row past the instant");
        CHECK(GotoTimestamp::resolveRow(&proxy, &m, t0 + 4500,
                  int(GotoTimestampDialog::Mode::AtOrBefore)) == 4,
              "at-or-before lands on the last row before it");
        CHECK(GotoTimestamp::resolveRow(&proxy, &m, t0 - 999999,
                  int(GotoTimestampDialog::Mode::AtOrBefore)) == 0,
              "and a target outside the span falls back to nearest rather than "
              "doing nothing visible");
    }
}
