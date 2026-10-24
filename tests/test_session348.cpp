#include "testutil.h"
#include "layoutaudit.h"

#include "colorrules.h"
#include "filterbar.h"
#include "gototimestampdialog.h"
#include "logmodel.h"
#include "markerscrollbar.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "settings.h"
#include "theme.h"
#include "timelineribbon.h"
#include "uistyle.h"

#include <QAction>
#include <QCoreApplication>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTemporaryDir>

// =============================================================================
//  Session 348 — the recorded-session (.dlr) viewer gets the live window's
//  timeline ribbon, marker scroll bar and Back/Forward. Real frames:
//  replay/loco_1_1_27062026_140226.cap, written to .dlr the way Save does.
// =============================================================================

namespace {

QString writeDlr348(const QString &path, LogModel *m, quint8 src)
{
    QFile f(path);
    if (!m || !f.open(QIODevice::WriteOnly)) return QString();
    SessionFile::FileHeader h;
    h.createdMs = 1782558147000LL;
    h.sourceId = src;
    h.kvchId = 1;
    f.write(SessionFile::encodeHeader(h));
    for (int r = 0; r < m->rowCount(); ++r) {
        const LogEntryPtr e = m->entryAt(r);
        if (e && !e->rawBytes.isEmpty()) f.write(SessionFile::encodeRecord(e->epochMs, e->rawBytes));
    }
    return path;
}

QAction *editAction(SessionWindow &w, const QString &text)
{
    for (QAction *m : w.menuBar()->actions())
        if (m->menu())
            for (QAction *a : m->menu()->actions())
                if (a->text() == text) return a;
    return nullptr;
}

}  // namespace

TEST_SUITE(session348)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    QStringList lines;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
        if (f.open(QIODevice::ReadOnly))
            while (!f.atEnd() && lines.size() < 400) {
                const QString l = QString::fromUtf8(f.readLine()).trimmed();
                if (l.startsWith(QLatin1Char('@'))) lines << l;
            }
    }
    CHECK(lines.size() == 400, "fixture: 400 real lines");

    MessageDispatcher disp;
    for (int i = 0; i < lines.size(); ++i)
        disp.ingestLocal(i % 2 ? 21 : 81, 1, lines.at(i).toUtf8(), 1782558147000LL + i * 137, QString());
    disp.drainNow();
    QTemporaryDir tmp;
    const QStringList paths = { writeDlr348(tmp.path() + QStringLiteral("/21_1.dlr"), disp.modelForKey(QStringLiteral("21_1")), 21),
                                writeDlr348(tmp.path() + QStringLiteral("/81_1.dlr"), disp.modelForKey(QStringLiteral("81_1")), 81) };

    ColorRules rules;
    NameMap names;
    SessionWindow w(&rules, &names, Theme::Light);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(w.loadFiles(paths) == 400, "every record loads");
    w.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

    // ---- every tab: a ribbon over it, marks on its scroll bar -------------
    CHECK(w.findChildren<TimelineRibbon *>().size() == 2, "one timeline ribbon per tab");
    int marked = 0;
    for (QTableView *tv : w.findChildren<QTableView *>())
        if (tv->model() && tv->model()->rowCount() == 200 && qobject_cast<MarkerScrollBar *>(tv->verticalScrollBar()))
            ++marked;
    CHECK(marked == 2, "each table scrolls with the marker scroll bar");

    QTableView *view = nullptr;
    for (QTableView *tv : w.findChildren<QTableView *>())
        if (tv->isVisible() && tv->model() && tv->model()->rowCount() == 200) view = tv;
    CHECK(view != nullptr, "the first tab shows, 200 rows");
    if (!view) return;
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
    auto *model = proxy ? qobject_cast<LogModel *>(proxy->sourceModel()) : nullptr;
    CHECK(model != nullptr, "fixture: the view's LogModel");
    if (!model) return;
    TimelineRibbon *ribbon = nullptr;
    for (TimelineRibbon *r : w.findChildren<TimelineRibbon *>())
        if (r->isVisible()) ribbon = r;
    CHECK(ribbon != nullptr, "the shown tab's ribbon is visible");
    if (!ribbon) return;

    // ---- click the ribbon: the nearest row ---------------------------------
    QAction *back = editAction(w, QStringLiteral("Bac&k"));
    QAction *fwd  = editAction(w, QStringLiteral("For&ward"));
    CHECK(back && fwd && back->shortcut() == QKeySequence("Alt+Left") && fwd->shortcut() == QKeySequence("Alt+Right"),
          "Edit ▸ Back (Alt+Left) and Forward (Alt+Right), as in the live window");
    if (!back || !fwd) return;
    CHECK(!back->isEnabled() && !fwd->isEnabled(), "both off before any jump");

    view->setCurrentIndex(proxy->index(5, LogModel::ColTime));
    const qint64 at5  = model->entryAt(proxy->mapToSource(proxy->index(5, 0)).row())->epochMs;
    const qint64 at50 = model->entryAt(proxy->mapToSource(proxy->index(50, 0)).row())->epochMs;
    emit ribbon->timeClicked(at50 + 3);   // between rows: lands on the nearest
    CHECK(view->currentIndex().row() == 50, "a click on the ribbon lands on the nearest row");
    CHECK(back->isEnabled() && !fwd->isEnabled(), "Back is on after the jump");

    // ---- Back and Forward ---------------------------------------------------
    back->trigger();
    CHECK(view->currentIndex().row() == 5 && fwd->isEnabled(), "Back returns to row 5");
    CHECK(model->entryAt(proxy->mapToSource(view->currentIndex()).row())->epochMs == at5, "the same frame");
    fwd->trigger();
    CHECK(view->currentIndex().row() == 50, "Forward goes to row 50 again");
    back->trigger();
    back->trigger();   // nothing further: no move
    CHECK(view->currentIndex().row() == 5 && !back->isEnabled(), "Back past the first jump does nothing");

    // ---- drag the ribbon: a visible after:/before: query --------------------
    const qint64 at100 = model->entryAt(proxy->mapToSource(proxy->index(100, 0)).row())->epochMs;
    emit ribbon->rangeSelected(at50, at100);
    FilterBar *bar = nullptr;
    for (FilterBar *b : w.findChildren<FilterBar *>())
        if (b->isVisible()) bar = b;
    CHECK(bar && bar->filterText() == GotoTimestamp::rangeQuery(at50, at100), "the drag writes the query into the filter box");
    CHECK(proxy->rowCount() > 0 && proxy->rowCount() < 200,
          QByteArray("the table shows that span only (") + QByteArray::number(proxy->rowCount()) + " rows)");

    const bool utc = Settings::showUtc();
    Settings::setShowUtc(true);
    CHECK(GotoTimestamp::rangeQuery(1782558147000LL, 1782558148500LL)
              == QStringLiteral("after:2026-06-27T11:02:27.000Z before:2026-06-27T11:02:28.500Z"),
          "rangeQuery: ISO with ms, UTC marked Z");
    Settings::setShowUtc(utc);

    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366, "still fits a laptop");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
