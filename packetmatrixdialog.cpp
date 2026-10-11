#include "packetmatrixdialog.h"

#include "packetsequencedialog.h"
#include "settings.h"
#include "statusline.h"
#include "uicolors.h"
#include "windowgeometry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace PacketMatrix;

namespace {

QString firstLine(const QString &s)
{
    return s.section(QLatin1Char('\n'), 0, 0).simplified();
}

} // namespace

PacketMatrixDialog::PacketMatrixDialog(const TestCatalog::Case &caseInfo, const QVector<TestCatalog::Case> &ops,
                                       QWidget *parent)
    : QDialog(parent)
    , m_case(caseInfo)
    , m_ops(ops)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Packet Matrix — %1").arg(caseInfo.id));
    WindowGeometry::makeResizableWindow(this);
    resize(1080, 640);

    auto *what = new QLabel(tr("<b>%1</b> %2 · %3 operations: one preset each")
                                .arg(caseInfo.id, firstLine(caseInfo.title).toHtmlEscaped())
                                .arg(ops.size()), this);
    what->setWordWrap(true);

    m_basePath = new QLineEdit(this);
    m_basePath->setReadOnly(true);
    m_basePath->setPlaceholderText(tr("a Packet Maker preset of the packet this case tests"));
    auto *baseBtn = new QPushButton(tr("Choose…"), this);
    baseBtn->setToolTip(tr("Every preset made here is this one with the one field changed: make it in the "
                           "Packet Maker with valid values for everything else, and Save"));
    connect(baseBtn, &QPushButton::clicked, this, [this]() {
        const QString start = m_basePath->text().isEmpty() ? Settings::matrixLastBase() : m_basePath->text();
        const QString path = QFileDialog::getOpenFileName(this, tr("Base preset"), start,
                                                          tr("Packet preset (*.packet.json *.json);;All files (*)"));
        if (!path.isEmpty()) setBasePreset(path);
    });
    m_baseInfo = new QLabel(this);
    m_baseInfo->setWordWrap(true);
    auto *baseRow = new QHBoxLayout;
    baseRow->addWidget(m_basePath, 1);
    baseRow->addWidget(baseBtn);

    m_fieldBox = new QComboBox(this);
    m_fieldBox->setToolTip(tr("The field each operation sets. Taken from the SIF's text when the packet has a field "
                              "of that name"));
    connect(m_fieldBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_filling) fillRows();
    });

    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels({ QString(), tr("Clause"), tr("The SIF sends"), tr("As printed"), tr("Value"),
                                         tr("Reading") });
    m_table->horizontalHeaderItem(ColValue)->setToolTip(tr("What the preset gets. Edit it: 8, 0b1000 or 0x8"));
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColSends, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(ColReading, QHeaderView::Stretch);
    m_table->setColumnWidth(ColSends, fontMetrics().horizontalAdvance(QStringLiteral("Stationary KAVACH should send a pac")));
    m_table->setWordWrap(false);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (!m_filling && it->column() == ColValue) checkRow(it->row());
    });

    m_folder = new QLineEdit(this);
    m_folder->setText(Settings::matrixLastFolder());
    m_folder->setPlaceholderText(tr("where the presets and the sequence go"));
    auto *folderBtn = new QPushButton(tr("Choose…"), this);
    connect(folderBtn, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Folder for the presets"), m_folder->text());
        if (!d.isEmpty()) setFolder(d);
    });
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(folderBtn);
    m_delay = new QSpinBox(this);
    m_delay->setRange(0, 600000);
    m_delay->setSingleStep(100);
    m_delay->setValue(1000);
    m_delay->setSuffix(tr(" ms"));
    m_delay->setToolTip(tr("The sequence's gap after each preset"));
    m_overwrite = new QCheckBox(tr("Replace files already there"), this);

    auto *form = new QFormLayout;
    form->addRow(tr("Base preset:"), baseRow);
    form->addRow(QString(), m_baseInfo);
    form->addRow(tr("Field:"), m_fieldBox);

    auto *outForm = new QFormLayout;
    outForm->addRow(tr("Folder:"), folderRow);
    auto *optRow = new QHBoxLayout;
    optRow->addWidget(m_delay);
    optRow->addSpacing(16);
    optRow->addWidget(m_overwrite);
    optRow->addStretch(1);
    outForm->addRow(tr("Delay after each:"), optRow);

    m_make = new QPushButton(tr("Make presets"), this);
    m_make->setDefault(true);
    connect(m_make, &QPushButton::clicked, this, [this]() { makePresets(); });
    m_openSeq = new QPushButton(tr("Open the sequence"), this);
    m_openSeq->setEnabled(false);
    m_openSeq->setToolTip(tr("Load the sequence just made into Packet Sequence, to send it"));
    connect(m_openSeq, &QPushButton::clicked, this, [this]() {
        auto *sq = new PacketSequenceDialog(parentWidget());
        sq->setAttribute(Qt::WA_DeleteOnClose);
        sq->loadSequence(m_seqPath);
        sq->show();
        sq->raise();
    });
    auto *close = new QPushButton(tr("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(m_make);
    buttons->addWidget(m_openSeq);
    buttons->addWidget(close);

    m_status = new StatusLine(this);
    auto *root = new QVBoxLayout(this);
    root->addWidget(what);
    root->addLayout(form);
    root->addWidget(m_table, 1);
    root->addLayout(outForm);
    root->addWidget(m_status);
    root->addLayout(buttons);

    const QString last = Settings::matrixLastBase();
    if (!last.isEmpty() && QFileInfo::exists(last)) setBasePreset(last);
    else fillRows();
    if (!m_haveBase) m_status->state(tr("Choose a base preset: the presets are it with one field changed."));
}

bool PacketMatrixDialog::setBasePreset(const QString &path)
{
    PacketPreset p;
    QString err;
    if (!PacketPreset::load(path, &p, &err)) {
        m_status->fail(tr("Not a preset: %1").arg(err));
        return false;
    }
    m_base = p;
    m_haveBase = true;
    m_basePath->setText(QDir::toNativeSeparators(path));
    Settings::setMatrixLastBase(path);
    m_fields = fieldsOf(m_builder.encoder(), p.captype);
    m_baseInfo->setText(tr("%1 packet, %2 header fields, %3 sub-packet(s)")
                            .arg(p.captype.toUpper()).arg(p.header.size()).arg(p.subs.size()));

    // The field the SIF names most, when the packet has it.
    QHash<QString, int> named;
    for (const TestCatalog::Case &op : m_ops) {
        QString f;
        if (findValue(op.input, &f, nullptr)) named[f]++;
    }
    QString best;
    for (const FieldRef &f : m_fields)
        if (named.value(f.name) > named.value(best)) best = f.name;
    m_filling = true;
    m_fieldBox->clear();
    int pick = -1;
    for (int i = 0; i < m_fields.size(); ++i) {
        m_fieldBox->addItem(tr("%1 (%2 bits)").arg(m_fields.at(i).label()).arg(m_fields.at(i).bits), m_fields.at(i).label());
        if (pick < 0 && m_fields.at(i).name == best) pick = i;
    }
    m_fieldBox->setCurrentIndex(pick < 0 ? 0 : pick);
    m_filling = false;
    fillRows();
    if (m_fields.isEmpty()) m_status->fail(tr("The schema has no %1 packet to build").arg(p.captype));
    else if (best.isEmpty()) m_status->warn(tr("The SIF's text names no field of this packet: pick the field"));
    else m_status->clear();
    return true;
}

bool PacketMatrixDialog::setField(const QString &label)
{
    const int i = m_fieldBox->findData(label);
    if (i < 0) return false;
    m_fieldBox->setCurrentIndex(i);
    return true;
}

QString PacketMatrixDialog::field() const
{
    return m_fieldBox->currentData().toString();
}

void PacketMatrixDialog::setFolder(const QString &path)
{
    m_folder->setText(QDir::toNativeSeparators(path));
}

void PacketMatrixDialog::setOverwrite(bool on)
{
    m_overwrite->setChecked(on);
}

const FieldRef *PacketMatrixDialog::currentField() const
{
    const QString label = field();
    for (const FieldRef &f : m_fields)
        if (f.label() == label) return &f;
    return nullptr;
}

void PacketMatrixDialog::fillRows()
{
    const FieldRef *f = currentField();
    m_filling = true;
    m_table->setRowCount(m_ops.size());
    // what the other rows print for this field: a binary-counting case reads binary
    QStringList siblings;
    for (const TestCatalog::Case &op : m_ops) {
        QString name, printed;
        if (f && findValue(op.input, &name, &printed) && name == f->name) siblings << printed;
    }
    for (int r = 0; r < m_ops.size(); ++r) {
        const TestCatalog::Case &op = m_ops.at(r);
        QString name, printed;
        const bool found = findValue(op.input, &name, &printed);
        auto *on = new QTableWidgetItem;
        on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        auto *clause = new QTableWidgetItem(op.id);
        clause->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        auto *sends = new QTableWidgetItem(firstLine(op.input));
        sends->setToolTip(op.input.left(800));
        sends->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        auto *pr = new QTableWidgetItem(found ? QStringLiteral("%1 %2").arg(name, printed) : QString());
        pr->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        auto *value = new QTableWidgetItem;
        auto *reading = new QTableWidgetItem;
        reading->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        bool tick = false;
        QString how;
        QColor colour = palette().color(QPalette::Text);
        if (!f) {
            how = tr("choose a base preset");
        } else if (!found) {
            how = tr("no value in the text: type one to make this row");
            colour = UiColor::muted();
        } else if (name != f->name) {
            how = tr("the SIF sets %1 here, not %2").arg(name, f->name);
            colour = UiColor::muted();
        } else if (!senderOf(op.input).isEmpty() && !senderOfPacket(m_base.captype).isEmpty()
                   && senderOf(op.input) != senderOfPacket(m_base.captype)) {
            how = tr("sent by the %1; this base is a %2 packet (%3)")
                      .arg(senderOf(op.input), senderOfPacket(m_base.captype), m_base.captype.toUpper());
            colour = UiColor::muted();
        } else {
            const Reading rd = read(printed, f->bits, siblings);
            how = rd.sure ? rd.how : tr("check: %1").arg(rd.how);
            if (!rd.sure) colour = UiColor::warning();
            if (rd.ok) {
                value->setText(QString::number(rd.value));
                reading->setData(Qt::UserRole, rd.value);
                reading->setData(Qt::UserRole + 1, how);
                reading->setData(Qt::UserRole + 2, rd.sure);
            }
            tick = rd.ok;
        }
        reading->setText(how);
        reading->setToolTip(how);
        if (f && found && name == f->name) {
            const Reading rd = read(printed, f->bits, siblings);
            if (!rd.why.isEmpty()) reading->setToolTip(tr("%1: %2. Edit the value if the SIF means otherwise")
                                                           .arg(rd.how, rd.why));
        }
        reading->setForeground(colour);
        on->setCheckState(tick ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(r, ColOn, on);
        m_table->setItem(r, ColClause, clause);
        m_table->setItem(r, ColSends, sends);
        m_table->setItem(r, ColPrinted, pr);
        m_table->setItem(r, ColValue, value);
        m_table->setItem(r, ColReading, reading);
        if (tick) {
            m_filling = false;
            checkRow(r);        // a value that does not fit the field is refused here
            m_filling = true;
        }
    }
    m_filling = false;
}

void PacketMatrixDialog::checkRow(int row)
{
    const FieldRef *f = currentField();
    QTableWidgetItem *value = m_table->item(row, ColValue);
    QTableWidgetItem *reading = m_table->item(row, ColReading);
    QTableWidgetItem *on = m_table->item(row, ColOn);
    if (!f || !value || !reading || !on) return;
    const QVariant sif = reading->data(Qt::UserRole);       // the value read off the SIF, if any
    m_filling = true;
    qint64 v = 0;
    if (value->text().trimmed().isEmpty()) {
        on->setCheckState(Qt::Unchecked);
    } else if (!parseTyped(value->text(), &v)) {
        on->setCheckState(Qt::Unchecked);
        reading->setText(tr("\"%1\" is not a number (8, 0b1000, 0x8)").arg(value->text()));
        reading->setForeground(UiColor::error());
    } else if (!fits(*f, v)) {
        on->setCheckState(Qt::Unchecked);
        reading->setText(tr("%1 does not fit %2's %3 bits").arg(v).arg(f->name).arg(f->bits));
        reading->setForeground(UiColor::error());
    } else if (sif.isValid() && sif.toLongLong() == v) {
        on->setCheckState(Qt::Checked);
        const bool sure = reading->data(Qt::UserRole + 2).toBool();
        reading->setText(reading->data(Qt::UserRole + 1).toString());
        reading->setForeground(sure ? palette().color(QPalette::Text) : UiColor::warning());
    } else {
        on->setCheckState(Qt::Checked);
        reading->setText(sif.isValid() ? tr("typed: %1 (the SIF reads %2)").arg(v).arg(sif.toLongLong())
                                       : tr("typed: %1").arg(v));
        reading->setForeground(palette().color(QPalette::Text));
    }
    if (!reading->text().startsWith(tr("check"))) reading->setToolTip(reading->text());
    m_filling = false;
}

QHash<QString, QString> PacketMatrixDialog::makePresets()
{
    QHash<QString, QString> made;
    const FieldRef *f = currentField();
    if (!m_haveBase || !f) {
        m_status->fail(tr("Choose a base preset first"));
        return made;
    }
    const QString folder = QDir::fromNativeSeparators(m_folder->text().trimmed());
    if (folder.isEmpty() || !QDir(folder).exists()) {
        m_status->fail(tr("Choose a folder that exists for the presets"));
        return made;
    }
    QVector<Row> rows;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (m_table->item(r, ColOn)->checkState() != Qt::Checked) continue;
        Row row;
        row.clause = m_table->item(r, ColClause)->text();
        if (!parseTyped(m_table->item(r, ColValue)->text(), &row.value)) continue;
        rows << row;
    }
    if (rows.isEmpty()) {
        m_status->warn(tr("No row is ticked"));
        return made;
    }
    Settings::setMatrixLastFolder(folder);
    m_outcomes = generate(m_base, *f, rows, folder, m_case.id, m_delay->value(), m_overwrite->isChecked(), m_builder,
                          &m_seqPath);
    QStringList refused;
    for (const Outcome &o : m_outcomes) {
        for (int r = 0; r < m_table->rowCount(); ++r) {
            if (m_table->item(r, ColClause)->text() != o.clause) continue;
            QTableWidgetItem *reading = m_table->item(r, ColReading);
            m_filling = true;
            reading->setText(o.ok ? tr("written: %1").arg(QFileInfo(o.file).fileName()) : tr("refused: %1").arg(o.why));
            reading->setToolTip(o.ok ? QDir::toNativeSeparators(o.file) : o.why);
            reading->setForeground(o.ok ? UiColor::ok() : UiColor::error());
            m_filling = false;
        }
        if (o.ok) made.insert(o.clause, o.file);
        else refused << QStringLiteral("%1 (%2)").arg(o.clause, o.why);
    }
    m_openSeq->setEnabled(!m_seqPath.isEmpty());
    const QString where = QDir::toNativeSeparators(folder);
    if (made.isEmpty())
        m_status->fail(tr("Nothing written: %1").arg(refused.join(QStringLiteral("; "))));
    else if (!refused.isEmpty())
        m_status->warn(tr("Wrote %1 presets and %2 to %3. Refused: %4")
                           .arg(made.size()).arg(QFileInfo(m_seqPath).fileName(), where, refused.join(QStringLiteral("; "))));
    else
        m_status->ok(tr("Wrote %1 presets and %2 to %3, each built and verified")
                         .arg(made.size()).arg(QFileInfo(m_seqPath).fileName(), where));
    if (!made.isEmpty()) emit presetsMade(made);
    return made;
}
