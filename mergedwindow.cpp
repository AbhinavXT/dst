#include "mergedwindow.h"
#include "windowgeometry.h"

#include "filterbar.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "settings.h"

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
    m_view->setModel(m_filterBar->proxyModel());
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    // ExtendedSelection: shift-click a span, ctrl-click to add. Copying a
    // block of rows into an incident report is the single most common
    // thing anyone does with this table, and single-selection made it a
    // row-at-a-time job.
    //
    // The raw-bytes and field panels follow the CURRENT row rather than the
    // selection, so they stay meaningful during a multi-row selection —
    // "the bytes of these fifty rows" is not a thing they could show.
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setTextElideMode(Qt::ElideRight);
    m_view->setWordWrap(Settings::wordWrapFor(Settings::rowDensity()));
    m_view->verticalHeader()->setVisible(false);
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_view->verticalHeader()->setDefaultSectionSize(
        Settings::rowHeightFor(Settings::rowDensity()));
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->setColumnWidth(LogModel::ColTime,      130);
    m_view->setColumnWidth(LogModel::ColSource,     70);
    m_view->setColumnWidth(LogModel::ColFriendly,  140);
    m_view->setColumnWidth(LogModel::ColDirection,  45);
    m_view->setColumnWidth(LogModel::ColSeverity,   66);   // fits "✕ ERR"

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
    statusBar()->addWidget(m_count);
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

    m_count->setText(tr("%1 messages from %2 source(s)")
                         .arg(m_model->count())
                         .arg(m_dispatcher ? m_dispatcher->knownKeys().size() : 0));
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
