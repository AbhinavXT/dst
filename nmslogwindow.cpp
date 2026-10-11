#include "nmslogwindow.h"

#include "frozencolumns.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"
#include "xlsxbook.h"

#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QBrush>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <algorithm>
#include <QTableView>
#include <QVBoxLayout>

// --- the table's model -------------------------------------------------------

void NmsLogTableModel::reset(const NmsLog::Table &t)
{
    beginResetModel();
    m_table = t;
    endResetModel();
}

int NmsLogTableModel::append(NmsLog::Kind kind, const QVector<LogEntryPtr> &entries)
{
    // Decoded into a scratch table with the same columns first: a packet with
    // a new column changes the column count, which the view must hear as a
    // reset, not as rows. Only the batch is decoded twice over, never the log.
    NmsLog::Table batch;
    batch.columns = m_table.columns;
    batch.groups = m_table.groups;
    batch.order = m_table.order;
    batch.index = m_table.index;
    batch.carry = m_table.carry;   // "changed" and the gaps carry on from the table
    for (const LogEntryPtr &e : entries) {
        if (e) NmsLog::append(batch, kind, *e);
    }
    const int added = batch.rows.size();
    if (added == 0) return 0;
    const bool widened = batch.columns.size() != m_table.columns.size();
    const int first = m_table.rows.size();
    if (widened) beginResetModel();
    else beginInsertRows(QModelIndex(), first, first + added - 1);
    m_table.columns = batch.columns;
    m_table.groups = batch.groups;
    m_table.order = batch.order;
    m_table.index = batch.index;
    m_table.rows += batch.rows;
    m_table.hex += batch.hex;
    m_table.epochMs += batch.epochMs;
    m_table.changed += batch.changed;
    m_table.gapColumn += batch.gapColumn;
    m_table.gapNote += batch.gapNote;
    m_table.gaps += batch.gaps;
    m_table.carry = batch.carry;
    if (widened) endResetModel();
    else endInsertRows();
    return added;
}

int NmsLogTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_table.rows.size();
}

int NmsLogTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_table.columnCount();
}

QVariant NmsLogTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    if (role == Qt::DisplayRole) return m_table.cell(index.row(), index.column());
    if (role != Qt::ToolTipRole && role != Qt::ForegroundRole && role != Qt::FontRole) return QVariant();
    // Session 250: a cell that reports a failure or an event is marked.
    const QString text = m_table.cell(index.row(), index.column());
    const QString column = m_table.header(index.column());
    QString why;
    NmsLog::Mark m = NmsLog::markOf(column, text, &why);
    // Session 252: packets missing before this one, on the cell that shows it.
    if (m == NmsLog::Mark::None && m_table.gapColumn.value(index.row()) == column) {
        m = NmsLog::Mark::Warn;
        why = m_table.gapNote.at(index.row());
    }
    if (role == Qt::ToolTipRole) return why.isEmpty() ? text : text + QStringLiteral("\n") + why;
    if (m == NmsLog::Mark::None) return QVariant();
    if (role == Qt::ForegroundRole) return QBrush(m == NmsLog::Mark::Fail ? UiColor::error() : UiColor::warning());
    QFont f;
    f.setBold(true);
    return f;
}

QVariant NmsLogTableModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (role != Qt::DisplayRole) return QVariant();
    if (o == Qt::Vertical) return section + 1;
    return m_table.header(section);
}

// --- which rows are shown ----------------------------------------------------

NmsLogFilter::NmsLogFilter(NmsLogTableModel *rows, QObject *parent)
    : QSortFilterProxyModel(parent), m_rows(rows)
{
    setSourceModel(rows);
}

void NmsLogFilter::setChangesOnly(bool on)
{
    if (on == m_changesOnly) return;
    m_changesOnly = on;
    invalidateFilter();
}

void NmsLogFilter::setOnlyRows(const QSet<int> &rows)
{
    m_onlyListed = true;
    m_only = rows;
    invalidateFilter();
}

bool NmsLogFilter::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    if (m_onlyListed) return m_only.contains(sourceRow);
    if (m_changesOnly && !m_rows->table().changed.value(sourceRow, true)) return false;
    return true;
}

// --- the window --------------------------------------------------------------

NmsLogWindow::NmsLogWindow(NmsLog::Kind kind, LogModel *model, const QString &tabKey, const QString &tabName,
                           QWidget *parent)
    : QWidget(parent, Qt::Window), m_kind(kind), m_model(model), m_tabKey(tabKey)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("nmsLogWindow_") + NmsLog::token(kind));
    setWindowTitle(tr("%1 — %2").arg(NmsLog::title(kind), tabName));
    WindowGeometry::makeResizableWindow(this);
    resize(1200, 600);

    m_rows = new NmsLogTableModel(this);
    m_filter = new NmsLogFilter(m_rows, this);
    m_view = new QTableView(this);
    m_view->setModel(m_filter);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->horizontalHeader()->setDefaultSectionSize(120);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 6);
    m_view->setToolTip(tr("One row per @%1 packet, as decoded. Bold cells: a failure (error colour) or an "
                          "event (warning colour) the packet reported, or packets missing before it (on the rtc or "
                          "message_seq); hover one for what. "
                          "Double-click a row: the frame in its tab")
                           .arg(NmsLog::token(kind)));

    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->verticalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_frozen = new FrozenColumns(m_view);
    m_frozen->overlay()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_frozen->overlay()->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);

    // Session 253: pinned rows, above the table, in its columns.
    m_pinFilter = new NmsLogFilter(m_rows, this);
    m_pinFilter->setOnlyRows({});
    m_pinned = new QTableView(this);
    m_pinned->setObjectName(QStringLiteral("nmsPinned"));
    m_pinned->setModel(m_pinFilter);
    m_pinned->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pinned->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pinned->horizontalHeader()->hide();
    m_pinned->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pinned->verticalHeader()->setDefaultSectionSize(m_view->verticalHeader()->defaultSectionSize());
    m_pinned->setContextMenuPolicy(Qt::CustomContextMenu);
    m_pinned->verticalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_pinned->setToolTip(tr("Pinned rows: they stay here whatever the table below scrolls to or shows. "
                            "Right-click to unpin"));
    m_pinned->hide();
    m_pinFrozen = new FrozenColumns(m_pinned);
    m_pinFrozen->overlay()->setContextMenuPolicy(Qt::CustomContextMenu);

    m_follow = new QCheckBox(tr("Follow new packets"), this);
    m_follow->setObjectName(QStringLiteral("nmsFollow"));
    m_follow->setChecked(true);
    m_follow->setToolTip(tr("Scroll to each new packet as it arrives"));
    m_changesOnly = new QCheckBox(tr("Changes only"), this);
    m_changesOnly->setObjectName(QStringLiteral("nmsChangesOnly"));
    m_changesOnly->setToolTip(tr("Show only the packets where an event reads differently from the last time it "
                                 "was reported (and any whose CRC failed). The first packet is always shown"));
    m_count = new QLabel(this);
    m_status = new StatusLine;
    auto *csv = new QPushButton(tr("Export CSV…"), this);
    auto *xlsx = new QPushButton(tr("Export Excel (.xlsx)…"), this);
    csv->setObjectName(QStringLiteral("nmsExportCsv"));
    xlsx->setObjectName(QStringLiteral("nmsExportXlsx"));

    auto *top = new QHBoxLayout;
    top->addWidget(m_count, 1);
    top->addWidget(m_changesOnly);
    top->addWidget(m_follow);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_status, 1);
    bottom->addWidget(csv);
    bottom->addWidget(xlsx);
    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addWidget(m_pinned);
    root->addWidget(m_view, 1);
    root->addLayout(bottom);

    connect(csv, &QPushButton::clicked, this, [this]() { exportTo(false); });
    connect(xlsx, &QPushButton::clicked, this, [this]() { exportTo(true); });
    connect(m_view, &QTableView::doubleClicked, this, [this](const QModelIndex &i) {
        const int r = tableRow(i);
        if (r >= 0) emit jumpRequested(m_tabKey, m_rows->table().epochMs.at(r));
    });
    connect(m_pinned, &QTableView::doubleClicked, m_view, &QTableView::doubleClicked);
    // Right-click: freeze columns, pin rows (session 253).
    auto menuOn = [this](QWidget *w, QTableView *v, bool header) {
        connect(w, &QWidget::customContextMenuRequested, this, [this, w, v, header](const QPoint &p) {
            contextMenuAt(v, v->viewport()->mapFromGlobal(w->mapToGlobal(p)), header);
        });
    };
    menuOn(m_view, m_view, false);
    menuOn(m_view->horizontalHeader(), m_view, true);
    menuOn(m_view->verticalHeader(), m_view, false);
    menuOn(m_frozen->overlay(), m_frozen->overlay(), false);
    menuOn(m_frozen->overlay()->horizontalHeader(), m_frozen->overlay(), true);
    menuOn(m_pinned, m_pinned, false);
    menuOn(m_pinned->verticalHeader(), m_pinned, false);
    menuOn(m_pinFrozen->overlay(), m_pinFrozen->overlay(), false);
    // The strip follows the table: column widths, sideways scroll, header width.
    connect(m_view->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int col, int, int size) {
        m_pinned->setColumnWidth(col, size);
    });
    connect(m_view->horizontalScrollBar(), &QAbstractSlider::valueChanged,
            m_pinned->horizontalScrollBar(), &QAbstractSlider::setValue);
    connect(m_view->horizontalScrollBar(), &QAbstractSlider::rangeChanged, this, [this]() { syncPinned(); });
    connect(m_view->verticalHeader(), &QHeaderView::geometriesChanged, this, [this]() { syncPinned(); });
    connect(m_filter, &QAbstractItemModel::modelReset, this, [this]() { syncPinned(); }, Qt::QueuedConnection);
    connect(m_changesOnly, &QCheckBox::toggled, this, [this](bool on) {
        m_filter->setChangesOnly(on);
        if (m_follow->isChecked()) m_view->scrollToBottom();
        showCount();
    });

    if (m_model) {
        connect(m_model, &QAbstractItemModel::rowsInserted, this,
                [this](const QModelIndex &, int first, int last) { onRowsInserted(first, last); });
        connect(m_model, &QAbstractItemModel::modelReset, this, &NmsLogWindow::rebuild);
        connect(m_model, &QObject::destroyed, this, [this]() {
            m_status->warn(tr("The tab was closed: no more packets will be added."));
        });
    }
    rebuild();
}

void NmsLogWindow::rebuild()
{
    m_pins.clear();                     // the rows they named are gone
    m_pinFilter->setOnlyRows(m_pins);
    m_rows->reset(NmsLog::Table());
    if (m_model) {
        QVector<LogEntryPtr> all;
        all.reserve(m_model->count());
        for (int i = 0; i < m_model->count(); ++i) all << m_model->entryAt(i);
        m_rows->append(m_kind, all);
    }
    m_view->resizeColumnsToContents();
    if (m_follow->isChecked()) m_view->scrollToBottom();
    showCount();
    syncPinned();
    if (m_rows->table().rows.isEmpty())
        m_status->state(tr("No @%1 packet in this tab yet; new ones are added as they arrive.")
                            .arg(NmsLog::token(m_kind)));
    else
        m_status->state(QString());
}

void NmsLogWindow::onRowsInserted(int first, int last)
{
    if (!m_model) return;
    QVector<LogEntryPtr> batch;
    for (int i = first; i <= last; ++i) batch << m_model->entryAt(i);
    const int columnsBefore = m_rows->columnCount();
    const int rowsBefore = m_rows->rowCount();
    if (m_rows->append(m_kind, batch) == 0) return;
    if (m_rows->columnCount() != columnsBefore) m_view->resizeColumnsToContents();
    else widenToFit(rowsBefore, m_rows->rowCount() - 1);
    if (m_follow->isChecked()) m_view->scrollToBottom();
    showCount();
}

// Session 249: a row that arrives later can be wider than the columns were
// sized for at open ("2026-06-26 …"). Widen to fit; never narrow, since
// the operator may have widened a column by hand.
void NmsLogWindow::widenToFit(int firstRow, int lastRow)
{
    const int grid = m_view->showGrid() ? 1 : 0;
    for (int c = 0; c < m_rows->columnCount(); ++c) {
        if (m_view->isColumnHidden(c)) continue;
        int want = 0;
        for (int r = firstRow; r <= lastRow; ++r) {
            const QModelIndex shown = m_filter->mapFromSource(m_rows->index(r, c));
            if (shown.isValid()) want = qMax(want, m_view->sizeHintForIndex(shown).width() + grid);
        }
        if (want > m_view->columnWidth(c)) m_view->setColumnWidth(c, want);
    }
}

void NmsLogWindow::showCount()
{
    const int n = m_rows->table().rows.size();
    QString text = n == 1 ? tr("1 packet") : tr("%1 packets").arg(n);
    if (m_filter->rowCount() != n) text += tr(", %1 shown").arg(m_filter->rowCount());
    const int gaps = m_rows->table().gaps;
    if (gaps > 0) text += gaps == 1 ? tr("; 1 gap") : tr("; %1 gaps").arg(gaps);
    m_count->setText(text);
}

int NmsLogWindow::tableRow(const QModelIndex &viewIndex) const
{
    if (!viewIndex.isValid()) return -1;
    const auto *proxy = qobject_cast<const QSortFilterProxyModel *>(viewIndex.model());
    const int r = proxy ? proxy->mapToSource(viewIndex).row() : viewIndex.row();
    return r >= 0 && r < m_rows->table().rows.size() ? r : -1;
}

bool NmsLogWindow::saveCsv(const QString &path, QString *err) const
{
    QFile f(path);
    // UTF-8 with a byte-order mark: Excel opens it as UTF-8 then ("—", "°C").
    if (f.open(QIODevice::WriteOnly)
        && f.write("\xEF\xBB\xBF") == 3
        && f.write(NmsLog::toCsv(m_rows->table()).toUtf8()) >= 0) {
        return true;
    }
    if (err) *err = f.errorString();
    return false;
}

bool NmsLogWindow::saveXlsx(const QString &path, QString *err) const
{
    return XlsxBook::writeFile(path, NmsLog::toSheets(m_rows->table(), m_kind), err);
}

void NmsLogWindow::exportTo(bool xlsx)
{
    if (m_rows->table().rows.isEmpty()) {
        m_status->warn(tr("Nothing to export yet."));
        return;
    }
    const QString suggested = NmsLog::fileStem(m_kind) + QLatin1Char('_') + m_tabKey
                              + (xlsx ? QStringLiteral(".xlsx") : QStringLiteral(".csv"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export %1").arg(NmsLog::title(m_kind)), suggested,
                                                      xlsx ? tr("Excel workbook (*.xlsx)") : tr("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QString err;
    const bool ok = xlsx ? saveXlsx(path, &err) : saveCsv(path, &err);
    if (ok) m_status->ok(tr("Saved %1 packets to %2").arg(m_rows->table().rows.size()).arg(path));
    else m_status->fail(tr("Could not write %1: %2").arg(path, err));
}

// --- session 253: frozen columns, pinned rows ----------------------------------

void NmsLogWindow::freezeColumns(int n)
{
    m_frozen->setCount(n);
    m_pinFrozen->setCount(n);
    m_status->state(n > 0 ? tr("Columns up to “%1” frozen").arg(m_rows->table().header(n - 1)) : QString());
}

int NmsLogWindow::frozenColumns() const
{
    return m_frozen->count();
}

void NmsLogWindow::setPinned(int tableRow, bool pinned)
{
    if (tableRow < 0 || tableRow >= m_rows->table().rows.size()) return;
    if (pinned) m_pins.insert(tableRow);
    else m_pins.remove(tableRow);
    m_pinFilter->setOnlyRows(m_pins);
    syncPinned();
}

QList<int> NmsLogWindow::pinnedRows() const
{
    QList<int> out = m_pins.values();
    std::sort(out.begin(), out.end());
    return out;
}

void NmsLogWindow::syncPinned()
{
    const int n = m_pinFilter->rowCount();
    m_pinned->setVisible(n > 0);
    if (n == 0) return;
    // The same columns, widths and row-number width, so the strip's cells sit
    // over the table's; the same right edge whether or not the table has a
    // vertical scroll bar.
    for (int c = 0; c < m_filter->columnCount(); ++c) {
        m_pinned->setColumnHidden(c, m_view->isColumnHidden(c));
        m_pinned->setColumnWidth(c, m_view->columnWidth(c));
    }
    m_pinned->verticalHeader()->setFixedWidth(m_view->verticalHeader()->width());
    m_pinned->setVerticalScrollBarPolicy(m_view->verticalScrollBar()->isVisible() || n > 6 ? Qt::ScrollBarAlwaysOn
                                                                                           : Qt::ScrollBarAlwaysOff);
    const int rowH = m_pinned->verticalHeader()->defaultSectionSize();
    m_pinned->setFixedHeight(qMin(n, 6) * rowH + 2 * m_pinned->frameWidth());
    m_pinned->horizontalScrollBar()->setRange(m_view->horizontalScrollBar()->minimum(),
                                              m_view->horizontalScrollBar()->maximum());
    m_pinned->horizontalScrollBar()->setValue(m_view->horizontalScrollBar()->value());
}

void NmsLogWindow::contextMenuAt(QTableView *v, const QPoint &pos, bool header)
{
    const bool inStrip = v == m_pinned || v == m_pinFrozen->overlay();
    const QModelIndex at = header ? QModelIndex() : v->indexAt(pos);
    const int col = header ? v->horizontalHeader()->logicalIndexAt(pos.x()) : at.column();
    const int row = tableRow(at);
    QMenu menu(this);
    if (row >= 0) {
        const bool pinned = m_pins.contains(row);
        QAction *pin = menu.addAction(pinned ? tr("Unpin row %1").arg(row + 1) : tr("Pin row %1 at the top").arg(row + 1));
        pin->setObjectName(QStringLiteral("nmsPinRow"));
        connect(pin, &QAction::triggered, this, [this, row, pinned]() { setPinned(row, !pinned); });
    }
    if (!m_pins.isEmpty() && inStrip) {
        QAction *all = menu.addAction(tr("Unpin all"));
        connect(all, &QAction::triggered, this, [this]() {
            m_pins.clear();
            m_pinFilter->setOnlyRows(m_pins);
            syncPinned();
        });
    }
    if (!menu.isEmpty()) menu.addSeparator();
    if (col >= 0 && col < m_rows->table().columnCount() - 1) {
        QAction *freeze = menu.addAction(tr("Freeze columns up to “%1”").arg(m_rows->table().header(col)));
        freeze->setObjectName(QStringLiteral("nmsFreeze"));
        connect(freeze, &QAction::triggered, this, [this, col]() { freezeColumns(col + 1); });
    }
    if (frozenColumns() > 0) {
        QAction *unfreeze = menu.addAction(tr("Unfreeze columns"));
        connect(unfreeze, &QAction::triggered, this, [this]() { freezeColumns(0); });
    }
    if (!menu.isEmpty()) menu.exec(v->viewport()->mapToGlobal(pos));
}
