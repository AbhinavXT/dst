#include "testutil.h"
#include "layoutaudit.h"

#include "comparewindow.h"
#include "logmodel.h"
#include "mergedwindow.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QFile>
#include <QScrollBar>
#include <QTableView>

// =============================================================================
//  Session 154 — Compare panes hide Source and Name (Abhinav's decision,
//  2026-10-03, after the review pass showed it).
//
//  Each pane shows one source, named in its picker, so those two columns
//  held one value per pane. In two panes at 1100 px they took ~200 px and
//  left Message ~35 px, scrolling sideways. The rule test_comparetools
//  pinned ("hidden columns are the setting's business, not the pane's")
//  now has that one exception; the log tabs and the Merged window, which
//  mixes sources, keep both columns. Real traffic:
//  replay/loco_1_1_27062026_140226.cap.
// =============================================================================

TEST_SUITE(session154)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher disp;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: real traffic");
        qint64 ms = 1782558147000LL;
        int n = 0;
        while (!f.atEnd() && n < 400) {
            const QByteArray l = f.readLine().trimmed();
            if (!l.startsWith('@')) continue;
            disp.ingestLocal(n % 2 ? 21 : 81, 1, l, ms + n * 137, QString());
            ++n;
        }
        disp.drainNow();
    }

    // ---- Compare, two panes at 1100 px ------------------------------------------------
    {
        CompareWindow w(&disp, nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(1100, 720);
        w.show();
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
        QList<QTableView *> panes;
        for (QTableView *v : w.findChildren<QTableView *>())
            if (v->isVisible() && v->model() && v->model()->columnCount() == LogModel::ColumnCount) panes << v;
        CHECK(panes.size() == 2, "fixture: two panes");
        for (QTableView *v : panes) {
            CHECK(v->isColumnHidden(LogModel::ColSource) && v->isColumnHidden(LogModel::ColFriendly),
                  "a pane hides Source and Name (one value per pane, named in its picker)");
            CHECK(!v->isColumnHidden(LogModel::ColMessage) && !v->isColumnHidden(LogModel::ColTime),
                  "and keeps Time and Message");
            CHECK(v->columnWidth(LogModel::ColMessage) >= 200,
                  QByteArray("Message has room (was ~35 px; now ") + QByteArray::number(v->columnWidth(LogModel::ColMessage)) + ")");
            CHECK(!v->horizontalScrollBar()->isVisible(), "and the pane does not scroll sideways");
        }
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), QByteArray("Compare: layout audit (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
    }

    // ---- Merged keeps Source: it mixes sources -----------------------------------------
    {
        NameMap names;
        MergedWindow w(&disp, &names, Theme::Light, 5000, nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        QTableView *v = w.findChild<QTableView *>();
        CHECK(v && !v->isColumnHidden(LogModel::ColSource), "the Merged window still shows Source");
    }
}
