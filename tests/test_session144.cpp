#include "testutil.h"
#include "layoutaudit.h"

#include "colorrules.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QFile>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLabel>
#include <QStatusBar>
#include <QTabBar>
#include <QTableView>
#include <QTemporaryDir>

// =============================================================================
//  Session 144 — UI revamp, tool windows 21: the recorded-session window.
//
//  Its table is set up the way the live log's is (LogTableView::configure):
//  the pixel widths it had clipped "16:32:27.1..." and "ime (local". The
//  side panels follow the row the cursor is on even when the table does not
//  have focus (Go to timestamp, Next problem and the find bar left them on
//  the previous frame). Decoded fields in front, dock tabs not elided, a
//  default width that does not grow on the first click, and counts in
//  English. Real frames: replay/loco_1_1_27062026_140226.cap, written to
//  .dlr the way Save writes them.
// =============================================================================

namespace {

QString writeDlr(const QString &path, LogModel *m, quint8 src)
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

}  // namespace

TEST_SUITE(session144)
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
    const QStringList paths = { writeDlr(tmp.path() + QStringLiteral("/21_1.dlr"), disp.modelForKey(QStringLiteral("21_1")), 21),
                                writeDlr(tmp.path() + QStringLiteral("/81_1.dlr"), disp.modelForKey(QStringLiteral("81_1")), 81) };
    CHECK(!paths.at(0).isEmpty() && !paths.at(1).isEmpty(), "fixture: two .dlr files written");

    ColorRules rules;
    NameMap names;
    SessionWindow w(&rules, &names, Theme::Light);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(w.loadFiles(paths) == 400, "every record loads");
    w.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

    QTableView *view = nullptr;
    for (QTableView *tv : w.findChildren<QTableView *>())
        if (tv->isVisible() && tv->model() && tv->model()->rowCount() == 200) view = tv;
    CHECK(view != nullptr, "the first tab's table is showing, 200 rows");
    if (!view) return;

    // ---- the live log's table setup ---------------------------------------
    {
        const QFontMetrics mono(UiStyle::monoFont());
        CHECK(view->columnWidth(LogModel::ColTime) >= mono.horizontalAdvance(QStringLiteral("88:88:88.888")) + 16,
              "the Time column fits a whole time (was 100 px: \"16:32:27.1...\")");
        QFont hf = view->horizontalHeader()->font();
        hf.setWeight(QFont::DemiBold);
        const QFontMetrics head(hf);
        CHECK(view->columnWidth(LogModel::ColSource) >= head.horizontalAdvance(QStringLiteral("Source")) + 16,
              "the Source column fits its header (was 60 px: \"Sourc\")");
        CHECK(view->itemDelegateForColumn(LogModel::ColTime) != nullptr,
              "the Time column has the live log's painter");
        CHECK(view->editTriggers() == QAbstractItemView::NoEditTriggers, "still read-only");
        CHECK(view->selectionMode() == QAbstractItemView::ExtendedSelection, "still multi-row selection");
    }

    // ---- the panels follow the cursor, focus or not ------------------------
    QLabel *rawHeader = nullptr;
    for (QLabel *l : w.findChildren<QLabel *>())
        if (l->text() == QLatin1String("(no row selected)")) rawHeader = l;
    CHECK(rawHeader != nullptr, "the raw panel starts with no row");
    const int minBefore = w.minimumSizeHint().width();
    view->clearFocus();
    w.setFocus();
    view->setCurrentIndex(view->model()->index(5, LogModel::ColMessage));
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
    CHECK(!view->hasFocus(), "fixture: the table does not have focus");
    CHECK(rawHeader && rawHeader->text().contains(QLatin1String("source_id")),
          "moving the cursor without focus still shows that frame (Go to timestamp, Next problem, Find)");

    // ---- the docks --------------------------------------------------------
    {
        QStringList elided;
        int bars = 0;
        for (QTabBar *bar : w.findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (!bar->isVisible()) continue;
            ++bars;
            CHECK(bar->count() == 2 && bar->tabText(bar->currentIndex()) == QLatin1String("Decoded fields"),
                  "Decoded fields is in front, as in the live window; Raw bytes one tab away");
            const QFontMetrics fm(bar->font());
            for (int i = 0; i < bar->count(); ++i)
                if (bar->tabRect(i).width() < fm.horizontalAdvance(bar->tabText(i)) + 10) elided << bar->tabText(i);
        }
        CHECK(bars == 1, "one dock tab bar");
        CHECK(elided.isEmpty(), QByteArray("dock tabs are not elided (") + elided.join(QLatin1String(", ")).toUtf8() + ")");
    }

    // ---- counts in English ------------------------------------------------
    {
        QString status;
        for (QLabel *l : w.statusBar()->findChildren<QLabel *>())
            if (l->text().contains(QLatin1String("records"))) status = l->text();
        CHECK(status == QLatin1String("400 records from 2 files"),
              QByteArray("the status reads \"400 records from 2 files\" (") + status.toUtf8() + ")");
    }

    // ---- fits, and does not grow on the first click -----------------------
    CHECK(w.minimumSizeHint().width() == minBefore,
          QByteArray("showing a frame does not widen the window (") + QByteArray::number(minBefore) + " -> "
              + QByteArray::number(w.minimumSizeHint().width()) + "; it grew on the first click)");
    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366, "fits a laptop");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
