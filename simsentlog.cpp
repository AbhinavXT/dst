#include "simsentlog.h"

#include "locosim.h"
#include "rfidtag.h"
#include "uicolors.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// The 16 tag bytes of a tag frame (AA AA len reader <16> BB BB), else empty.
QByteArray tagOf(const QByteArray &frame)
{
    return frame.size() == LocoSim::kTagFrameSize ? frame.mid(4, RfidTag::TagBytes) : QByteArray();
}

}  // namespace

SimSentLog::SimSentLog(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("simSentLog"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto *bar = new QHBoxLayout;
    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("simSentCount"));
    m_count->setStyleSheet(UiColor::mutedStyle());
    bar->addWidget(m_count, 1);
    m_showHealth = new QCheckBox(tr("Health frames"), this);
    m_showHealth->setObjectName(QStringLiteral("simSentShowHealth"));
    m_showHealth->setChecked(true);
    m_showHealth->setToolTip(tr("Show the readers' health frames too (reader 1 every 1 s, reader 2 every 2 s)"));
    connect(m_showHealth, &QCheckBox::toggled, this, [this]() { applyHealthFilter(); });
    bar->addWidget(m_showHealth);
    auto *clearButton = new QToolButton(this);
    clearButton->setObjectName(QStringLiteral("simSentClear"));
    clearButton->setText(tr("Clear"));
    clearButton->setAutoRaise(true);
    connect(clearButton, &QToolButton::clicked, this, &SimSentLog::clear);
    bar->addWidget(clearButton);
    layout->addLayout(bar);

    m_table = new QTableWidget(0, Columns, this);
    m_table->setObjectName(QStringLiteral("simSentTable"));
    m_table->setHorizontalHeaderLabels({ tr("Time"), tr("Reader"), tr("Port"), tr("Frame"), tr("Tag / health"),
                                         tr("At"), tr("Note"), tr("Bytes") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    // Measured once to the font, not to the rows (5000 of them).
    {
        const QFontMetrics fm(m_table->font());
        const int pad = 18;
        const int widths[ColBytes] = {
            fm.horizontalAdvance(QStringLiteral("00:00:00.000")), fm.horizontalAdvance(tr("Reader")),
            fm.horizontalAdvance(tr("port A")), fm.horizontalAdvance(tr("health")), fm.horizontalAdvance(tr("Tag / health")),
            fm.horizontalAdvance(QStringLiteral("000000.0 m")), fm.horizontalAdvance(tr("not sent: port A closed")),
        };
        for (int c = 0; c < ColBytes; ++c) m_table->setColumnWidth(c, widths[c] + pad);
    }
    m_table->setToolTip(tr("Every RFID frame the simulator sent, newest at the bottom. "
                           "Double-click a tag to open it in the Decode Workbench."));
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { openRow(row); });
    layout->addWidget(m_table, 1);
    refreshCount();
}

void SimSentLog::add(const Frame &in)
{
    Frame frame = in;
    frame.seq = ++m_seq;
    QScrollBar *bar = m_table->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum() - 2;

    if (m_frames.size() >= kMaxRows) {
        (m_frames.first().health ? m_healthRows : m_tagRows) -= 1;
        m_frames.removeFirst();
        m_table->removeRow(0);
    }
    m_frames.append(frame);
    (frame.health ? m_healthRows : m_tagRows) += 1;
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    const QByteArray tag = tagOf(frame.bytes);
    QString what;
    if (frame.health)
        what = frame.bytes.size() > 4 && quint8(frame.bytes.at(4)) == 1 ? tr("OK (1)") : tr("fail (2)");
    else
        what = tag.isEmpty() ? tr("?") : RfidTag::nameOf(tag);
    QString tip;
    if (!tag.isEmpty()) {
        const RfidTag::Summary sum = RfidTag::summary(tag);
        tip = tr("Tag %1 · %2 · abs_loc %3 · CRC %4")
                  .arg(RfidTag::nameOf(tag), RfidTag::typeName(sum.type))
                  .arg(sum.absLoc)
                  .arg(sum.crcOk ? tr("good") : tr("bad"));
        for (const auto &f : RfidTag::describe(tag)) tip += QStringLiteral("\n%1: %2").arg(f.first, f.second);
    }
    const QString cells[Columns] = {
        QDateTime::fromMSecsSinceEpoch(frame.ms).toString(QStringLiteral("HH:mm:ss.zzz")),
        QString::number(frame.reader),
        frame.port,
        frame.health ? tr("health") : tr("tag"),
        what,
        std::isnan(frame.atM) ? QString() : tr("%1 m").arg(frame.atM, 0, 'f', 1),
        frame.note,
        QString::fromLatin1(frame.bytes.toHex(' ')).toUpper(),
    };
    for (int c = 0; c < Columns; ++c) {
        auto *item = new QTableWidgetItem(cells[c]);
        if (!tip.isEmpty()) item->setToolTip(tip);
        if (!frame.sent) item->setForeground(UiColor::error());
        m_table->setItem(row, c, item);
    }
    m_table->setRowHidden(row, frame.health && !m_showHealth->isChecked());
    if (atEnd) m_table->scrollToBottom();
    refreshCount();
}

void SimSentLog::clear()
{
    m_frames.clear();
    m_tagRows = m_healthRows = 0;
    m_table->setRowCount(0);
    refreshCount();
}

int SimSentLog::count() const
{
    return m_frames.size();
}

QString SimSentLog::workbenchLine(const Frame &frame)
{
    if (frame.health || tagOf(frame.bytes).isEmpty()) return QString();
    // As a VCC log has it: @rfid_<loco>_<cab> <time> <seq> <reader> <16 tag bytes>.
    return QStringLiteral("@rfid_0_0 %1 %2 %3")
        .arg(QDateTime::fromMSecsSinceEpoch(frame.ms).toString(Qt::ISODate))
        .arg(frame.seq)
        .arg(QString::fromLatin1(frame.bytes.mid(3, 1 + RfidTag::TagBytes).toHex(' ')).toUpper());
}

void SimSentLog::openRow(int row)
{
    if (row < 0 || row >= m_frames.size()) return;
    const Frame &frame = m_frames.at(row);
    const QString line = workbenchLine(frame);
    if (line.isEmpty() || !m_open) return;
    m_open(line, tr("sent tag %1, reader %2").arg(RfidTag::nameOf(tagOf(frame.bytes))).arg(frame.reader));
}

void SimSentLog::applyHealthFilter()
{
    const bool show = m_showHealth->isChecked();
    for (int row = 0; row < m_frames.size(); ++row)
        m_table->setRowHidden(row, m_frames.at(row).health && !show);
    if (show) m_table->scrollToBottom();
}

void SimSentLog::refreshCount()
{
    int tags = 0, health = 0;
    for (const Frame &f : m_frames) (f.health ? health : tags) += 1;
    m_count->setText(tr("%n tag frame(s)", nullptr, tags) + tr(" · %n health frame(s)", nullptr, health));
    emit countChanged(m_frames.size());
}
