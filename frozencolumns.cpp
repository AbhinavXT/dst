#include "frozencolumns.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHeaderView>
#include <QScrollBar>
#include <QTableView>

FrozenColumns::FrozenColumns(QTableView *table)
    : QObject(table), m_table(table)
{
    m_overlay = new QTableView(table);
    m_overlay->setObjectName(QStringLiteral("frozenColumns"));
    m_overlay->setModel(table->model());
    m_overlay->setSelectionModel(table->selectionModel());
    m_overlay->setSelectionBehavior(table->selectionBehavior());
    m_overlay->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_overlay->setFocusPolicy(Qt::NoFocus);
    m_overlay->setFrameShape(QFrame::NoFrame);
    m_overlay->verticalHeader()->hide();
    // isHidden(), not isVisible(): the table is usually built before it is shown.
    m_overlay->horizontalHeader()->setVisible(!table->horizontalHeader()->isHidden());
    m_overlay->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_overlay->horizontalHeader()->setMinimumSectionSize(table->horizontalHeader()->minimumSectionSize());
    m_overlay->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_overlay->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_overlay->setVerticalScrollMode(table->verticalScrollMode());
    m_overlay->setShowGrid(table->showGrid());
    m_overlay->setToolTip(table->toolTip());
    m_overlay->hide();
    table->viewport()->stackUnder(m_overlay);
    // Pixels, not columns: a cell can be scrolled clear of the overlay exactly.
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Up and down with the table. One way only: before the overlay is laid
    // out its range is shorter, and a clamped value sent back would scroll the
    // table to the top. A wheel on the overlay is passed to the table.
    connect(table->verticalScrollBar(), &QAbstractSlider::valueChanged,
            m_overlay->verticalScrollBar(), &QAbstractSlider::setValue);
    // The overlay never scrolls on its own: Qt moves a view's scroll bar
    // itself (a model reset, a new range), so put it back to the table's.
    auto follow = [this]() {
        if (m_table && m_overlay->verticalScrollBar()->value() != m_table->verticalScrollBar()->value())
            m_overlay->verticalScrollBar()->setValue(m_table->verticalScrollBar()->value());
    };
    connect(m_overlay->verticalScrollBar(), &QAbstractSlider::rangeChanged, this, follow);
    connect(m_overlay->verticalScrollBar(), &QAbstractSlider::valueChanged, this, follow, Qt::QueuedConnection);
    m_overlay->viewport()->installEventFilter(this);
    // The table's widths and row heights are the overlay's.
    connect(table->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int col, int, int size) {
        if (col < m_count) { m_overlay->setColumnWidth(col, size); place(); }
    });
    connect(table->verticalHeader(), &QHeaderView::sectionResized, this, [this](int row, int, int size) {
        m_overlay->setRowHeight(row, size);
    });
    connect(m_overlay, &QAbstractItemView::doubleClicked, table, &QAbstractItemView::doubleClicked);
    // A reset or a new column: the overlay's hidden columns are set again.
    if (QAbstractItemModel *m = table->model()) {
        connect(m, &QAbstractItemModel::modelReset, this, &FrozenColumns::apply, Qt::QueuedConnection);
        connect(m, &QAbstractItemModel::columnsInserted, this, &FrozenColumns::apply, Qt::QueuedConnection);
        connect(m, &QAbstractItemModel::layoutChanged, this, &FrozenColumns::apply, Qt::QueuedConnection);
    }
    table->installEventFilter(this);
    table->viewport()->installEventFilter(this);
}

void FrozenColumns::setCount(int n)
{
    m_count = qMax(0, n);
    apply();
}

void FrozenColumns::apply()
{
    if (!m_table) return;
    const int cols = m_table->model() ? m_table->model()->columnCount() : 0;
    const int n = qMin(m_count, cols);
    m_overlay->verticalHeader()->setDefaultSectionSize(m_table->verticalHeader()->defaultSectionSize());
    m_overlay->horizontalHeader()->setFixedHeight(m_table->horizontalHeader()->height());
    for (int c = 0; c < cols; ++c) {
        m_overlay->setColumnHidden(c, c >= n || m_table->isColumnHidden(c));
        if (c < n) m_overlay->setColumnWidth(c, m_table->columnWidth(c));
    }
    m_overlay->setVisible(n > 0);
    m_overlay->verticalScrollBar()->setValue(m_table->verticalScrollBar()->value());
    place();
}

void FrozenColumns::place()
{
    if (!m_table || m_overlay->isHidden()) return;
    int width = 0;
    for (int c = 0; c < m_count && c < m_table->horizontalHeader()->count(); ++c)
        if (!m_table->isColumnHidden(c)) width += m_table->columnWidth(c);
    const int frame = m_table->frameWidth();
    const int headerH = m_table->horizontalHeader()->isHidden() ? 0 : m_table->horizontalHeader()->height();
    m_overlay->horizontalHeader()->setFixedHeight(headerH);
    m_overlay->setGeometry(m_table->verticalHeader()->isHidden() ? frame : m_table->verticalHeader()->width() + frame,
                           frame, width, m_table->viewport()->height() + headerH);
}

bool FrozenColumns::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_overlay->viewport() && event->type() == QEvent::Wheel && m_table) {
        QCoreApplication::sendEvent(m_table->viewport(), event);
        return true;
    }
    if (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest
        || event->type() == QEvent::Show)
        place();
    return QObject::eventFilter(watched, event);
}

void FrozenColumns::scrollTo(const QModelIndex &index, int hint)
{
    if (!m_table || !index.isValid()) return;
    m_table->scrollTo(index, QAbstractItemView::ScrollHint(hint));
    if (m_count <= 0 || m_overlay->isHidden() || index.column() < m_count) return;
    const QRect cell = m_table->visualRect(index);
    const int covered = m_overlay->width();
    if (cell.left() < covered)
        m_table->horizontalScrollBar()->setValue(m_table->horizontalScrollBar()->value() - (covered - cell.left()));
}
