#include "mergedwindow.h"
#include "windowgeometry.h"

#include "emptystate.h"
#include "filterbar.h"
#include "logtableview.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "uicolors.h"

#include <QCloseEvent>
#include <QCheckBox>
#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QTableView>
#include <QVBoxLayout>

MergedWindow::MergedWindow(MessageDispatcher *dispatcher,
                           const NameMap     *names,
                           Theme              theme,
                           int                capacity,
                           QWidget           *parent)
    : QMainWindow(parent)
    , m_dispatcher(dispatcher)
    , m_names(names)
{
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("All sources — chronological"));
    WindowGeometry::makeResizableWindow(this);
    resize(1200, 720);
    // Default above; a remembered size/position wins over it.
    WindowGeometry::restore(this, QStringLiteral("mergedWindow"));

    m_model = new LogModel(this, capacity);
    m_model->setNameMap(m_names);
    m_model->setTheme(theme);

    m_filterBar = new FilterBar(m_model);
    m_filterBar->setNameMap(m_names);

    m_view = new QTableView;
    m_view->setObjectName(QStringLiteral("mergedView"));
    m_view->setModel(m_filterBar->proxyModel());
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Session 130: set up like every other log table (logtableview). Until
    // now this table had its own pixel widths and its own density reading,
    // so it ignored the operator's hidden columns and stored widths, drew
    // the Time column in a proportional font, and cut "Time (local)".
    LogTableView::configure(m_view);

    // An empty table is "nothing has arrived yet" or "the filter hides it
    // all"; the source model tells them apart, as in the log tabs.
    LogModel *model = m_model;
    EmptyState::attach(m_view, [model]() -> QString {
        if (model->count() == 0)
            return QObject::tr("Waiting for traffic.\nEvery source's messages appear here, in time order.");
        return QObject::tr("No rows match the filter.\n%1 message(s) are hidden by it.").arg(model->count());
    });

    m_follow = new QCheckBox(tr("Follow new messages"));
    m_follow->setChecked(true);

    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(m_filterBar);
    layout->addWidget(m_view, 1);

    auto *central = new QWidget;
    central->setLayout(layout);
    setCentralWidget(central);

    m_count = new QLabel;
    m_count->setObjectName(QStringLiteral("mergedCount"));
    statusBar()->addWidget(m_count);
    // How to get from here to the source's own tab, said once, quietly; a
    // tooltip on the table would cover the rows' own tooltips.
    auto *hint = new QLabel(tr("Double-click a row to open it in its source's tab"));
    hint->setObjectName(QStringLiteral("mergedHint"));
    hint->setStyleSheet(UiColor::mutedStyle());
    hint->setContentsMargins(12, 0, 0, 0);
    statusBar()->addWidget(hint, 1);
    statusBar()->addPermanentWidget(m_follow);

    if (m_dispatcher) {
        connect(m_dispatcher, &MessageDispatcher::allEntriesAppended,
                this,         &MergedWindow::onAllEntries);
    }
    connect(m_view, &QTableView::doubleClicked,
            this,   &MergedWindow::onRowActivated);

    primeFromExisting();
}

void MergedWindow::primeFromExisting()
{
    if (!m_dispatcher || !m_model) return;

    // Pull what the tabs already hold and re-interleave it. Each model is
    // internally time-ordered, so a single stable sort of the union is
    // correct; this runs once, at open, on at most (tabs x capacity) rows.
    QVector<LogEntryPtr> all;
    const QStringList keys = m_dispatcher->knownKeys();
    for (const QString &key : keys) {
        LogModel *m = m_dispatcher->modelForKey(key);
        if (!m) continue;
        const int n = m->count();
        for (int i = 0; i < n; ++i) {
            LogEntryPtr e = m->entryAt(i);
            if (e) all.append(e);
        }
    }
    if (all.isEmpty()) { onAllEntries({}); return; }

    std::stable_sort(all.begin(), all.end(),
                     [](const LogEntryPtr &a, const LogEntryPtr &b) {
                         return a->epochMs < b->epochMs;
                     });
    m_model->appendEntries(all);
    onAllEntries({});          // refresh the count label
}

void MergedWindow::onAllEntries(QVector<LogEntryPtr> entries)
{
    if (!entries.isEmpty()) {
        // Follow-mode decision is taken BEFORE the insert. Afterwards the
        // scrollbar maximum has already moved, so "was the user at the
        // bottom?" can no longer be answered.
        const bool atBottom =
            m_view->verticalScrollBar()->value()
            >= m_view->verticalScrollBar()->maximum() - 4;

        m_model->appendEntries(entries);

        if (m_follow->isChecked() && atBottom) {
            m_view->scrollToBottom();
        }
    }

    const int sources = m_dispatcher ? m_dispatcher->knownKeys().size() : 0;
    m_count->setText(tr("%1 messages from %2 %3")
                         .arg(m_model->count())
                         .arg(sources)
                         .arg(sources == 1 ? tr("source") : tr("sources")));
}

void MergedWindow::onRowActivated()
{
    const QModelIndex cur = m_view->currentIndex();
    if (!cur.isValid()) return;

    auto *proxy = qobject_cast<QSortFilterProxyModel*>(m_view->model());
    const QModelIndex src = proxy ? proxy->mapToSource(cur) : cur;

    LogEntryPtr e = m_model->entryAt(src.row());
    if (!e) return;

    // Hand off to MainWindow, which owns the tabs. Double-clicking a row
    // here means "show me this in its own source", which is the natural
    // follow-up once the merged view has told you where to look.
    emit jumpRequested(e->tabKey(), e->epochMs);
}

void MergedWindow::setTheme(Theme t)
{
    if (m_model) m_model->setTheme(t);
}

void MergedWindow::closeEvent(QCloseEvent *event)
{
    // These windows are WA_DeleteOnClose, so this is the last
    // point at which the geometry still exists to be read.
    WindowGeometry::save(this, QStringLiteral("mergedWindow"));
    QMainWindow::closeEvent(event);
}
