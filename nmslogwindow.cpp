#include "nmslogwindow.h"

#include "frozencolumns.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"
#include "xlsxbook.h"

#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QStyle>
#include <QFont>
#include <QBrush>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QApplication>
#include <QTableWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <algorithm>
#include <QTableView>
#include <QVBoxLayout>

// --- the table's model -------------------------------------------------------

void NmsLogTableModel::reset(const NmsLog::Table &t)
{
    m_spread.clear();
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
    batch.faultSpans = m_table.faultSpans;   // open spans are extended in place (session 255)
    batch.rowBase = m_table.rows.size();
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
    m_table.faultSpans = batch.faultSpans;
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
    if (role == Qt::BackgroundRole) {
        // Session 256: a find match, tinted as the log find tints one.
        // The cells that hold every word; in a row that matches only across
        // cells, the cells that hold one of them.
        if (m_findWords.isEmpty()) return QVariant();
        if (!m_findColumn.isEmpty() && m_table.header(index.column()) != m_findColumn) return QVariant();
        const QString text = m_table.cell(index.row(), index.column());
        bool tint = NmsLog::cellMatches(text, m_findWords);
        if (!tint && m_findColumn.isEmpty()) {
            auto it = m_spread.constFind(index.row());
            if (it == m_spread.constEnd()) {
                bool whole = false;
                for (int c = 0; c < m_table.columnCount() && !whole; ++c)
                    whole = NmsLog::cellMatches(m_table.cell(index.row(), c), m_findWords);
                it = m_spread.insert(index.row(), !whole && NmsLog::rowMatches(m_table, index.row(), m_findWords));
            }
            if (it.value())
                for (const QString &w : m_findWords) tint = tint || NmsLog::cellMatches(text, { w });
        }
        if (!tint) return QVariant();
        QColor colour = UiColor::accent();
        colour.setAlpha(qApp->palette().color(QPalette::Base).lightness() < 128 ? 70 : 48);
        return QBrush(colour);
    }
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

void NmsLogTableModel::setFind(const QStringList &words, const QString &column)
{
    if (words == m_findWords && column == m_findColumn) return;
    m_findWords = words;
    m_findColumn = column;
    m_spread.clear();
    if (rowCount() > 0) emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), { Qt::BackgroundRole });
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

void NmsLogFilter::setMatching(const QStringList &words, const QString &column)
{
    if (words == m_words && column == m_column) return;
    m_words = words;
    m_column = column;
    invalidateFilter();
}

void NmsLogFilter::setMarkedOnly(bool on)
{
    if (on == m_markedOnly) return;
    m_markedOnly = on;
    invalidateFilter();
}

bool NmsLogFilter::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    if (m_onlyListed) return m_only.contains(sourceRow);
    const NmsLog::Table &t = m_rows->table();
    if (!m_words.isEmpty()
        && !NmsLog::rowMatches(t, sourceRow, m_words, m_column.isEmpty() ? -1 : t.index.value(m_column, -2)))
        return false;
    if (m_markedOnly && !NmsLog::rowMarked(t, sourceRow)) return false;
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
    m_pinned->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);   // as the table's (FrozenColumns)
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
    m_hideUnchanging = new QCheckBox(tr("Hide unchanging"), this);
    m_hideUnchanging->setObjectName(QStringLiteral("nmsHideUnchanging"));
    m_hideUnchanging->setToolTip(tr("Hide the NMS header and event columns that every packet so far reads the same "
                                    "in. A column comes back as soon as a packet reads differently. Time, rtc, seq, "
                                    "loco / ctrl, frame len and CRC are always shown"));
    auto *columns = new QPushButton(tr("Columns…"), this);
    columns->setObjectName(QStringLiteral("nmsColumns"));
    columns->setToolTip(tr("Show or hide any column"));
    QPushButton *faults = nullptr;
    if (kind == NmsLog::Kind::Fault) {
        faults = new QPushButton(tr("Fault summary…"), this);
        faults->setObjectName(QStringLiteral("nmsFaultSummary"));
        faults->setToolTip(tr("Each fault from the packet that first lists it to the first later packet from the "
                              "same subsystem that no longer does: how long, in how many packets"));
    }
    // Session 256: the find and filter bar.
    m_findEdit = new QLineEdit(this);
    m_findEdit->setObjectName(QStringLiteral("nmsFind"));
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->setPlaceholderText(tr("Find (Ctrl+F): words, e.g. “not ok” or “dmi link”"));
    m_findEdit->setToolTip(tr("Every word must appear, in any case; “_” counts as a space. Enter or F3: next "
                              "match, Shift+F3: previous. Esc: clear"));
    m_findColumn = new QComboBox(this);
    m_findColumn->setObjectName(QStringLiteral("nmsFindColumn"));
    m_findColumn->setToolTip(tr("Where to look: one column, or the whole row (the frame's hex too)"));
    m_findColumn->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_filterRows = new QCheckBox(tr("Only matching rows"), this);
    m_filterRows->setObjectName(QStringLiteral("nmsFilterRows"));
    m_filterRows->setToolTip(tr("Show only the packets that match; off: every packet, the matches tinted"));
    m_markedOnly = new QCheckBox(tr("Marked rows only"), this);
    m_markedOnly->setObjectName(QStringLiteral("nmsMarkedOnly"));
    m_markedOnly->setToolTip(tr("Show only the packets with a bold cell: a failure or an event reported, or "
                                "packets missing before it"));
    auto *prev = new QToolButton(this);
    prev->setText(QStringLiteral("▲"));
    prev->setToolTip(tr("Previous match (Shift+F3)"));
    auto *next = new QToolButton(this);
    next->setText(QStringLiteral("▼"));
    next->setToolTip(tr("Next match (F3, Enter)"));
    m_findCount = new QLabel(this);
    m_findCount->setObjectName(QStringLiteral("nmsFindCount"));
    m_findTimer = new QTimer(this);
    m_findTimer->setSingleShot(true);
    m_findTimer->setInterval(200);
    m_count = new QLabel(this);
    m_status = new StatusLine;
    auto *csv = new QPushButton(tr("Export CSV…"), this);
    auto *xlsx = new QPushButton(tr("Export Excel (.xlsx)…"), this);
    csv->setObjectName(QStringLiteral("nmsExportCsv"));
    xlsx->setObjectName(QStringLiteral("nmsExportXlsx"));

    auto *top = new QHBoxLayout;
    top->addWidget(m_count, 1);
    if (faults) top->addWidget(faults);
    top->addWidget(m_hideUnchanging);
    top->addWidget(columns);
    top->addWidget(m_changesOnly);
    top->addWidget(m_follow);
    auto *findRow = new QHBoxLayout;
    findRow->addWidget(m_findEdit, 2);
    findRow->addWidget(m_findColumn);
    findRow->addWidget(prev);
    findRow->addWidget(next);
    findRow->addWidget(m_findCount, 1);
    findRow->addWidget(m_filterRows);
    findRow->addWidget(m_markedOnly);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_status, 1);
    bottom->addWidget(csv);
    bottom->addWidget(xlsx);
    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addLayout(findRow);
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
    connect(m_hideUnchanging, &QCheckBox::toggled, this, [this]() { applyColumns(); });
    connect(m_findEdit, &QLineEdit::textChanged, m_findTimer, qOverload<>(&QTimer::start));
    connect(m_findTimer, &QTimer::timeout, this, &NmsLogWindow::applyFind);
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this]() {
        if (m_findTimer->isActive()) { m_findTimer->stop(); applyFind(); }
        findNext();
    });
    connect(m_findColumn, qOverload<int>(&QComboBox::activated), this, [this]() { applyFind(); });
    connect(m_filterRows, &QCheckBox::toggled, this, [this]() { applyFind(); });
    connect(m_markedOnly, &QCheckBox::toggled, this, [this](bool on) {
        m_filter->setMarkedOnly(on);
        recountMatches();
        showCount();
    });
    connect(prev, &QToolButton::clicked, this, &NmsLogWindow::findPrevious);
    connect(next, &QToolButton::clicked, this, &NmsLogWindow::findNext);
    auto *findKey = new QShortcut(QKeySequence::Find, this);
    connect(findKey, &QShortcut::activated, this, [this]() { m_findEdit->setFocus(); m_findEdit->selectAll(); });
    auto *nextKey = new QShortcut(QKeySequence::FindNext, this);
    connect(nextKey, &QShortcut::activated, this, &NmsLogWindow::findNext);
    auto *prevKey = new QShortcut(QKeySequence::FindPrevious, this);
    connect(prevKey, &QShortcut::activated, this, &NmsLogWindow::findPrevious);
    auto *escKey = new QShortcut(QKeySequence(Qt::Key_Escape), m_findEdit);
    escKey->setContext(Qt::WidgetShortcut);
    connect(escKey, &QShortcut::activated, m_findEdit, &QLineEdit::clear);
    if (faults) connect(faults, &QPushButton::clicked, this, [this]() {
        QDialog *d = faultSummary();
        d->show();
        d->raise();
    });
    connect(columns, &QPushButton::clicked, this, [this]() {
        QDialog *d = columnChooser();
        d->show();
        d->raise();
    });
    connect(m_changesOnly, &QCheckBox::toggled, this, [this](bool on) {
        m_filter->setChangesOnly(on);
        if (m_follow->isChecked()) m_view->scrollToBottom();
        recountMatches();
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
    m_need.clear();
    m_needChars.clear();
    m_needHex = m_needHexChars = 0;
    if (m_model) {
        QVector<LogEntryPtr> all;
        all.reserve(m_model->count());
        for (int i = 0; i < m_model->count(); ++i) all << m_model->entryAt(i);
        m_rows->append(m_kind, all);
    }
    m_view->resizeColumnsToContents();
    widenToFit(0, m_rows->rowCount() - 1);
    if (m_follow->isChecked()) m_view->scrollToBottom();
    applyColumns();
    fillFindColumns();
    m_current = -1;
    recountMatches();
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
    widenToFit(rowsBefore, m_rows->rowCount() - 1);
    if (m_rows->columnCount() != columnsBefore) fillFindColumns();
    applyColumns();
    if (!m_words.isEmpty()) {
        // The new rows' matches, after the ones already counted.
        for (int r = rowsBefore; r < m_rows->rowCount(); ++r)
            if (m_filter->mapFromSource(m_rows->index(r, 0)).isValid()
                && NmsLog::rowMatches(m_rows->table(), r, m_words, findColumnPos())) m_matches << r;
        showFindCount();
    }
    if (m_follow->isChecked()) m_view->scrollToBottom();
}

// Session 249: a row that arrives later can be wider than the columns were
// sized for at open ("2026-06-26 …"). Widen to fit; never narrow, since
// the operator may have widened a column by hand.
// Session 254: every cell is measured once, as it arrives, shown or not
// (Changes only may show it later), and the widest is kept per column.
// Qt's resize-to-contents measures only the rows near the view, so after a
// reset (a new column) the kept widths are applied again.
void NmsLogWindow::widenToFit(int firstRow, int lastRow)
{
    const NmsLog::Table &t = m_rows->table();
    if (m_need.size() < t.columns.size()) { m_need.resize(t.columns.size()); m_needChars.resize(t.columns.size()); }
    // What the view adds to a text's width (margins, the style sheet's item
    // padding, the grid), read off a real cell rather than guessed.
    const QFontMetrics plain(m_view->font());
    if (m_filter->rowCount() > 0) {
        const QModelIndex probe = m_filter->index(0, 0);
        const QVariant f = probe.data(Qt::FontRole);
        m_pad = m_view->sizeHintForIndex(probe).width() + (m_view->showGrid() ? 1 : 0)
                - (f.isValid() ? QFontMetrics(f.value<QFont>()) : plain).horizontalAdvance(probe.data().toString()) + 1;
    }
    const int pad = m_pad;
    for (int r = firstRow; r <= lastRow && r < t.rows.size(); ++r) {
        const QString hex = t.hex.at(r);
        if (hex.size() + 3 >= m_needHexChars) {
            m_needHex = qMax(m_needHex, plain.horizontalAdvance(hex) + pad);
            m_needHexChars = qMax(m_needHexChars, int(hex.size()));
        }
        for (int c = 0; c < t.order.size(); ++c) {
            const QString text = t.cell(r, c);
            const int id = t.order.at(c);
            // A text three characters shorter than the longest so far is not
            // measured: opening a long log stays cheap.
            if (text.isEmpty() || text.size() + 3 < m_needChars.at(id)) continue;
            const QVariant f = m_rows->data(m_rows->index(r, c), Qt::FontRole);
            const int w = (f.isValid() ? QFontMetrics(f.value<QFont>()) : plain).horizontalAdvance(text) + pad;
            m_need[id] = qMax(m_need.at(id), w);
            m_needChars[id] = qMax(m_needChars.at(id), int(text.size()));
        }
    }
    fitColumns();
}

void NmsLogWindow::fitColumns()
{
    const NmsLog::Table &t = m_rows->table();
    for (int c = 0; c < m_rows->columnCount(); ++c) {
        const int need = c >= t.order.size() ? m_needHex                 // the frame (hex)
                       : t.order.at(c) < m_need.size() ? m_need.at(t.order.at(c)) : 0;
        if (need > m_view->columnWidth(c)) m_view->setColumnWidth(c, need);
    }
}

void NmsLogWindow::showCount()
{
    const int n = m_rows->table().rows.size();
    QString text = n == 1 ? tr("1 packet") : tr("%1 packets").arg(n);
    if (m_filter->rowCount() != n) text += tr(", %1 shown").arg(m_filter->rowCount());
    const int gaps = m_rows->table().gaps;
    if (gaps > 0) text += gaps == 1 ? tr("; 1 gap") : tr("; %1 gaps").arg(gaps);
    int hidden = 0;
    for (int c = 0; c < m_rows->columnCount(); ++c) hidden += m_view->isColumnHidden(c) ? 1 : 0;
    if (hidden > 0) text += tr("; %1 of %2 columns hidden").arg(hidden).arg(m_rows->columnCount());
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
    return saveCsv(path, false, err);
}

bool NmsLogWindow::saveXlsx(const QString &path, QString *err) const
{
    return saveXlsx(path, false, err);
}

bool NmsLogWindow::saveCsv(const QString &path, bool shownOnly, QString *err) const
{
    QFile f(path);
    const QString csv = shownOnly ? NmsLog::toCsv(m_rows->table(), shownRows(), shownColumns())
                                  : NmsLog::toCsv(m_rows->table());
    // UTF-8 with a byte-order mark: Excel opens it as UTF-8 then ("—", "°C").
    if (f.open(QIODevice::WriteOnly)
        && f.write("\xEF\xBB\xBF") == 3
        && f.write(csv.toUtf8()) >= 0) {
        return true;
    }
    if (err) *err = f.errorString();
    return false;
}

bool NmsLogWindow::saveXlsx(const QString &path, bool shownOnly, QString *err) const
{
    return XlsxBook::writeFile(path, shownOnly ? NmsLog::toSheets(m_rows->table(), m_kind, shownRows(), shownColumns())
                                               : NmsLog::toSheets(m_rows->table(), m_kind),
                               err);
}

QVector<int> NmsLogWindow::shownRows() const
{
    QVector<int> out;
    out.reserve(m_filter->rowCount());
    for (int v = 0; v < m_filter->rowCount(); ++v) out << m_filter->mapToSource(m_filter->index(v, 0)).row();
    return out;
}

QVector<int> NmsLogWindow::shownColumns() const
{
    QVector<int> out;
    for (int c = 0; c < m_rows->columnCount(); ++c)
        if (!m_view->isColumnHidden(c)) out << c;
    return out;
}

void NmsLogWindow::exportTo(bool xlsx)
{
    if (m_rows->table().rows.isEmpty()) {
        m_status->warn(tr("Nothing to export yet."));
        return;
    }
    // Session 256: rows or columns hidden: what is shown, or everything?
    const int rowsShown = m_filter->rowCount(), rowsAll = m_rows->rowCount();
    const int colsShown = shownColumns().size(), colsAll = m_rows->columnCount();
    bool shownOnly = false;
    if (rowsShown != rowsAll || colsShown != colsAll) {
        QMessageBox ask(QMessageBox::Question, tr("Export %1").arg(NmsLog::title(m_kind)),
                        tr("The table shows %1 of %2 packets and %3 of %4 columns.").arg(rowsShown).arg(rowsAll)
                            .arg(colsShown).arg(colsAll),
                        QMessageBox::Cancel, this);
        QPushButton *shown = ask.addButton(tr("What is shown"), QMessageBox::AcceptRole);
        QPushButton *all = ask.addButton(tr("Every packet and column"), QMessageBox::AcceptRole);
        ask.setDefaultButton(shown);
        ask.exec();
        if (ask.clickedButton() == shown) shownOnly = true;
        else if (ask.clickedButton() != all) return;
        if (shownOnly && rowsShown == 0) {
            m_status->warn(tr("No packet is shown: nothing to export."));
            return;
        }
    }
    const QString suggested = NmsLog::fileStem(m_kind) + QLatin1Char('_') + m_tabKey
                              + (xlsx ? QStringLiteral(".xlsx") : QStringLiteral(".csv"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export %1").arg(NmsLog::title(m_kind)), suggested,
                                                      xlsx ? tr("Excel workbook (*.xlsx)") : tr("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QString err;
    const bool ok = xlsx ? saveXlsx(path, shownOnly, &err) : saveCsv(path, shownOnly, &err);
    if (ok) m_status->ok(tr("Saved %1 packets to %2").arg(shownOnly ? rowsShown : rowsAll).arg(path));
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

// --- session 254: which columns are shown ------------------------------------

void NmsLogWindow::applyColumns()
{
    const NmsLog::Table &t = m_rows->table();
    const bool hideSame = m_hideUnchanging->isChecked() && t.rows.size() > 1;
    for (int c = 0; c < m_rows->columnCount(); ++c) {
        const bool hide = m_userHidden.contains(t.header(c))
                          || (hideSame && t.groupAt(c) != NmsLog::Group::Lead && t.unchanging(c));
        if (m_view->isColumnHidden(c) != hide) m_view->setColumnHidden(c, hide);
    }
    m_frozen->setCount(m_frozen->count());       // the overlays follow
    m_pinFrozen->setCount(m_pinFrozen->count());
    syncPinned();
    showCount();
    if (m_chooser && m_chooser->isVisible()) fillChooser();
    if (m_faults && m_faults->isVisible()) fillFaultSummary();
}

void NmsLogWindow::setHideUnchanging(bool on)
{
    m_hideUnchanging->setChecked(on);
}

void NmsLogWindow::setColumnShown(const QString &name, bool shown)
{
    if (shown) m_userHidden.remove(name);
    else m_userHidden.insert(name);
    applyColumns();
}

void NmsLogWindow::showAllColumns()
{
    m_userHidden.clear();
    m_hideUnchanging->setChecked(false);
    applyColumns();
}

QStringList NmsLogWindow::hiddenColumns() const
{
    QStringList out;
    for (int c = 0; c < m_rows->columnCount(); ++c)
        if (m_view->isColumnHidden(c)) out << m_rows->table().header(c);
    return out;
}

QDialog *NmsLogWindow::columnChooser()
{
    if (m_chooser) { fillChooser(); return m_chooser; }
    auto *d = new QDialog(this);
    d->setObjectName(QStringLiteral("nmsColumnChooser"));
    d->setWindowTitle(tr("Columns — %1").arg(NmsLog::title(m_kind)));
    auto *note = new QLabel(tr("Tick the columns to show. “same in every packet” marks the ones Hide unchanging "
                               "would hide."), d);
    note->setWordWrap(true);
    m_chooserList = new QListWidget(d);
    m_chooserList->setObjectName(QStringLiteral("nmsColumnList"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, d);
    QPushButton *all = buttons->addButton(tr("Show all"), QDialogButtonBox::ResetRole);
    all->setObjectName(QStringLiteral("nmsShowAll"));
    auto *lay = new QVBoxLayout(d);
    lay->addWidget(note);
    lay->addWidget(m_chooserList, 1);
    lay->addWidget(buttons);
    d->resize(360, 520);
    connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::hide);
    connect(all, &QPushButton::clicked, this, &NmsLogWindow::showAllColumns);
    connect(m_chooserList, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
        const QString name = it->data(Qt::UserRole).toString();
        const bool shown = it->checkState() == Qt::Checked;
        if (shown == !m_userHidden.contains(name)) return;   // filled, not ticked
        setColumnShown(name, shown);
    });
    m_chooser = d;
    fillChooser();
    return d;
}

void NmsLogWindow::fillChooser()
{
    if (!m_chooserList) return;
    const NmsLog::Table &t = m_rows->table();
    QSignalBlocker block(m_chooserList);
    const int scroll = m_chooserList->verticalScrollBar()->value();
    m_chooserList->clear();
    for (int c = 0; c < m_rows->columnCount(); ++c) {
        const QString name = t.header(c);
        const bool same = t.rows.size() > 1 && t.groupAt(c) != NmsLog::Group::Lead && t.unchanging(c);
        auto *it = new QListWidgetItem(same ? tr("%1   (same in every packet)").arg(name) : name, m_chooserList);
        it->setData(Qt::UserRole, name);
        it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        it->setCheckState(m_userHidden.contains(name) ? Qt::Unchecked : Qt::Checked);
    }
    m_chooserList->verticalScrollBar()->setValue(scroll);
}

// --- session 255: the fault summary -------------------------------------------

QDialog *NmsLogWindow::faultSummary()
{
    if (m_faults) { fillFaultSummary(); return m_faults; }
    auto *d = new QDialog(this);
    d->setObjectName(QStringLiteral("nmsFaultSummaryDialog"));
    d->setWindowTitle(tr("Fault summary — %1").arg(windowTitle()));
    m_faultNote = new QLabel(d);
    m_faultNote->setWordWrap(true);
    m_faultTable = new QTableWidget(d);
    m_faultTable->setObjectName(QStringLiteral("nmsFaultSpans"));
    m_faultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_faultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_faultTable->verticalHeader()->hide();
    m_faultTable->setToolTip(tr("A fault is listed from the packet that first lists it until the first later "
                                "packet from the same subsystem that no longer does. Double-click: the packet "
                                "that first listed it"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, d);
    QPushButton *csv = buttons->addButton(tr("Export CSV…"), QDialogButtonBox::ActionRole);
    auto *lay = new QVBoxLayout(d);
    lay->addWidget(m_faultNote);
    lay->addWidget(m_faultTable, 1);
    lay->addWidget(buttons);
    d->resize(1100, 420);
    connect(buttons, &QDialogButtonBox::rejected, d, &QDialog::hide);
    connect(m_faultTable, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        const NmsLog::Table &t = m_rows->table();
        if (row >= 0 && row < t.faultSpans.size()) {
            const int r = t.faultSpans.at(row).fromRow;
            if (r >= 0 && r < t.epochMs.size()) emit jumpRequested(m_tabKey, t.epochMs.at(r));
        }
    });
    connect(csv, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(m_faults, tr("Export the fault summary"),
                                                          QStringLiteral("nms_fault_summary_") + m_tabKey + QStringLiteral(".csv"),
                                                          tr("CSV (*.csv)"));
        if (path.isEmpty()) return;
        QString err;
        if (saveFaultSummaryCsv(path, &err))
            m_status->ok(tr("Saved %1 faults to %2").arg(m_rows->table().faultSpans.size()).arg(path));
        else
            m_status->fail(tr("Could not write %1: %2").arg(path, err));
    });
    m_faults = d;
    fillFaultSummary();
    return d;
}

void NmsLogWindow::fillFaultSummary()
{
    if (!m_faultTable) return;
    const NmsLog::Table &t = m_rows->table();
    const QVector<QStringList> rows = NmsLog::faultSummary(t);
    const QStringList head = NmsLog::faultSummaryHeader();
    m_faultTable->setColumnCount(head.size());
    m_faultTable->setHorizontalHeaderLabels(head);
    m_faultTable->setRowCount(rows.size());
    int open = 0;
    QSet<QString> distinct;
    for (int r = 0; r < rows.size(); ++r) {
        if (t.faultSpans.at(r).toRow < 0) ++open;
        const NmsLog::Table::FaultSpan &sp = t.faultSpans.at(r);
        distinct.insert(sp.subsystem + QLatin1Char('|') + sp.module + QLatin1Char('|') + QString::number(sp.codeType)
                        + QLatin1Char('|') + sp.fault);
        for (int c = 0; c < head.size(); ++c) {
            QTableWidgetItem *it = m_faultTable->item(r, c);
            if (!it) m_faultTable->setItem(r, c, it = new QTableWidgetItem);
            it->setText(rows.at(r).value(c));
        }
    }
    m_faultTable->resizeColumnsToContents();
    m_faultNote->setText(rows.isEmpty() ? tr("No fault listed in this tab yet.")
                                        : tr("%1 distinct fault(s) in %2 packet(s), listed %3 time(s): a fault listed "
                                             "again after a packet no longer listed it is a new line. %4 still listed "
                                             "in the last packet.")
                                              .arg(distinct.size()).arg(t.rows.size()).arg(rows.size()).arg(open));
}

bool NmsLogWindow::saveFaultSummaryCsv(const QString &path, QString *err) const
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly) && f.write("\xEF\xBB\xBF") == 3
        && f.write(NmsLog::faultSummaryCsv(m_rows->table()).toUtf8()) >= 0)
        return true;
    if (err) *err = f.errorString();
    return false;
}

// --- session 256: the find and filter bar -------------------------------------

int NmsLogWindow::findColumnPos() const
{
    const QString col = m_findColumn->currentData().toString();
    return col.isEmpty() ? -1 : m_rows->table().index.value(col, -2);
}

void NmsLogWindow::fillFindColumns()
{
    const QString keep = m_findColumn->currentData().toString();
    QSignalBlocker block(m_findColumn);
    m_findColumn->clear();
    m_findColumn->addItem(tr("All columns"), QString());
    for (int c = 0; c < m_rows->columnCount(); ++c) m_findColumn->addItem(m_rows->table().header(c), m_rows->table().header(c));
    const int at = m_findColumn->findData(keep);
    m_findColumn->setCurrentIndex(at < 0 ? 0 : at);
}

void NmsLogWindow::applyFind()
{
    m_findTimer->stop();
    m_words = NmsLog::findWords(m_findEdit->text());
    const QString column = m_findColumn->currentData().toString();
    m_rows->setFind(m_words, column);
    m_filter->setMatching(m_filterRows->isChecked() ? m_words : QStringList(), column);
    m_current = -1;
    recountMatches();
    showCount();
    if (m_filterRows->isChecked() && m_follow->isChecked()) m_view->scrollToBottom();
}

void NmsLogWindow::recountMatches()
{
    m_matches.clear();
    if (!m_words.isEmpty()) {
        const int col = findColumnPos();
        for (int v = 0; v < m_filter->rowCount(); ++v) {
            const int r = m_filter->mapToSource(m_filter->index(v, 0)).row();
            if (NmsLog::rowMatches(m_rows->table(), r, m_words, col)) m_matches << r;
        }
    }
    if (!m_matches.contains(m_current)) m_current = -1;
    showFindCount();
}

void NmsLogWindow::showFindCount()
{
    if (m_words.isEmpty()) { m_findCount->clear(); return; }
    if (m_matches.isEmpty()) {
        m_findCount->setText(tr("no match"));
        m_findCount->setStyleSheet(UiColor::warningStyle());
        return;
    }
    m_findCount->setStyleSheet(QString());
    const int at = m_matches.indexOf(m_current);
    m_findCount->setText(at >= 0 ? tr("%1 of %2 matching packets").arg(at + 1).arg(m_matches.size())
                                 : tr("%1 matching packets").arg(m_matches.size()));
}

void NmsLogWindow::goToMatch(int step)
{
    if (m_findTimer->isActive()) applyFind();
    if (m_matches.isEmpty()) return;
    const int n = m_matches.size();
    int at = m_matches.indexOf(m_current);
    if (at >= 0) {
        at = (at + step + n) % n;
    } else {
        // No current match: the first after (or before) the selected row.
        const int here = tableRow(m_view->currentIndex());
        if (step > 0) {
            at = 0;
            while (here >= 0 && at < n && m_matches.at(at) <= here) ++at;
            at %= n;
        } else {
            at = n - 1;
            while (here >= 0 && at >= 0 && m_matches.at(at) >= here) --at;
            at = (at + n) % n;
        }
    }
    m_current = m_matches.at(at);
    m_follow->setChecked(false);   // a match being read is not scrolled away
    // The matching cell: the find's column, or the row's first cell with
    // every word (else one of them), so a match far to the right is shown.
    int col = findColumnPos();
    if (col < 0) {
        const NmsLog::Table &t = m_rows->table();
        for (int c = 0; c < t.columnCount() && col < 0; ++c)
            if (!m_view->isColumnHidden(c) && NmsLog::cellMatches(t.cell(m_current, c), m_words)) col = c;
        for (int c = 0; c < t.columnCount() && col < 0; ++c)
            for (const QString &w : m_words)
                if (col < 0 && !m_view->isColumnHidden(c) && NmsLog::cellMatches(t.cell(m_current, c), { w })) col = c;
    }
    const QModelIndex v = m_filter->mapFromSource(m_rows->index(m_current, qMax(0, col)));
    m_view->setCurrentIndex(v);
    m_frozen->scrollTo(v, QAbstractItemView::PositionAtCenter);
    showFindCount();
}

void NmsLogWindow::findNext() { goToMatch(1); }
void NmsLogWindow::findPrevious() { goToMatch(-1); }

void NmsLogWindow::setFindText(const QString &text)
{
    m_findEdit->setText(text);
    applyFind();
}

void NmsLogWindow::setFindColumn(const QString &column)
{
    const int at = m_findColumn->findData(column);
    m_findColumn->setCurrentIndex(at < 0 ? 0 : at);
    applyFind();
}

void NmsLogWindow::setFilterRows(bool on) { m_filterRows->setChecked(on); }
void NmsLogWindow::setMarkedOnly(bool on) { m_markedOnly->setChecked(on); }
