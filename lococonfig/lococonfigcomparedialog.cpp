#include "lococonfigcomparedialog.h"
#include "flasherstyle.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Column { ColumnField = 0, ColumnGroup, ColumnA, ColumnB, ColumnCount };
}

LocoConfigCompareDialog::LocoConfigCompareDialog(const LocoInfo::Layout *layout,
                                                 const LocoInfo::Presentation *presentation,
                                                 const QList<LocoInfo::LocoConfig> &configs,
                                                 const QString &nameA, QWidget *parent)
    : QDialog(parent)
    , m_layout(layout)
    , m_presentation(presentation)
    , m_configs(configs)
{
    setWindowTitle(tr("Compare configurations"));
    resize(900, 600);

    auto *dialogLayout = new QVBoxLayout(this);
    dialogLayout->setContentsMargins(16, 16, 16, 16);
    dialogLayout->setSpacing(10);

    auto *title = new QLabel(tr("Compare configurations"), this);
    title->setFont(FlasherStyle::scaledFont(title->font(), 1.4, true));
    dialogLayout->addWidget(title);
    auto *subtitle = new QLabel(tr("The LOCO_INFO values of two saved configurations · targets and "
                                   "past sends are not compared"), this);
    subtitle->setStyleSheet(UiColor::mutedStyle());
    subtitle->setWordWrap(true);
    dialogLayout->addWidget(subtitle);

    auto *pickers = new QHBoxLayout();
    m_a = new QComboBox(this);
    m_b = new QComboBox(this);
    m_a->setObjectName(QStringLiteral("compareA"));
    m_b->setObjectName(QStringLiteral("compareB"));
    for (const LocoInfo::LocoConfig &c : m_configs) {
        m_a->addItem(c.name);
        m_b->addItem(c.name);
    }
    for (QComboBox *box : { m_a, m_b }) {
        box->setMinimumWidth(200);
        box->setMinimumHeight(30);
    }
    pickers->addWidget(new QLabel(tr("A"), this));
    pickers->addWidget(m_a, 1);
    auto *swap = new QPushButton(tr("Swap"), this);
    swap->setToolTip(tr("Swap A and B"));
    pickers->addWidget(swap);
    pickers->addWidget(new QLabel(tr("B"), this));
    pickers->addWidget(m_b, 1);
    m_all = new QCheckBox(tr("Show all fields"), this);
    m_all->setToolTip(tr("Off: only the fields whose values differ"));
    pickers->addWidget(m_all);
    dialogLayout->addLayout(pickers);

    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setHorizontalHeaderLabels({ tr("Field"), tr("Group"), tr("A"), tr("B") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->hide();
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->setFont(UiStyle::monoFont());
    m_table->horizontalHeader()->setSectionResizeMode(ColumnField, QHeaderView::Stretch);
    m_table->setColumnWidth(ColumnGroup, 170);
    m_table->setColumnWidth(ColumnA, 160);
    m_table->setColumnWidth(ColumnB, 160);
    dialogLayout->addWidget(m_table, 1);

    auto *bottom = new QHBoxLayout();
    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("compareSummary"));
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bottom->addWidget(m_summary, 1);
    auto *copy = new QPushButton(tr("Copy"), this);
    copy->setToolTip(tr("The table as tab-separated text, for a report or a message"));
    bottom->addWidget(copy);
    auto *close = new QPushButton(tr("Close"), this);
    bottom->addWidget(close);
    dialogLayout->addLayout(bottom);

    // A = the configuration open in the editor; B = the next one, so the
    // first view already compares two different configurations.
    const int indexA = qMax(0, m_a->findText(nameA));
    m_a->setCurrentIndex(indexA);
    m_b->setCurrentIndex(m_configs.size() > 1 ? (indexA + 1) % m_configs.size() : indexA);

    connect(m_a, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { rebuild(); });
    connect(m_b, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { rebuild(); });
    connect(m_all, &QCheckBox::toggled, this, [this](bool) { rebuild(); });
    connect(swap, &QPushButton::clicked, this, [this]() { setPair(m_b->currentText(), m_a->currentText()); });
    connect(copy, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(asText()); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    rebuild();
}

const LocoInfo::LocoConfig *LocoConfigCompareDialog::config(const QString &name) const
{
    for (const LocoInfo::LocoConfig &c : m_configs) {
        if (c.name == name) {
            return &c;
        }
    }
    return nullptr;
}

void LocoConfigCompareDialog::setPair(const QString &nameA, const QString &nameB)
{
    const QSignalBlocker blockA(m_a);
    const QSignalBlocker blockB(m_b);
    m_a->setCurrentIndex(m_a->findText(nameA));
    m_b->setCurrentIndex(m_b->findText(nameB));
    rebuild();
}

void LocoConfigCompareDialog::setShowAll(bool on)
{
    const QSignalBlocker block(m_all);
    m_all->setChecked(on);
    rebuild();
}

void LocoConfigCompareDialog::rebuild()
{
    m_table->setRowCount(0);
    const LocoInfo::LocoConfig *a = config(m_a->currentText());
    const LocoInfo::LocoConfig *b = config(m_b->currentText());
    if (a == nullptr || b == nullptr) {
        m_summary->setText(tr("Pick two configurations"));
        return;
    }
    const QStringList differ = LocoInfo::changedKeys(*m_layout, a->values, b->values);
    const bool all = m_all->isChecked();
    QFont bold = m_table->font();
    bold.setBold(true);
    for (const LocoInfo::Field &field : m_layout->fields()) {
        if (field.isCrc) {
            continue;
        }
        const bool differs = differ.contains(field.key);
        if (!differs && !all) {
            continue;
        }
        const QString format = m_presentation->formats.value(field.key);
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        QString label = field.name;
        if (!field.section.isEmpty()) {
            label = field.section + QStringLiteral(" › ") + field.name;
        }
        auto *name = new QTableWidgetItem(label);
        name->setData(Qt::UserRole, field.key);
        QString tip = field.key;
        const QString note = m_presentation->notes.value(field.key);
        if (!note.isEmpty()) {
            tip += QLatin1Char('\n') + note;
        }
        if (a->locked.contains(field.key) || b->locked.contains(field.key)) {
            tip += QLatin1Char('\n') + tr("Locked in %1")
                                          .arg(a->locked.contains(field.key) && b->locked.contains(field.key)
                                                   ? tr("both")
                                                   : (a->locked.contains(field.key) ? a->name : b->name));
        }
        name->setToolTip(tip);
        auto *group = new QTableWidgetItem(m_presentation->groupOf(field.key));
        group->setForeground(FlasherStyle::muted());
        auto *valueA = new QTableWidgetItem(LocoInfo::formatValue(field, a->values.value(field.key), format));
        auto *valueB = new QTableWidgetItem(LocoInfo::formatValue(field, b->values.value(field.key), format));
        if (differs && all) {
            // Among all fields, the differing ones stand out.
            for (QTableWidgetItem *item : { name, valueA, valueB }) {
                item->setFont(bold);
            }
            name->setText(QStringLiteral("● ") + label);
        }
        m_table->setItem(row, ColumnField, name);
        m_table->setItem(row, ColumnGroup, group);
        m_table->setItem(row, ColumnA, valueA);
        m_table->setItem(row, ColumnB, valueB);
    }
    m_table->setHorizontalHeaderLabels({ tr("Field"), tr("Group"), a->name, b->name });

    const int total = m_layout->fields().size() - 1;   // the CRC is computed, not compared
    if (a->name == b->name) {
        m_summary->setText(tr("A and B are the same configuration"));
    } else if (differ.isEmpty()) {
        m_summary->setText(tr("All %1 fields are the same: %2 and %3 send the same bytes")
                               .arg(total).arg(a->name, b->name));
    } else {
        m_summary->setText(tr("%n of %1 fields differ", nullptr, differ.size()).arg(total));
    }
}

int LocoConfigCompareDialog::rowCount() const
{
    return m_table->rowCount();
}

QString LocoConfigCompareDialog::fieldAt(int row) const
{
    const QTableWidgetItem *item = m_table->item(row, ColumnField);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QString LocoConfigCompareDialog::valueAt(int row, bool b) const
{
    const QTableWidgetItem *item = m_table->item(row, b ? ColumnB : ColumnA);
    return item ? item->text() : QString();
}

QString LocoConfigCompareDialog::summary() const
{
    return m_summary->text();
}

QString LocoConfigCompareDialog::asText() const
{
    QStringList lines;
    lines << tr("Field\tGroup\t%1\t%2").arg(m_a->currentText(), m_b->currentText());
    for (int row = 0; row < m_table->rowCount(); ++row) {
        lines << QStringLiteral("%1\t%2\t%3\t%4")
                     .arg(fieldAt(row), m_table->item(row, ColumnGroup)->text(), valueAt(row, false),
                          valueAt(row, true));
    }
    lines << summary();
    return lines.join(QLatin1Char('\n'));
}
