#include "fieldindexdialog.h"

#include "schema/schemadecoder.h"
#include "uicolors.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QTableWidget>
#include <QVBoxLayout>

FieldIndexDialog::FieldIndexDialog(const Schema::Decoder *decoder,
                                   QWidget *parent)
    : QDialog(parent), m_decoder(decoder)
{
    setWindowTitle(tr("Field index — which packets carry what"));
    setObjectName(QStringLiteral("fieldIndexDialog"));
    resize(620, 460);

    auto *root = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Field or packet:")));
    m_filter = new QLineEdit;
    m_filter->setObjectName(QStringLiteral("fieldIndexFilter"));
    m_filter->setPlaceholderText(tr("FRAME_NUM, or slrp"));
    m_filter->setClearButtonEnabled(true);
    // Matching packets too, not just field names: "what does LSRP carry" is
    // the same question read from the other end, and it would be perverse to
    // have the table on screen and make the operator go elsewhere for it.
    top->addWidget(m_filter, 1);
    root->addLayout(top);

    m_sharedOnly = new QCheckBox(tr("Only fields carried by more than one packet"));
    m_sharedOnly->setObjectName(QStringLiteral("fieldIndexSharedOnly"));
    m_sharedOnly->setToolTip(
        tr("The ones where narrowing matters. A field in a single packet\n"
           "needs no narrowing; a field in five will show whichever packet\n"
           "spoke last unless the pin says which."));
    root->addWidget(m_sharedOnly);

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("fieldIndexTable"));
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        { tr("Field"), tr("In"), tr("Packets") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(m_table, 1);

    m_summary = new QLabel;
    m_summary->setObjectName(QStringLiteral("fieldIndexSummary"));
    root->addWidget(m_summary);

    connect(m_filter, &QLineEdit::textChanged, this,
            [this](const QString &) { applyFilter(); });
    connect(m_sharedOnly, &QCheckBox::toggled, this,
            [this](bool) { applyFilter(); });

    reload();
}

void FieldIndexDialog::setFilter(const QString &text)
{
    m_filter->setText(text);
    m_filter->selectAll();
    m_filter->setFocus();
}

void FieldIndexDialog::reload()
{
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    if (!m_decoder) {
        m_summary->setText(tr("No schema loaded."));
        return;
    }

    // Field name → the packets carrying it, in the schema's own terms: the
    // captype token where it differs from the packet name, because the token
    // is what a pin is narrowed on and what a capture line actually says.
    QMap<QString, QStringList> byField;
    for (const Schema::Decoder::PacketFields &pf : m_decoder->fieldsByCaptype()) {
        const QString label =
            (pf.captype.isEmpty()
             || pf.packet.compare(pf.captype, Qt::CaseInsensitive) == 0)
                ? pf.packet
                : QStringLiteral("%1 (%2)").arg(pf.packet, pf.captype);
        for (const QString &f : pf.fields) { byField[f] << label; }
    }

    m_table->setRowCount(byField.size());
    int row = 0;
    int shared = 0;
    for (auto it = byField.cbegin(); it != byField.cend(); ++it, ++row) {
        auto *name = new QTableWidgetItem(it.key());

        // Sorts numerically rather than as text, or 10 lands between 1 and 2.
        auto *count = new QTableWidgetItem;
        count->setData(Qt::DisplayRole, it.value().size());
        count->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (it.value().size() > 1) {
            ++shared;
            // The count is the whole point of the table, so the rows where
            // it is greater than one are the rows worth spotting.
            count->setForeground(UiColor::accent());
        }

        auto *packets = new QTableWidgetItem(it.value().join(QStringLiteral(", ")));
        packets->setToolTip(it.value().join(QLatin1Char('\n')));

        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, count);
        m_table->setItem(row, 2, packets);
    }

    m_table->setSortingEnabled(true);
    m_table->resizeColumnToContents(0);
    m_table->resizeColumnToContents(1);
    m_sharedOnly->setText(
        tr("Only the %1 fields carried by more than one packet").arg(shared));
    applyFilter();
}

void FieldIndexDialog::applyFilter()
{
    const QString needle = m_filter->text().trimmed();
    const bool sharedOnly = m_sharedOnly->isChecked();

    int shown = 0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const QTableWidgetItem *name    = m_table->item(r, 0);
        const QTableWidgetItem *count   = m_table->item(r, 1);
        const QTableWidgetItem *packets = m_table->item(r, 2);
        if (!name || !count || !packets) { continue; }

        bool keep = needle.isEmpty()
                    || name->text().contains(needle, Qt::CaseInsensitive)
                    || packets->text().contains(needle, Qt::CaseInsensitive);
        if (keep && sharedOnly && count->data(Qt::DisplayRole).toInt() < 2) {
            keep = false;
        }
        m_table->setRowHidden(r, !keep);
        if (keep) { ++shown; }
    }

    m_summary->setText(shown == m_table->rowCount()
                           ? tr("%1 field(s).").arg(shown)
                           : tr("%1 of %2 field(s).")
                                 .arg(shown).arg(m_table->rowCount()));
}
