#include "testutil.h"
#include "layoutaudit.h"

#include "filterbar.h"
#include "logmodel.h"
#include "logtableview.h"
#include "settings.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

// =============================================================================
//  Session 152 — the shared filter bar, one row or two.
//
//  On one row the severity/direction chips and the count left the text box
//  ~40 px in any pane under ~960 px (the main window's log tab, the
//  recorded-session window), and their 846-px floor held every such window
//  at least that wide. Now the text box keeps 200 px; past that the chips
//  and the count move to a second row, and the bar's floor is the two-row
//  width. Also: a stored width is no longer applied to Message, the
//  stretched last column; it held until the next resize, so a narrower pane
//  scrolled sideways. Real rows: replay/loco_1_1_27062026_140226.cap.
// =============================================================================

namespace {

void settle()
{
    for (int i = 0; i < 8; ++i) QCoreApplication::processEvents();
}

}  // namespace

TEST_SUITE(session152)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    LogModel model(nullptr, 10000);
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
        QVector<LogEntryPtr> v;
        if (f.open(QIODevice::ReadOnly))
            while (!f.atEnd() && v.size() < 200) {
                const QString l = QString::fromUtf8(f.readLine()).trimmed();
                if (!l.startsWith(QLatin1Char('@'))) continue;
                auto e = LogEntryPtr::create();
                e->text = l;
                e->epochMs = QDateTime::fromString(l.section(QLatin1Char(' '), 1, 1), Qt::ISODate).toMSecsSinceEpoch();
                e->cacheDerived();
                v << e;
            }
        model.appendEntries(v);
    }
    CHECK(model.count() == 200, "fixture: 200 real rows");

    QWidget host;
    auto *lay = new QVBoxLayout(&host);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *bar = new FilterBar(&model);
    auto *view = new QTableView;
    view->setModel(bar->proxyModel());
    LogTableView::configure(view);
    lay->addWidget(bar);
    lay->addWidget(view, 1);

    QLineEdit *edit = bar->findChild<QLineEdit *>();
    QWidget *chips = bar->findChild<QWidget *>(QStringLiteral("filterChips"));
    QLabel *count = nullptr;
    for (QLabel *l : bar->findChildren<QLabel *>())
        if (l->text().startsWith(QLatin1String("Showing"))) count = l;
    CHECK(edit && chips && count, "fixture: the text box, the chips and the count");
    if (!edit || !chips || !count) return;

    // ---- wide: one row, as before --------------------------------------------------
    host.resize(1300, 400);
    host.show();
    settle();
    CHECK(!bar->isTwoRows(), "at 1300 px, one row");
    CHECK(qAbs(chips->mapTo(&host, QPoint()).y() - edit->mapTo(&host, QPoint()).y()) < edit->height(),
          "the chips sit beside the text box");

    // ---- a typical pane: two rows, and a usable text box ---------------------------
    host.resize(790, 400);
    settle();
    CHECK(bar->isTwoRows(), "at 790 px (the main window's log pane), two rows");
    CHECK(edit->width() >= FilterBar::kMinEdit,
          QByteArray("the text box keeps its 200 px (was ~40; now ") + QByteArray::number(edit->width()) + ")");
    CHECK(chips->isVisible() && count->isVisible() && chips->mapTo(&host, QPoint()).y() > edit->mapTo(&host, QPoint()).y() + edit->height() / 2,
          "the chips and the count are on the second row, still there");
    CHECK(count->fontMetrics().horizontalAdvance(count->text()) <= count->width(), "the count is shown whole");

    // ---- the floor ------------------------------------------------------------------
    CHECK(bar->minimumSizeHint().width() <= 650,
          QByteArray("the bar's floor is the two-row width (was 846; now ") + QByteArray::number(bar->minimumSizeHint().width()) + ")");
    host.resize(bar->minimumSizeHint().width(), 400);
    settle();
    CHECK(edit->width() >= FilterBar::kMinEdit - 2, "at its floor the text box still has its 200 px");
    const QStringList loose = LayoutAudit::orphans(&host);
    CHECK(loose.isEmpty(), QByteArray("no stray widget (") + loose.join(QLatin1String(", ")).toUtf8() + ")");

    // ---- and back --------------------------------------------------------------------
    host.resize(1300, 400);
    settle();
    CHECK(!bar->isTwoRows(), "wide again: back to one row");

    // ---- the stored Message width ------------------------------------------------------
    {
        const QList<int> before = Settings::logColumnWidths();
        QList<int> stored;
        for (int c = 0; c < LogModel::ColumnCount; ++c) stored << (c == LogModel::ColMessage ? 900 : 0);
        Settings::setLogColumnWidths(stored);
        QTableView narrow;
        narrow.setModel(bar->proxyModel());
        LogTableView::configure(&narrow);
        narrow.resize(700, 300);
        narrow.show();
        settle();
        CHECK(!narrow.horizontalScrollBar()->isVisible(),
              "a stored 900-px Message width does not make a 700-px table scroll sideways");
        Settings::setLogColumnWidths(before);
    }
}
