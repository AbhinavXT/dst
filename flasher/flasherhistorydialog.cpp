#include "flasherhistorydialog.h"

#include "flasherstyle.h"
#include "uicolors.h"
#include "uistyle.h"
#include "windowgeometry.h"

#include <QCloseEvent>
#include <QComboBox>
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

enum HistoryColumn {
    ColumnWhen = 0,
    ColumnOperator,
    ColumnChassis,
    ColumnCard,
    ColumnImage,
    ColumnCrc,
    ColumnResult,
    ColumnTime,
    ColumnCountTotal
};

}  // namespace

FlasherHistoryDialog::FlasherHistoryDialog(const QString &historyPath, QWidget *parent)
    : QDialog(parent)
    , m_log(historyPath)
{
    setWindowTitle(tr("Flash history"));
    WindowGeometry::makeResizableWindow(this);
    resize(1180, 760);
    WindowGeometry::restore(this, QStringLiteral("flasherHistory"));

    auto *dialogLayout = new QVBoxLayout(this);
    dialogLayout->setContentsMargins(16, 16, 16, 16);
    dialogLayout->setSpacing(12);

    // ---- title + filters -------------------------------------------------------
    auto *titleRow = new QHBoxLayout();
    auto *titleColumn = new QVBoxLayout();
    auto *title = new QLabel(tr("Flash history"), this);
    title->setFont(FlasherStyle::scaledFont(title->font(), 1.5, true));
    titleColumn->addWidget(title);
    auto *subtitle = new QLabel(tr("Stored locally · append-only · %1")
                                    .arg(QDir::toNativeSeparators(historyPath)), this);
    subtitle->setStyleSheet(UiColor::mutedStyle());
    subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titleColumn->addWidget(subtitle);
    titleRow->addLayout(titleColumn, 1);
    dialogLayout->addLayout(titleRow);

    auto *filterRow = new QHBoxLayout();
    filterRow->setSpacing(10);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search history — file, chassis, operator, CRC, batch"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(34);
    filterRow->addWidget(m_search, 1);

    m_cardFilter = new QComboBox(this);
    m_cardFilter->addItem(tr("Card: All"), 0);
    for (int cardType : Flasher::defaultCardOrder()) {
        m_cardFilter->addItem(tr("Card: %1").arg(Flasher::cardName(cardType)), cardType);
    }
    filterRow->addWidget(m_cardFilter);

    m_resultFilter = new QComboBox(this);
    m_resultFilter->addItem(tr("Result: All"), QString());
    const Flasher::CardOutcome outcomes[] = {
        Flasher::CardOutcome::Accepted, Flasher::CardOutcome::AlreadyHeld, Flasher::CardOutcome::Failed,
        Flasher::CardOutcome::Cancelled, Flasher::CardOutcome::NotRun
    };
    for (Flasher::CardOutcome outcome : outcomes) {
        m_resultFilter->addItem(tr("Result: %1").arg(Flasher::outcomeText(outcome)),
                                Flasher::outcomeText(outcome));
    }
    filterRow->addWidget(m_resultFilter);

    m_periodFilter = new QComboBox(this);
    m_periodFilter->addItem(tr("Last 7 days"), 7);
    m_periodFilter->addItem(tr("Last 30 days"), 30);
    m_periodFilter->addItem(tr("Last 90 days"), 90);
    m_periodFilter->addItem(tr("All time"), 0);
    m_periodFilter->setCurrentIndex(1);
    filterRow->addWidget(m_periodFilter);

    auto *exportButton = new QPushButton(tr("Export CSV"), this);
    connect(exportButton, &QPushButton::clicked, this, &FlasherHistoryDialog::exportCsv);
    filterRow->addWidget(exportButton);
    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    connect(refreshButton, &QPushButton::clicked, this, &FlasherHistoryDialog::reload);
    filterRow->addWidget(refreshButton);
    dialogLayout->addLayout(filterRow);

    // ---- table above, detail below -----------------------------------------------
    auto *splitter = new QSplitter(Qt::Vertical, this);
    m_table = new QTableWidget(0, ColumnCountTotal, splitter);
    m_table->setHorizontalHeaderLabels({ tr("When"), tr("Operator"), tr("Chassis"), tr("Card"),
                                         tr("Image"), tr("CRC-32"), tr("Result"), tr("Time") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnImage, QHeaderView::Stretch);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->setColumnWidth(ColumnWhen, 150);
    m_table->setColumnWidth(ColumnOperator, 110);
    m_table->setColumnWidth(ColumnChassis, 150);
    m_table->setColumnWidth(ColumnCard, 120);
    m_table->setColumnWidth(ColumnCrc, 110);
    m_table->setColumnWidth(ColumnResult, 110);
    m_table->setColumnWidth(ColumnTime, 80);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &FlasherHistoryDialog::onRowSelected);

    auto *detail = new QWidget(splitter);
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(0, 8, 0, 0);
    detailLayout->setSpacing(6);
    m_detailHeader = new QLabel(detail);
    m_detailHeader->setFont(FlasherStyle::scaledFont(m_detailHeader->font(), 1.0, true));
    detailLayout->addWidget(m_detailHeader);
    m_detailFacts = new QLabel(detail);
    m_detailFacts->setFont(UiStyle::monoFont());
    m_detailFacts->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailFacts->setWordWrap(true);
    detailLayout->addWidget(m_detailFacts);
    m_detailLog = new QPlainTextEdit(detail);
    m_detailLog->setReadOnly(true);
    m_detailLog->setFont(UiStyle::monoFont());
    m_detailLog->setLineWrapMode(QPlainTextEdit::NoWrap);
    detailLayout->addWidget(m_detailLog, 1);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    dialogLayout->addWidget(splitter, 1);

    m_footer = new QLabel(this);
    m_footer->setStyleSheet(UiColor::mutedStyle());
    dialogLayout->addWidget(m_footer);

    connect(m_search, &QLineEdit::textChanged, this, &FlasherHistoryDialog::applyFilters);
    connect(m_cardFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FlasherHistoryDialog::applyFilters);
    connect(m_resultFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FlasherHistoryDialog::applyFilters);
    connect(m_periodFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FlasherHistoryDialog::applyFilters);

    UiColor::onThemeChange(this, [this]() { applyFilters(); });
    reload();
}

void FlasherHistoryDialog::reload()
{
    int skipped = 0;
    m_all = m_log.readAll(&skipped);
    // Newest first: that is what anyone opening an audit trail looks for.
    std::reverse(m_all.begin(), m_all.end());
    applyFilters();
    if (skipped > 0) {
        m_footer->setText(tr("%1 record(s) · %2 unreadable line(s) skipped").arg(m_all.size()).arg(skipped));
        m_footer->setStyleSheet(UiColor::warningStyle());
    }
}

int FlasherHistoryDialog::visibleRowCount() const
{
    return m_table->rowCount();
}

void FlasherHistoryDialog::applyFilters()
{
    Flasher::HistoryFilter filter;
    filter.text = m_search->text();
    filter.cardType = m_cardFilter->currentData().toInt();
    filter.result = m_resultFilter->currentData().toString();
    filter.lastDays = m_periodFilter->currentData().toInt();

    const QDateTime now = QDateTime::currentDateTimeUtc();
    m_visible.clear();
    for (const Flasher::HistoryRecord &record : m_all) {
        if (Flasher::historyMatches(record, filter, now)) {
            m_visible.append(record);
        }
    }

    m_table->setRowCount(0);
    m_table->setRowCount(m_visible.size());
    const QFont mono = UiStyle::monoFont();
    for (int row = 0; row < m_visible.size(); ++row) {
        const Flasher::HistoryRecord &record = m_visible.at(row);
        auto setCell = [this, row](int column, const QString &text) -> QTableWidgetItem * {
            auto *item = new QTableWidgetItem(text);
            m_table->setItem(row, column, item);
            return item;
        };
        setCell(ColumnWhen, record.when.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        setCell(ColumnOperator, record.operatorName);
        setCell(ColumnChassis, record.chassis);
        setCell(ColumnCard, Flasher::cardName(record.cardType));
        QTableWidgetItem *imageItem = setCell(ColumnImage, record.fileName());
        imageItem->setFont(mono);
        imageItem->setToolTip(QDir::toNativeSeparators(record.imagePath));
        QTableWidgetItem *crcItem = setCell(ColumnCrc, record.crcText);
        crcItem->setFont(mono);

        // Result: coloured text plus the word itself (colour alone is not a signal).
        QTableWidgetItem *resultItem = setCell(ColumnResult, record.result);
        QFont bold = resultItem->font();
        bold.setBold(true);
        resultItem->setFont(bold);
        if (record.result == Flasher::outcomeText(Flasher::CardOutcome::Accepted)
            || record.result == Flasher::outcomeText(Flasher::CardOutcome::AlreadyHeld)) {
            resultItem->setForeground(FlasherStyle::accepted());
        } else if (record.result == Flasher::outcomeText(Flasher::CardOutcome::Failed)
                   || record.result == Flasher::outcomeText(Flasher::CardOutcome::Cancelled)) {
            resultItem->setForeground(FlasherStyle::danger());
        } else {
            resultItem->setForeground(FlasherStyle::muted());
        }

        if (record.result == Flasher::outcomeText(Flasher::CardOutcome::NotRun)) {
            setCell(ColumnTime, QStringLiteral("—"));
        } else {
            setCell(ColumnTime, Flasher::formatDuration(record.totalMs));
        }
    }

    m_footer->setStyleSheet(UiColor::mutedStyle());
    m_footer->setText(tr("Showing %1 of %2 record(s)").arg(m_visible.size()).arg(m_all.size()));
    onRowSelected();
}

void FlasherHistoryDialog::onRowSelected()
{
    const QList<QTableWidgetItem *> selected = m_table->selectedItems();
    if (selected.isEmpty()) {
        m_detailHeader->setText(tr("Select a row to see its full session log, SHA-256 and phase timing."));
        m_detailFacts->clear();
        m_detailLog->clear();
        return;
    }
    const int row = selected.first()->row();
    if (row < 0 || row >= m_visible.size()) {
        return;
    }
    const Flasher::HistoryRecord &record = m_visible.at(row);
    m_detailHeader->setText(tr("%1 · %2 · %3").arg(Flasher::cardName(record.cardType), record.result,
                                                   record.when.toLocalTime().toString(Qt::ISODate)));
    QStringList facts;
    facts << tr("Image     %1").arg(QDir::toNativeSeparators(record.imagePath));
    facts << tr("SHA-256   %1").arg(record.sha256);
    facts << tr("CRC-32    %1").arg(record.crcText);
    facts << tr("Batch     %1 · %2 → %3").arg(record.batchId, record.chassis, record.vccIp);
    facts << tr("Phases    handshake %1 · send %2 · repair %3 · total %4")
                 .arg(Flasher::formatDuration(record.handshakeMs), Flasher::formatDuration(record.sendMs),
                      Flasher::formatDuration(record.repairMs), Flasher::formatDuration(record.totalMs));
    facts << tr("Transfer  %1 rounds · %2/%3 blocks resent · %4 on wire · %5 avg")
                 .arg(record.repairRounds).arg(record.blocksResent).arg(record.totalBlocks)
                 .arg(Flasher::formatBytes(record.bytesOnWire), Flasher::formatRate(record.avgKBps));
    facts << tr("Message   %1").arg(record.message);
    m_detailFacts->setText(facts.join(QLatin1Char('\n')));
    m_detailLog->setPlainText(record.log.join(QLatin1Char('\n')));
}

void FlasherHistoryDialog::exportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export history"),
        QStringLiteral("flash_history_%1.csv").arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))),
        tr("CSV files (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Export history"),
                             tr("Could not write %1:\n%2").arg(path, file.errorString()));
        return;
    }
    // Exactly what the filters show, oldest first, so a spreadsheet reads in time order.
    QList<Flasher::HistoryRecord> ordered = m_visible;
    std::reverse(ordered.begin(), ordered.end());
    file.write(Flasher::historyToCsv(ordered).toUtf8());
}

void FlasherHistoryDialog::closeEvent(QCloseEvent *event)
{
    WindowGeometry::save(this, QStringLiteral("flasherHistory"));
    QDialog::closeEvent(event);
}
