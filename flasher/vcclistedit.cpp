#include "vcclistedit.h"

#include "flashercore.h"
#include "flasherstyle.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QCheckBox>
#include <QHash>
#include <QHBoxLayout>
#include <QMenu>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QToolButton>
#include <QVBoxLayout>

VccListEdit::VccListEdit(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(6);

    m_rowsLayout = new QVBoxLayout();
    m_rowsLayout->setSpacing(4);
    outer->addLayout(m_rowsLayout);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_addButton = new QPushButton(tr("+ Add VCC"), this);
    m_addButton->setObjectName(QStringLiteral("vccAddButton"));
    m_addButton->setToolTip(tr("Add the next VCC (the address after the last one).\n"
                               "Several VCCs are flashed at once, each with its own transfer."));
    connect(m_addButton, &QPushButton::clicked, this, &VccListEdit::addNext);
    buttons->addWidget(m_addButton);
    m_rangeButton = new QPushButton(tr("Add range…"), this);
    m_rangeButton->setObjectName(QStringLiteral("vccRangeButton"));
    m_rangeButton->setToolTip(tr("Add several VCCs at once, e.g. 192.168.25.168-175"));
    connect(m_rangeButton, &QPushButton::clicked, this, &VccListEdit::askForRange);
    buttons->addWidget(m_rangeButton);

    // Session 300: the 2.6 addresses, loco by loco.
    m_locoButton = new QToolButton(this);
    m_locoButton->setObjectName(QStringLiteral("vccLocoButton"));
    m_locoButton->setText(tr("Loco addresses"));
    m_locoButton->setToolTip(tr("The 2.6 updater's addresses: each VCC controller of each loco.\n"
                                "Input, Output and Analog images go to VCC 1 controller 1."));
    m_locoButton->setPopupMode(QToolButton::InstantPopup);
    m_locoButton->setMinimumHeight(m_addButton->sizeHint().height());
    auto *menu = new QMenu(m_locoButton);
    for (int loco = 1; loco <= Flasher::locoCount(); ++loco) {
        QMenu *sub = menu->addMenu(tr("Loco %1").arg(loco));
        for (const Flasher::LocoController &controller : Flasher::locoControllers()) {
            if (controller.loco != loco) continue;
            QString text = tr("VCC %1 controller %2   %3").arg(controller.vcc).arg(controller.controller)
                               .arg(controller.ip.mid(controller.ip.lastIndexOf(QLatin1Char('.'))));
            if (controller.vcc == 1 && controller.controller == 1) {
                text += tr("   (card images)");
            }
            const QString ip = controller.ip;
            sub->addAction(text, this, [this, ip]() { pickAddress(ip); });
        }
        sub->addSeparator();
        sub->addAction(tr("All 4 controllers (VCC image)"), this, [this, loco]() { pickLoco(loco); });
    }
    m_locoButton->setMenu(menu);
    m_locoButton->hide();
    buttons->addStretch(1);
    outer->addLayout(buttons);
    // On a line of its own: beside the other two it cut "Add range…" in the
    // flasher's left column.
    auto *locoLine = new QHBoxLayout();
    locoLine->setSpacing(6);
    locoLine->addWidget(m_locoButton);
    locoLine->addStretch(1);
    outer->addLayout(locoLine);

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("vccListSummary"));
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(UiColor::mutedStyle());
    m_summary->hide();
    outer->addWidget(m_summary);

    addRow();
    UiColor::onThemeChange(this, [this]() { refresh(); });
}

// -----------------------------------------------------------------------------
//  Rows
// -----------------------------------------------------------------------------

void VccListEdit::addRow(const QString &ip, bool ticked)
{
    Row row;
    row.widget = new QWidget(this);
    // Session 300: two lines, the row and under it the loco controller.
    auto *outerRow = new QVBoxLayout(row.widget);
    outerRow->setContentsMargins(0, 0, 0, 0);
    outerRow->setSpacing(1);
    auto *layout = new QHBoxLayout();
    outerRow->addLayout(layout);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    row.tick = new QCheckBox(row.widget);
    row.tick->setChecked(ticked);
    row.tick->setToolTip(tr("Ticked: flashed in this run. Untick to leave this VCC out."));
    layout->addWidget(row.tick);

    row.edit = new QLineEdit(ip, row.widget);
    row.edit->setFont(UiStyle::monoFont());
    row.edit->setPlaceholderText(QStringLiteral("192.168.25.168"));
    row.edit->setMinimumHeight(30);
    layout->addWidget(row.edit, 1);

    row.remove = new QToolButton(row.widget);
    row.remove->setText(QStringLiteral("×"));
    row.remove->setToolTip(tr("Remove this VCC"));
    row.remove->setAutoRaise(true);
    layout->addWidget(row.remove);

    // Session 300: under the row, which loco controller the address is.
    row.name = new QLabel(row.widget);
    row.name->setStyleSheet(UiColor::mutedStyle());
    row.name->setContentsMargins(row.tick->sizeHint().width() + 6, 0, 0, 0);
    row.name->hide();
    outerRow->addWidget(row.name);

    QWidget *key = row.widget;
    connect(row.tick, &QCheckBox::toggled, this, [this]() { refresh(); });
    connect(row.edit, &QLineEdit::textChanged, this, [this]() { refresh(); });
    connect(row.remove, &QToolButton::clicked, this, [this, key]() {
        for (int index = 0; index < m_rows.size(); ++index) {
            if (m_rows.at(index).widget == key) {
                removeRow(index);
                return;
            }
        }
    });

    m_rowsLayout->addWidget(row.widget);
    m_rows.append(row);
    refresh();
}

void VccListEdit::removeRow(int index)
{
    if (index < 0 || index >= m_rows.size()) {
        return;
    }
    // Always one row: the last one is emptied instead.
    if (m_rows.size() == 1) {
        m_rows[0].edit->clear();
        m_rows[0].tick->setChecked(true);
        return;
    }
    QWidget *gone = m_rows.at(index).widget;
    m_rows.remove(index);
    gone->hide();
    gone->deleteLater();
    refresh();
}

int VccListEdit::tickedCount() const
{
    int count = 0;
    for (const Row &row : m_rows) {
        count += row.tick->isChecked() && !row.edit->text().trimmed().isEmpty() ? 1 : 0;
    }
    return count;
}

QLineEdit *VccListEdit::edit(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).edit : nullptr;
}

QCheckBox *VccListEdit::tick(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).tick : nullptr;
}

bool VccListEdit::rowIsMarked(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows.at(row).marked;
}

QString VccListEdit::summary() const
{
    return m_summary->isVisibleTo(const_cast<VccListEdit *>(this)) ? m_summary->text() : QString();
}

// -----------------------------------------------------------------------------
//  Text form
// -----------------------------------------------------------------------------

void VccListEdit::setText(const QString &text)
{
    m_quiet = true;
    for (const Row &row : m_rows) {
        row.widget->hide();
        row.widget->deleteLater();
    }
    m_rows.clear();
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
                                          Qt::SkipEmptyParts
#else
                                          QString::SkipEmptyParts
#endif
    );
    for (const QString &token : tokens) {
        // A range opens into one row per VCC; anything else is one row.
        const Flasher::VccTargetList parsed = Flasher::parseVccTargets(token);
        if (token.contains(QLatin1Char('-')) && parsed.ok()) {
            for (const Flasher::VccTarget &target : parsed.targets) {
                addRow(target.text(0));
            }
        } else {
            addRow(token);
        }
    }
    if (m_rows.isEmpty()) {
        addRow();
    }
    m_quiet = false;
    refresh();
}

QString VccListEdit::text() const
{
    QStringList ticked;
    for (const Row &row : m_rows) {
        const QString ip = row.edit->text().trimmed();
        if (row.tick->isChecked() && !ip.isEmpty()) {
            ticked << ip;
        }
    }
    return ticked.join(QStringLiteral(", "));
}

// -----------------------------------------------------------------------------
//  Adding
// -----------------------------------------------------------------------------

// The address after the last good row: .169 after .168, its port kept.
QString VccListEdit::nextAddress() const
{
    for (int index = m_rows.size() - 1; index >= 0; --index) {
        const Flasher::VccTargetList one = Flasher::parseVccTargets(m_rows.at(index).edit->text());
        if (!one.ok() || one.targets.size() != 1) {
            continue;
        }
        const Flasher::VccTarget &target = one.targets.first();
        const int dot = target.ip.lastIndexOf(QLatin1Char('.'));
        const int last = target.ip.mid(dot + 1).toInt();
        if (last >= 254) {
            return QString();
        }
        Flasher::VccTarget next = target;
        next.ip = target.ip.left(dot + 1) + QString::number(last + 1);
        return next.text(0);
    }
    return QString();
}

void VccListEdit::addNext()
{
    if (m_rows.size() >= Flasher::kMaxParallelVccs) {
        return;
    }
    // An empty last row is filled rather than another added under it.
    if (m_rows.last().edit->text().trimmed().isEmpty()) {
        m_rows.last().edit->setFocus();
        return;
    }
    addRow(nextAddress());
    m_rows.last().edit->setFocus();
}

bool VccListEdit::addRange(const QString &range, QString *error)
{
    const Flasher::VccTargetList parsed = Flasher::parseVccTargets(range);
    if (!parsed.ok()) {
        if (error) *error = parsed.error;
        return false;
    }
    int kept = 0;
    for (const Row &row : m_rows) {
        kept += row.edit->text().trimmed().isEmpty() ? 0 : 1;
    }
    if (kept + parsed.targets.size() > Flasher::kMaxParallelVccs) {
        if (error) {
            *error = tr("That makes %1 VCCs; at most %2 are flashed at once.")
                         .arg(kept + parsed.targets.size()).arg(Flasher::kMaxParallelVccs);
        }
        return false;
    }
    m_quiet = true;
    // Blank rows give way to the range.
    for (int index = m_rows.size() - 1; index >= 0 && m_rows.size() > 0; --index) {
        if (m_rows.at(index).edit->text().trimmed().isEmpty()) {
            QWidget *gone = m_rows.at(index).widget;
            m_rows.remove(index);
            gone->hide();
            gone->deleteLater();
        }
    }
    for (const Flasher::VccTarget &target : parsed.targets) {
        addRow(target.text(0));
    }
    m_quiet = false;
    refresh();
    return true;
}

void VccListEdit::askForRange()
{
    QString start = nextAddress();
    if (start.isEmpty()) start = QStringLiteral("192.168.25.168");
    if (start.contains(QLatin1Char(':'))) start = start.left(start.indexOf(QLatin1Char(':')));
    bool ok = false;
    const QString range = QInputDialog::getText(
        this, tr("Add range"),
        tr("First address, a dash, and the last number.\nFor example 192.168.25.168-175 adds 8 VCCs."),
        QLineEdit::Normal, start + QLatin1Char('-'), &ok);
    if (!ok || range.trimmed().isEmpty()) {
        return;
    }
    QString error;
    if (!addRange(range.trimmed(), &error)) {
        QMessageBox::warning(this, tr("Add range"), error);
    }
}

// -----------------------------------------------------------------------------
//  Session 300: loco addresses
// -----------------------------------------------------------------------------

void VccListEdit::setLocoPicker(bool shown)
{
    m_locoButton->setVisible(shown);
    m_locoButton->setProperty("shown", shown);
    refresh();
}

bool VccListEdit::locoPickerShown() const
{
    return m_locoButton->property("shown").toBool();
}

void VccListEdit::pickAddress(const QString &ip)
{
    for (const Row &row : m_rows) {
        if (row.edit->text().trimmed() == ip) {
            row.tick->setChecked(true);
            return;   // already listed
        }
    }
    if (m_rows.size() == 1) {
        m_rows[0].edit->setText(ip);
        m_rows[0].tick->setChecked(true);
        return;
    }
    if (m_rows.last().edit->text().trimmed().isEmpty()) {
        m_rows.last().edit->setText(ip);
        return;
    }
    if (m_rows.size() < Flasher::kMaxParallelVccs) {
        addRow(ip);
    }
}

void VccListEdit::pickLoco(int loco)
{
    QStringList ips;
    for (const Flasher::LocoController &controller : Flasher::locoControllers()) {
        if (controller.loco == loco) ips << controller.ip;
    }
    setText(ips.join(QStringLiteral(", ")));
}

QString VccListEdit::rowLabel(int row) const
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).name->isHidden()) {
        return QString();
    }
    return m_rows.at(row).name->text();
}

// -----------------------------------------------------------------------------
//  Marks, summary, buttons
// -----------------------------------------------------------------------------

void VccListEdit::refresh()
{
    if (m_quiet) {
        return;
    }
    // A row is wrong when it is not one address, or the same VCC as another row.
    QHash<QString, int> seen;
    QVector<QString> keys(m_rows.size());
    QVector<QString> reasons(m_rows.size());
    for (int index = 0; index < m_rows.size(); ++index) {
        const QString text = m_rows.at(index).edit->text().trimmed();
        if (text.isEmpty()) {
            continue;
        }
        const Flasher::VccTargetList one = Flasher::parseVccTargets(text);
        if (!one.ok()) {
            reasons[index] = tr("Not a valid IPv4 address");
        } else if (one.targets.size() != 1) {
            reasons[index] = tr("One address per row: Add range… adds several");
        } else {
            keys[index] = one.targets.first().text(0);
            seen[keys[index]] += 1;
        }
    }
    const QColor danger = FlasherStyle::danger();
    for (int index = 0; index < m_rows.size(); ++index) {
        Row &row = m_rows[index];
        if (reasons.at(index).isEmpty() && !keys.at(index).isEmpty() && seen.value(keys.at(index)) > 1) {
            reasons[index] = tr("Listed twice: one transfer per VCC");
        }
        row.marked = !reasons.at(index).isEmpty();
        if (row.marked) {
            row.edit->setStyleSheet(QStringLiteral("QLineEdit { color:%1; border:1px solid %1; border-radius:4px; }")
                                        .arg(danger.name()));
            row.edit->setToolTip(reasons.at(index));
        } else if (!row.tick->isChecked()) {
            row.edit->setStyleSheet(UiColor::mutedStyle());
            row.edit->setToolTip(tr("Left out of this run (unticked)"));
        } else {
            row.edit->setStyleSheet(QString());
            row.edit->setToolTip(tr("VCC address. a.b.c.d:port sets a port for this VCC only."));
        }
    }

    // Session 300: label each row with its loco controller (2.6 only).
    for (Row &row : m_rows) {
        const Flasher::LocoController controller = Flasher::locoControllerAt(row.edit->text());
        const bool known = locoPickerShown() && controller.loco != 0 && !row.marked;
        row.name->setText(known ? controller.name() : QString());
        row.name->setVisible(known);
    }

    // The summary, once there is more than one row.
    const int ticked = tickedCount();
    if (m_rows.size() > 1) {
        if (ticked == 0) {
            m_summary->setText(tr("No VCC ticked"));
        } else if (ticked == 1) {
            m_summary->setText(tr("1 of %1 VCCs ticked").arg(m_rows.size()));
        } else {
            m_summary->setText(tr("%1 of %2 VCCs ticked — flashed at once").arg(ticked).arg(m_rows.size()));
        }
        m_summary->show();
    } else {
        m_summary->hide();
    }
    const bool room = m_rows.size() < Flasher::kMaxParallelVccs;
    m_addButton->setEnabled(room);
    m_rangeButton->setEnabled(room);
    m_addButton->setToolTip(room ? tr("Add the next VCC (the address after the last one).\n"
                                      "Several VCCs are flashed at once, each with its own transfer.")
                                 : tr("At most %1 VCCs are flashed at once").arg(Flasher::kMaxParallelVccs));
    emit changed();
}
