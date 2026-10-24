#include "testutil.h"
#include "layoutaudit.h"

#include "colorrules.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "sessionfile.h"
#include "sessionsummary.h"
#include "sessionwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDockWidget>
#include <QFile>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QTreeWidget>

// =============================================================================
//  Session 349 — the .dlr viewer's Summary dock: per source its span, counts,
//  rate, longest gap, and the quiet spans over N s (double-click to land).
//  Real frames: replay/loco_1_1_27062026_140226.cap, with a 10 s hole cut
//  into source 21 after its 100th record.
// =============================================================================

namespace {

constexpr qint64 kStart = 1782558147000LL;

QString writeDlr349(const QString &path, LogModel *m, quint8 src)
{
    QFile f(path);
    if (!m || !f.open(QIODevice::WriteOnly)) return QString();
    SessionFile::FileHeader h;
    h.createdMs = kStart;
    h.sourceId = src;
    h.kvchId = 1;
    f.write(SessionFile::encodeHeader(h));
    for (int r = 0; r < m->rowCount(); ++r) {
        const LogEntryPtr e = m->entryAt(r);
        if (e && !e->rawBytes.isEmpty()) f.write(SessionFile::encodeRecord(e->epochMs, e->rawBytes));
    }
    return path;
}

QTreeWidgetItem *child(QTreeWidgetItem *p, const QString &name)
{
    for (int i = 0; p && i < p->childCount(); ++i)
        if (p->child(i)->text(0) == name) return p->child(i);
    return nullptr;
}

}  // namespace

TEST_SUITE(session349)
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

    // Even lines to 81, odd to 21 (274 ms apart); 21's 101st record onward 10 s late.
    MessageDispatcher disp;
    int n21 = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const bool is21 = i % 2;
        const qint64 t = kStart + i * 137 + (is21 && n21 >= 100 ? 10000 : 0);
        if (is21) ++n21;
        disp.ingestLocal(is21 ? 21 : 81, 1, lines.at(i).toUtf8(), t, QString());
    }
    disp.drainNow();
    LogModel *m21 = disp.modelForKey(QStringLiteral("21_1"));
    LogModel *m81 = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(m21 && m81 && m21->rowCount() == 200 && m81->rowCount() == 200, "fixture: 200 rows each");
    if (!m21 || !m81) return;

    // ---- the computation ----------------------------------------------------
    const qint64 last21Before = kStart + 199 * 137;   // line 199: 21's 100th record
    const qint64 first21After = kStart + 201 * 137 + 10000;
    {
        const SessionSummary::Source s = SessionSummary::summarize(m21, 5000);
        CHECK(s.records == 200, "21: 200 records");
        CHECK(s.firstMs == kStart + 137 && s.lastMs == kStart + 399 * 137 + 10000, "21: first and last time");
        CHECK(s.gaps.size() == 1 && s.gaps.at(0).fromMs == last21Before && s.gaps.at(0).toMs == first21After,
              "21: one quiet span, from its last row before to its first row after");
        CHECK(s.longest.ms() == 10274, "21: the longest gap is that span, 10.274 s");
        CHECK(SessionSummary::summarize(m21, 20000).gaps.isEmpty(), "over 20 s: none");
        CHECK(s.errors + s.warnings <= s.records, "counts stay within the records");
    }
    {
        const SessionSummary::Source s = SessionSummary::summarize(m81, 5000);
        CHECK(s.gaps.isEmpty() && s.longest.ms() == 274, "81: no quiet span, rows 274 ms apart");
    }
    CHECK(SessionSummary::summarize(nullptr, 5000).records == 0, "no model: empty");

    // ---- the dock ------------------------------------------------------------
    QTemporaryDir tmp;
    const QStringList paths = { writeDlr349(tmp.path() + QStringLiteral("/21_1.dlr"), m21, 21),
                                writeDlr349(tmp.path() + QStringLiteral("/81_1.dlr"), m81, 81) };
    ColorRules rules;
    NameMap names;
    SessionWindow w(&rules, &names, Theme::Light);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(w.loadFiles(paths) == 400, "every record loads");
    w.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

    QDockWidget *dock = nullptr;
    for (QDockWidget *d : w.findChildren<QDockWidget *>())
        if (d->windowTitle() == QLatin1String("Summary")) dock = d;
    CHECK(dock != nullptr, "a Summary dock");
    QTreeWidget *tree = w.findChild<QTreeWidget *>();
    QSpinBox *gapOver = w.findChild<QSpinBox *>();
    CHECK(tree && gapOver && gapOver->value() == 5, "its tree, and \"Quiet spans over 5 s\"");
    if (!tree || !gapOver) return;
    CHECK(tree->topLevelItemCount() == 2, "one entry per source");
    QTreeWidgetItem *src21 = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->text(0).contains(QLatin1String("21"))) src21 = tree->topLevelItem(i);
    CHECK(src21 && src21->text(1) == QLatin1String("200 records"), "21: 200 records");
    QTreeWidgetItem *quiet = child(src21, QStringLiteral("Quiet spans"));
    CHECK(quiet && quiet->text(1) == QLatin1String("1") && quiet->childCount() == 1, "21: one quiet span listed");
    CHECK(child(src21, QStringLiteral("Length")) && child(src21, QStringLiteral("Length"))->text(1) == QLatin1String("1:04"),
          "21: length 64.5 s reads 1:04");
    QTreeWidgetItem *longest = child(src21, QStringLiteral("Longest gap"));
    CHECK(longest && longest->text(1).startsWith(QLatin1String("10.3 s after ")), "21: longest gap 10.3 s");
    if (!quiet || quiet->childCount() != 1) return;
    CHECK(quiet->child(0)->text(1) == QLatin1String("quiet 10.3 s"), "the span reads \"quiet 10.3 s\"");

    // Double-click the span: the 21 tab, its last row before the silence.
    emit tree->itemActivated(quiet->child(0), 0);
    QTableView *view = nullptr;
    for (QTableView *tv : w.findChildren<QTableView *>())
        if (tv->isVisible() && tv->model() && tv->model()->rowCount() == 200) view = tv;
    auto *proxy = view ? qobject_cast<QSortFilterProxyModel *>(view->model()) : nullptr;
    auto *model = proxy ? qobject_cast<LogModel *>(proxy->sourceModel()) : nullptr;
    CHECK(model && view->currentIndex().isValid()
              && model->entryAt(proxy->mapToSource(view->currentIndex()).row())->epochMs == last21Before,
          "double-click lands on the last row before the quiet span");

    gapOver->setValue(20);
    QTreeWidgetItem *src21b = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->text(0).contains(QLatin1String("21"))) src21b = tree->topLevelItem(i);
    QTreeWidgetItem *quiet20 = child(src21b, QStringLiteral("Quiet spans"));
    CHECK(quiet20 && quiet20->text(1) == QLatin1String("0") && quiet20->childCount() == 0, "over 20 s: none listed");

    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366, "still fits a laptop");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
