#include "lococonfighistorydialog.h"

#include "flasherstyle.h"
#include "uicolors.h"
#include "uistyle.h"
#include "windowgeometry.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
enum HistoryColumn { ColumnWhen = 0, ColumnOperator, ColumnConfig, ColumnTarget, ColumnCrc, ColumnVccCrc, ColumnLoco, ColumnTotal };
}

LocoConfigHistoryDialog::LocoConfigHistoryDialog(const QString &historyPath, const LocoInfo::Layout *layout,
                                                 QWidget *parent)
    : QDialog(parent)
    , m_history(historyPath)
    , m_layout(layout)
{
    setWindowTitle(tr("Loco configuration history"));
    WindowGeometry::makeResizableWindow(this);
    resize(1000, 680);

    auto *dialogLayout = new QVBoxLayout(this);
    dialogLayout->setContentsMargins(16, 16, 16, 16);
    dialogLayout->setSpacing(10);

    auto *title = new QLabel(tr("Configurations sent"), this);
    title->setFont(FlasherStyle::scaledFont(title->font(), 1.4, true));
    dialogLayout->addWidget(title);
    auto *subtitle = new QLabel(tr("Append-only · exact bytes of every send · %1")
                                    .arg(QDir::toNativeSeparators(historyPath)), this);
    subtitle->setStyleSheet(UiColor::mutedStyle());
    subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dialogLayout->addWidget(subtitle);

    auto *toolbar = new QHBoxLayout();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search — configuration, target, operator, CRC"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &LocoConfigHistoryDialog::applyFilter);
    toolbar->addWidget(m_search, 1);
    m_exportButton = new QPushButton(tr("Export as loco_info.bin…"), this);
    connect(m_exportButton, &QPushButton::clicked, this, &LocoConfigHistoryDialog::exportSelected);
    toolbar->addWidget(m_exportButton);
    m_loadButton = new QPushButton(tr("Load into current configuration…"), this);
    connect(m_loadButton, &QPushButton::clicked, this, [this]() {
        const LocoInfo::SendRecord *record = selectedRecord();
        if (record != nullptr) {
            emit loadRequested(record->body, describe(*record));
        }
    });
    toolbar->addWidget(m_loadButton);
    auto *refresh = new QPushButton(tr("Refresh"), this);
    connect(refresh, &QPushButton::clicked, this, &LocoConfigHistoryDialog::reload);
    toolbar->addWidget(refresh);
    dialogLayout->addLayout(toolbar);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    m_table = new QTableWidget(0, ColumnTotal, splitter);
    m_table->setHorizontalHeaderLabels({ tr("When"), tr("Operator"), tr("Configuration"), tr("Target"),
                                         tr("loco_info_crc"), tr("vcc_crc"), tr("Loco confirmed") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnConfig, QHeaderView::Stretch);
    m_table->setColumnWidth(ColumnWhen, 150);
    m_table->setColumnWidth(ColumnOperator, 110);
    m_table->setColumnWidth(ColumnTarget, 170);
    m_table->setColumnWidth(ColumnCrc, 120);
    m_table->setColumnWidth(ColumnVccCrc, 120);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &LocoConfigHistoryDialog::onSelection);
    m_detail = new QPlainTextEdit(splitter);
    m_detail->setReadOnly(true);
    m_detail->setFont(UiStyle::monoFont());
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    dialogLayout->addWidget(splitter, 1);

    m_footer = new QLabel(this);
    m_footer->setStyleSheet(UiColor::mutedStyle());
    dialogLayout->addWidget(m_footer);

    reload();
}

void LocoConfigHistoryDialog::reload()
{
    int skipped = 0;
    m_all = m_history.readAll(&skipped);
    m_verifications = m_history.readVerifications();
    std::reverse(m_all.begin(), m_all.end());
    applyFilter();
    if (skipped > 0) {
        m_footer->setText(tr("%1 send(s) · %2 unreadable line(s) skipped").arg(m_all.size()).arg(skipped));
    }
}

int LocoConfigHistoryDialog::visibleRowCount() const
{
    return m_table->rowCount();
}

QString LocoConfigHistoryDialog::describe(const LocoInfo::SendRecord &record) const
{
    return tr("to %1 on %2").arg(record.target,
                                 record.when.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
}

void LocoConfigHistoryDialog::applyFilter()
{
    const QString needle = m_search->text().trimmed();
    m_visible.clear();
    for (const LocoInfo::SendRecord &record : m_all) {
        const QString haystack = QStringList{ record.configName, record.target, record.operatorName,
                                              LocoInfo::crcText(record.crc), LocoInfo::crcText(record.vccCrc) }
                                     .join(QLatin1Char(' '));
        if (needle.isEmpty() || haystack.contains(needle, Qt::CaseInsensitive)) {
            m_visible.append(record);
        }
    }
    m_table->setRowCount(0);
    m_table->setRowCount(m_visible.size());
    const QFont mono = UiStyle::monoFont();
    for (int row = 0; row < m_visible.size(); ++row) {
        const LocoInfo::SendRecord &record = m_visible.at(row);
        auto setCell = [this, row](int column, const QString &text) -> QTableWidgetItem * {
            auto *item = new QTableWidgetItem(text);
            m_table->setItem(row, column, item);
            return item;
        };
        setCell(ColumnWhen, record.when.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        setCell(ColumnOperator, record.operatorName);
        setCell(ColumnConfig, record.configName);
        setCell(ColumnTarget, record.target)->setFont(mono);
        setCell(ColumnCrc, LocoInfo::crcText(record.crc))->setFont(mono);
        setCell(ColumnVccCrc, LocoInfo::crcText(record.vccCrc))->setFont(mono);
        // What the loco's next @linfo said about this send (session 81).
        const LocoInfo::Verification *v = verificationFor(record);
        QTableWidgetItem *loco = setCell(ColumnLoco, v == nullptr ? tr("—")
                                          : (v->match ? tr("✓ %1 at %2").arg(v->locoKey, v->seenAt.toLocalTime().toString(QStringLiteral("HH:mm:ss")))
                                                      : tr("✗ %1: %2 differ").arg(v->locoKey).arg(v->differences.size())));
        if (v == nullptr) {
            loco->setToolTip(tr("No @linfo from the loco was seen after this send while the window was open."));
        } else {
            loco->setForeground(v->match ? UiColor::ok() : UiColor::error());
            loco->setToolTip(v->differences.join(QLatin1Char('\n')));
        }
    }
    m_footer->setText(tr("Showing %1 of %2 send(s)").arg(m_visible.size()).arg(m_all.size()));
    onSelection();
}

const LocoInfo::Verification *LocoConfigHistoryDialog::verificationFor(const LocoInfo::SendRecord &record) const
{
    for (const LocoInfo::Verification &v : m_verifications) {
        if (qAbs(v.sentAt.msecsTo(record.when)) < 1000) return &v;
    }
    return nullptr;
}

const LocoInfo::SendRecord *LocoConfigHistoryDialog::selectedRecord() const
{
    const QList<QTableWidgetItem *> selected = m_table->selectedItems();
    if (selected.isEmpty()) {
        return nullptr;
    }
    const int row = selected.first()->row();
    if (row < 0 || row >= m_visible.size()) {
        return nullptr;
    }
    return &m_visible.at(row);
}

void LocoConfigHistoryDialog::onSelection()
{
    const LocoInfo::SendRecord *record = selectedRecord();
    m_exportButton->setEnabled(record != nullptr);
    m_loadButton->setEnabled(record != nullptr);
    if (record == nullptr) {
        m_detail->setPlainText(tr("Select a send to see its bytes."));
        return;
    }
    // The body as a hex dump, 16 bytes a line with offsets: the same view
    // loco_config_v12 prints, so the two can be compared by eye.
    QStringList lines;
    lines << tr("%1 · %2 · %3 bytes").arg(record->configName, describe(*record)).arg(record->body.size());
    const LocoInfo::Parsed parsed = LocoInfo::parseBody(*m_layout, record->body);
    if (parsed.ok && !parsed.crcOk) {
        lines << tr("WARNING: stored CRC does not match the bytes");
    }
    if (const LocoInfo::Verification *v = verificationFor(*record)) {
        lines << (v->match ? tr("Loco %1 reported exactly this at %2").arg(v->locoKey, v->seenAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                           : tr("Loco %1 reported something else at %2:").arg(v->locoKey, v->seenAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        for (const QString &diff : v->differences) lines << QStringLiteral("  ") + diff;
    } else {
        lines << tr("Not confirmed: no @linfo from the loco was seen after this send.");
    }
    lines << QString();
    for (int offset = 0; offset < record->body.size(); offset += 16) {
        const QByteArray chunk = record->body.mid(offset, 16);
        lines << QStringLiteral("%1  %2").arg(offset, 4, 16, QLatin1Char('0'))
                     .arg(QString::fromLatin1(chunk.toHex(' ')).toUpper());
    }
    m_detail->setPlainText(lines.join(QLatin1Char('\n')));
}

void LocoConfigHistoryDialog::exportSelected()
{
    const LocoInfo::SendRecord *record = selectedRecord();
    if (record == nullptr) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export loco_info.bin"),
                                                      QStringLiteral("loco_info.bin"), tr("LOCO_INFO (*.bin)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Could not write %1:\n%2").arg(path, file.errorString()));
        return;
    }
    file.write(record->body);
}
