#include "nmslogwindow.h"

#include "statusline.h"
#include "windowgeometry.h"
#include "xlsxbook.h"

#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
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
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) return m_table.cell(index.row(), index.column());
    return QVariant();
}

QVariant NmsLogTableModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (role != Qt::DisplayRole) return QVariant();
    if (o == Qt::Vertical) return section + 1;
    return m_table.header(section);
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
    m_view = new QTableView(this);
    m_view->setModel(m_rows);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->horizontalHeader()->setDefaultSectionSize(120);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 6);
    m_view->setToolTip(tr("One row per @%1 packet, as decoded. Double-click a row: the frame in its tab")
                           .arg(NmsLog::token(kind)));

    m_follow = new QCheckBox(tr("Follow new packets"), this);
    m_follow->setChecked(true);
    m_follow->setToolTip(tr("Scroll to each new packet as it arrives"));
    m_count = new QLabel(this);
    m_status = new StatusLine;
    auto *csv = new QPushButton(tr("Export CSV…"), this);
    auto *xlsx = new QPushButton(tr("Export Excel (.xlsx)…"), this);
    csv->setObjectName(QStringLiteral("nmsExportCsv"));
    xlsx->setObjectName(QStringLiteral("nmsExportXlsx"));

    auto *top = new QHBoxLayout;
    top->addWidget(m_count, 1);
    top->addWidget(m_follow);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_status, 1);
    bottom->addWidget(csv);
    bottom->addWidget(xlsx);
    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addWidget(m_view, 1);
    root->addLayout(bottom);

    connect(csv, &QPushButton::clicked, this, [this]() { exportTo(false); });
    connect(xlsx, &QPushButton::clicked, this, [this]() { exportTo(true); });
    connect(m_view, &QTableView::doubleClicked, this, [this](const QModelIndex &i) {
        const NmsLog::Table &t = m_rows->table();
        if (i.isValid() && i.row() < t.epochMs.size()) emit jumpRequested(m_tabKey, t.epochMs.at(i.row()));
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
    if (m_rows->append(m_kind, batch) == 0) return;
    if (m_rows->columnCount() != columnsBefore) m_view->resizeColumnsToContents();
    if (m_follow->isChecked()) m_view->scrollToBottom();
    showCount();
}

void NmsLogWindow::showCount()
{
    const int n = m_rows->table().rows.size();
    m_count->setText(n == 1 ? tr("1 packet") : tr("%1 packets").arg(n));
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
