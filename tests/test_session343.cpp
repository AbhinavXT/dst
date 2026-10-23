#include "testutil.h"

#include "bookmarks.h"
#include "fieldcatalog.h"
#include "incidentreport.h"
#include "incidentreportwindow.h"
#include "logmodel.h"
#include "mainwindow.h"
#include "messagedispatcher.h"

#include <QApplication>

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 343 — notes on log rows in the incident pack. A bookmark's note
//  (Bookmarks dock ▸ Edit note…, or the row menu's new Add note…) inside the
//  report's window, on the report's tab, is listed under "Operator notes",
//  in the operator's own words, with the time and the row. Real run:
//  replay/loco_1_1_27062026_151052.cap (the rear-end, session 133).
// =============================================================================

TEST_SUITE(session343)
{
    MessageDispatcher disp;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_151052.cap"));
    CHECK(f.open(QIODevice::ReadOnly), "fixture: the capture");
    int n = 0;
    while (!f.atEnd() && n < 3000) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
        ++n;
    }
    disp.drainNow();
    LogModel *model = disp.modelForKey(QStringLiteral("21_1"));
    CHECK(model && model->count() > 1000, "fixture: the tab's rows");
    if (!model) return;

    const qint64 at = model->entryAt(model->count() / 2)->epochMs;
    QTemporaryDir tmp;
    BookmarkStore marks;
    marks.load(tmp.filePath(QStringLiteral("bookmarks.json")));
    // The store keeps its own order: a note goes on by tab and time.
    auto mark = [&marks](const char *tab, qint64 ms, const QString &snippet, const QString &note) {
        marks.toggle(QLatin1String(tab), ms, snippet);
        for (int i = 0; i < marks.count(); ++i)
            if (marks.all().at(i).tabKey == QLatin1String(tab) && marks.all().at(i).epochMs == ms) marks.setNote(i, note);
    };
    mark("21_1", at - 20'000, QStringLiteral("@dmi_1_1 …"), QStringLiteral("EB here <loco pilot says so>"));
    mark("21_1", at + 5'000, QStringLiteral("@arp_1_1 …"), QString());   // no note: still listed
    mark("21_1", at - 10 * 60'000, QStringLiteral("far before"), QStringLiteral("outside the window"));
    mark("21_2", at, QStringLiteral("other tab"), QStringLiteral("another tab"));

    IncidentReport::Options o;
    IncidentReport::Summary s = IncidentReport::build(model, QStringLiteral("21_1"), QStringLiteral("Loco 1"), at, o);
    IncidentReport::addNotes(&s, marks.all());
    CHECK(s.notes.size() == 2 && s.notes.at(0).ms == at - 20'000 && s.notes.at(1).ms == at + 5'000,
          "the two bookmarks in the window, on this tab, in time order");
    CHECK(s.notes.at(0).note == QStringLiteral("EB here <loco pilot says so>") && s.notes.at(1).note.isEmpty(),
          "with their notes, a bookmark without one too");

    const QString html = IncidentReport::toHtml(s, o);
    CHECK(html.contains(QStringLiteral("<h2>Operator notes: 2</h2>")), "the report has an Operator notes section");
    CHECK(html.contains(QStringLiteral("EB here &lt;loco pilot says so&gt;")) && !html.contains(QStringLiteral("outside the window"))
              && !html.contains(QStringLiteral("another tab")),
          "the note, escaped; nothing from outside the window or another tab");
    CHECK(html.indexOf(QStringLiteral("Operator notes")) < html.indexOf(QStringLiteral("DMI at key moments")),
          "before the DMI moments: the operator's own words come first");

    IncidentReport::Summary none = IncidentReport::build(model, QStringLiteral("21_1"), QString(), at, o);
    IncidentReport::addNotes(&none, {});
    CHECK(!IncidentReport::toHtml(none, o).contains(QStringLiteral("Operator notes")), "no bookmarks: no section");

    // ---- Add note… on a row (setRowNote), and the report window --------------------
    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    [&]() {
        MainWindow w;
        auto *wd = w.findChild<MessageDispatcher *>();
        CHECK(wd, "fixture: the window's dispatcher");
        if (!wd) return;
        QFile g(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_151052.cap"));
        g.open(QIODevice::ReadOnly);
        int k = 0;
        while (!g.atEnd() && k < 400) {
            const QByteArray l = g.readLine().trimmed();
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3 || !l.startsWith('@')) continue;
            wd->ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
            ++k;
        }
        wd->drainNow();
        QApplication::processEvents();
        LogModel *m = wd->modelForKey(QStringLiteral("21_1"));
        CHECK(m && m->count() == 400, "fixture: 400 rows in the window's tab");
        if (!m) return;
        BookmarkStore &store = w.bookmarks();
        const QVector<Bookmark> before = store.all();
        const LogEntryPtr e = m->entryAt(200);
        w.setRowNote(QStringLiteral("21_1"), e, QStringLiteral("brake felt late"));
        CHECK(e->bookmarked && store.has(QStringLiteral("21_1"), e->epochMs), "Add note on an unmarked row bookmarks it");
        w.setRowNote(QStringLiteral("21_1"), e, QStringLiteral("brake felt late, twice"));
        int mine = 0;
        QString note;
        for (const Bookmark &b : store.all())
            if (b.tabKey == QLatin1String("21_1") && b.epochMs == e->epochMs) { ++mine; note = b.note; }
        CHECK(mine == 1 && note == QStringLiteral("brake felt late, twice"), "again: the same bookmark, its note replaced");

        IncidentReportWindow rw(m, QStringLiteral("21_1"), QStringLiteral("Loco 1"), e->epochMs, IncidentReport::Options());
        CHECK(rw.summary().notes.isEmpty(), "a report window not given the bookmarks lists no notes");
        rw.setBookmarks(&store);
        CHECK(rw.summary().notes.size() == 1 && rw.summary().notes.first().note == QStringLiteral("brake felt late, twice")
                  && rw.html().contains(QStringLiteral("brake felt late, twice")),
              "given them (as Tools ▸ Incident report does): the note is in the pack");
        store.replaceAll(before);   // leave the test INI's bookmarks as found
    }();
    if (!catalogWasLoaded) FieldCatalog::instance().clear();
}
