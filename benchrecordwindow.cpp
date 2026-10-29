#include "benchrecordwindow.h"
#include "statusline.h"
#include "windowgeometry.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

enum Column { ColTime, ColSince, ColKind, ColDir, ColPeer, ColSays, ColNote, ColBytes, Columns };

QString csvCell(const QString &text)
{
    if (!text.contains(QLatin1Char(',')) && !text.contains(QLatin1Char('"')) && !text.contains(QLatin1Char('\n')))
        return text;
    QString quoted = text;
    quoted.replace(QLatin1String("\""), QLatin1String("\"\""));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

}  // namespace

BenchRecordWindow::BenchRecordWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("benchRecordWindow"));
    setWindowTitle(tr("Test recording"));
    setAttribute(Qt::WA_DeleteOnClose);
    WindowGeometry::makeResizableWindow(this);
    auto *root = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    m_fileLabel = new QLabel(tr("No recording open"), this);
    m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(m_fileLabel, 1);
    auto *open = new QPushButton(tr("Open…"), this);
    top->addWidget(open);
    auto *csv = new QPushButton(tr("Export CSV…"), this);
    csv->setObjectName(QStringLiteral("benchRecordCsv"));
    top->addWidget(csv);
    root->addLayout(top);

    auto *filters = new QHBoxLayout;
    for (const QString &kind : BenchRecord::kinds()) {
        auto *box = new QCheckBox(kind, this);
        box->setChecked(true);
        box->setObjectName(QStringLiteral("benchRecordKind_%1").arg(kind));
        connect(box, &QCheckBox::toggled, this, [this]() { applyFilter(); });
        m_kindBox.insert(kind, box);
        filters->addWidget(box);
    }
    filters->addSpacing(12);
    m_find = new QLineEdit(this);
    m_find->setPlaceholderText(tr("Find: every word, anywhere in the row"));
    m_find->setClearButtonEnabled(true);
    connect(m_find, &QLineEdit::textChanged, this, [this]() { applyFilter(); });
    filters->addWidget(m_find, 1);
    filters->addSpacing(12);
    filters->addWidget(new QLabel(tr("Go to"), this));
    m_goTo = new QDoubleSpinBox(this);
    m_goTo->setRange(0, 1e7);
    m_goTo->setDecimals(1);
    m_goTo->setSuffix(tr(" s"));
    filters->addWidget(m_goTo);
    auto *go = new QPushButton(tr("Go"), this);
    filters->addWidget(go);
    root->addLayout(filters);

    m_table = new QTableWidget(0, Columns, this);
    m_table->setObjectName(QStringLiteral("benchRecordTable"));
    m_table->setHorizontalHeaderLabels({ tr("Time"), tr("+ s"), tr("Kind"), tr("Dir"), tr("Peer"), tr("Says"),
                                         tr("Note"), tr("Bytes") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(m_table, 1);
    m_status = new StatusLine(this);
    root->addWidget(m_status);

    connect(open, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open a test recording"),
                                                          m_path.isEmpty() ? BenchRecord::defaultPath(0).section(QLatin1Char('/'), 0, -2)
                                                                           : QFileInfo(m_path).absolutePath(),
                                                          tr("Test recordings (*.benchrec);;All files (*)"));
        if (!path.isEmpty()) openFile(path);
    });
    connect(csv, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export the rows shown as CSV"),
                                                          QFileInfo(m_path).completeBaseName() + QStringLiteral(".csv"),
                                                          tr("CSV (*.csv)"));
        if (!path.isEmpty()) exportCsv(path, true);
    });
    connect(go, &QPushButton::clicked, this, [this]() { goTo(m_goTo->value()); });
    resize(1200, 640);
    m_status->state(tr("Open a .benchrec file: the Loco Simulator's or the Brake Test Bench's Record button makes one."));
}

bool BenchRecordWindow::openFile(const QString &path)
{
    QString err;
    BenchRecord::File file;
    if (!BenchRecord::read(path, &file, &err)) {
        m_status->fail(tr("%1 not opened: %2").arg(QFileInfo(path).fileName(), err));
        return false;
    }
    m_file = file;
    m_path = path;
    m_fileLabel->setText(QFileInfo(path).fileName());
    m_fileLabel->setToolTip(path);
    setWindowTitle(tr("Test recording — %1").arg(QFileInfo(path).fileName()));
    fill();
    if (file.torn) m_status->warn(tr("%1 frames; the file ends in a line cut short (a crash while recording): "
                                     "everything before it is here.").arg(file.items.size()));
    else m_status->ok(tr("%1 frames, from %2.").arg(file.items.size())
                          .arg(QDateTime::fromMSecsSinceEpoch(file.createdMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    return true;
}

void BenchRecordWindow::fill()
{
    QHash<QString, int> counts;
    m_table->setRowCount(m_file.items.size());
    for (int row = 0; row < m_file.items.size(); ++row) {
        const BenchRecord::Item &item = m_file.items.at(row);
        ++counts[item.kind];
        const QStringList cells = {
            QDateTime::fromMSecsSinceEpoch(item.ms).toString(QStringLiteral("HH:mm:ss.zzz")),
            QString::number((item.ms - m_file.createdMs) / 1000.0, 'f', 3),
            item.kind,
            item.kind == QLatin1String("event") ? QString() : item.tx ? tr("sent") : tr("received"),
            item.peer, item.says, item.note,
            QString::fromLatin1(item.bytes.toHex(' ').toUpper()),
        };
        for (int col = 0; col < Columns; ++col) m_table->setItem(row, col, new QTableWidgetItem(cells.at(col)));
    }
    for (auto it = m_kindBox.cbegin(); it != m_kindBox.cend(); ++it)
        it.value()->setText(QStringLiteral("%1 (%2)").arg(it.key()).arg(counts.value(it.key())));
    m_table->resizeColumnsToContents();
    m_goTo->setMaximum(m_file.items.isEmpty() ? 0 : (m_file.items.last().ms - m_file.createdMs) / 1000.0);
    applyFilter();
}

void BenchRecordWindow::applyFilter()
{
    const QStringList words = m_find->text().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QCheckBox *box = m_kindBox.value(m_table->item(row, ColKind)->text());
        bool show = !box || box->isChecked();
        if (show && !words.isEmpty()) {
            QString all;
            for (int col = 0; col < Columns; ++col) all += m_table->item(row, col)->text().toLower() + QLatin1Char(' ');
            for (const QString &w : words) show = show && all.contains(w);
        }
        m_table->setRowHidden(row, !show);
    }
}

void BenchRecordWindow::setKindShown(const QString &kind, bool shown)
{
    if (QCheckBox *box = m_kindBox.value(kind)) box->setChecked(shown);
}

void BenchRecordWindow::setFind(const QString &text)
{
    m_find->setText(text);
}

int BenchRecordWindow::shownRows() const
{
    int n = 0;
    for (int row = 0; row < m_table->rowCount(); ++row) n += m_table->isRowHidden(row) ? 0 : 1;
    return n;
}

int BenchRecordWindow::goTo(double seconds)
{
    const qint64 at = m_file.createdMs + qint64(seconds * 1000);
    for (int row = 0; row < m_file.items.size(); ++row) {
        if (m_table->isRowHidden(row) || m_file.items.at(row).ms < at) continue;
        m_table->selectRow(row);
        m_table->scrollToItem(m_table->item(row, 0), QAbstractItemView::PositionAtTop);
        return row;
    }
    m_status->warn(tr("No row shown at or after %1 s.").arg(seconds, 0, 'f', 1));
    return -1;
}

QString BenchRecordWindow::toCsv(bool shownOnly) const
{
    QStringList lines;
    QStringList head;
    for (int col = 0; col < Columns; ++col) head << csvCell(m_table->horizontalHeaderItem(col)->text());
    lines << head.join(QLatin1Char(','));
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (shownOnly && m_table->isRowHidden(row)) continue;
        QStringList cells;
        for (int col = 0; col < Columns; ++col) cells << csvCell(m_table->item(row, col)->text());
        lines << cells.join(QLatin1Char(','));
    }
    return lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
}

bool BenchRecordWindow::exportCsv(const QString &path, bool shownOnly)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_status->fail(tr("Not written: %1").arg(f.errorString()));
        return false;
    }
    f.write(toCsv(shownOnly).toUtf8());
    m_status->ok(tr("Written: %1").arg(path));
    return true;
}
