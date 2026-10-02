#include "testutil.h"
#include "layoutaudit.h"

#include "emptystate.h"
#include "filterbar.h"
#include "logmodel.h"
#include "logtimedelegate.h"
#include "mergedwindow.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLabel>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>

// =============================================================================
//  Session 130 — UI revamp, tool windows 6: the Merged window
//  ("All sources — chronological").
//
//  The table is set up like every other log table (LogTableView::configure):
//  the operator's hidden columns, row density and stored widths, the mono
//  Time column, widths measured to the font. An empty or filtered-out table
//  says which of the two it is. The status bar says how to get to a row's
//  own tab.
//  Real traffic: replay/loco_1_1_27062026_140226.cap, as three sources.
// =============================================================================

TEST_SUITE(session130)
{
    // The hidden-columns setting, as an operator might have it: Dir hidden.
    const QList<int> savedHidden = Settings::hiddenColumns();
    Settings::setHiddenColumns({ LogModel::ColDirection });

    NameMap names;

    // ---- empty: says it is waiting, not a bare grid ------------------------
    {
        MessageDispatcher empty;
        MergedWindow w(&empty, &names, Theme::Dark, 5000);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(1100, 720);
        w.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        auto *view = w.findChild<QTableView *>(QStringLiteral("mergedView"));
        CHECK(view != nullptr, "the merged table is found by name");
        CHECK(view && EmptyState::isShowing(view), "no traffic yet: the table says it is waiting");
    }

    // ---- fed real traffic from three sources --------------------------------
    MessageDispatcher disp;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    int n = 0;
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd() && n < 300) {
            const QByteArray l = f.readLine().trimmed();
            if (!l.startsWith('@')) continue;
            const quint8 src = n % 3 == 0 ? 81 : (n % 3 == 1 ? 21 : 22);
            disp.ingestLocal(src, 1, l, 1782558147000LL + n * 137, QString());
            ++n;
        }
    }
    disp.drainNow();
    CHECK(n == 300 && disp.knownKeys().size() == 3, "fixture: 300 real capture lines from three sources");

    // Opened after the traffic: primed from the tabs, in time order.
    MergedWindow w(&disp, &names, Theme::Dark, 5000);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1100, 720);
    w.show();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    auto *view = w.findChild<QTableView *>(QStringLiteral("mergedView"));
    if (!view) { CHECK(false, "the merged table exists"); Settings::setHiddenColumns(savedHidden); return; }
    QAbstractItemModel *m = view->model();
    CHECK(m->rowCount() == 300, "every source's rows are in the one table");
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(m);
    auto *lm = proxy ? qobject_cast<LogModel *>(proxy->sourceModel()) : nullptr;
    bool ordered = lm && lm->count() == 300;
    for (int r = 1; ordered && r < lm->count(); ++r)
        if (lm->entryAt(r)->epochMs < lm->entryAt(r - 1)->epochMs) ordered = false;
    CHECK(ordered, "in time order");
    CHECK(!EmptyState::isShowing(view), "with rows, no overlay over them");

    // ---- set up like the log tabs --------------------------------------------
    CHECK(view->isColumnHidden(LogModel::ColDirection),
          "a column the operator hid is hidden here too (it was always shown)");
    CHECK(!view->isColumnHidden(LogModel::ColMessage), "Message is never hidden");
    CHECK(view->verticalHeader()->defaultSectionSize() == Settings::rowHeightFor(Settings::rowDensity()),
          "row height follows the density setting");
    CHECK(qobject_cast<LogTimeDelegate *>(view->itemDelegateForColumn(LogModel::ColTime)) != nullptr,
          "the Time column has the log tabs' mono painter");
    QFont hf = view->horizontalHeader()->font();
    hf.setWeight(QFont::DemiBold);
    const int headW = QFontMetrics(hf).horizontalAdvance(
        m->headerData(LogModel::ColTime, Qt::Horizontal).toString());
    CHECK(view->columnWidth(LogModel::ColTime) > headW,
          QByteArray("\"Time (local)\" fits its column (") + QByteArray::number(view->columnWidth(LogModel::ColTime))
              + " px for " + QByteArray::number(headW) + " px of header)");
    CHECK(view->editTriggers() == QAbstractItemView::NoEditTriggers, "cells are not editable");

    // ---- a filter that hides everything says so ------------------------------
    auto *bar = w.findChild<FilterBar *>();
    CHECK(bar != nullptr, "the filter bar is there");
    if (bar) {
        bar->setQuery(QStringLiteral("msg:no_such_text_anywhere"));
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        CHECK(m->rowCount() == 0 && EmptyState::isShowing(view),
              "filtered to nothing: the table says the filter hides the rows");
        bar->setQuery(QString());
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        CHECK(m->rowCount() == 300 && !EmptyState::isShowing(view), "filter cleared: rows, no overlay");
    }

    // ---- the status bar --------------------------------------------------------
    auto *count = w.findChild<QLabel *>(QStringLiteral("mergedCount"));
    CHECK(count && count->text() == QStringLiteral("300 messages from 3 sources"),
          QByteArray("the count reads as English (") + (count ? count->text().toUtf8() : QByteArray("none")) + ")");
    auto *hint = w.findChild<QLabel *>(QStringLiteral("mergedHint"));
    CHECK(hint && hint->isVisible() && hint->text().contains(QLatin1String("Double-click")),
          "the status bar says double-click opens a row in its source's tab");

    // ---- double-click still hands the row to its tab ---------------------------
    {
        QSignalSpy spy(&w, &MergedWindow::jumpRequested);
        view->setCurrentIndex(m->index(1, 0));
        emit view->doubleClicked(m->index(1, 0));
        CHECK(spy.count() == 1 && spy.at(0).at(0).toString() == QStringLiteral("21_1"),
              "double-click asks for the row's own tab (21_1)");
    }

    // ---- live rows still arrive -------------------------------------------------
    disp.ingestLocal(81, 1, QByteArrayLiteral("@dmi_1_1 2026-06-27T14:02:59 99999 AA AA"),
                     1782558147000LL + 400 * 137, QString());
    disp.drainNow();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    CHECK(m->rowCount() == 301, "a new message arrives in the merged table");

    // ---- fits; nothing loose ----------------------------------------------------
    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366,
          QByteArray("fits a 1366 x 768 laptop (minimum ")
              + QByteArray::number(w.minimumSizeHint().width()) + " x "
              + QByteArray::number(w.minimumSizeHint().height()) + ")");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (")
                               + loose.join(QLatin1String(", ")).toUtf8() + ")");

    Settings::setHiddenColumns(savedHidden);
}
