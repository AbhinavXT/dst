#include "linfofielddialog.h"

#include "uicolors.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QString fieldLabel(const LocoInfo::Field &field)
{
    return QStringLiteral("%1   (%2)").arg(field.key, field.typeText());
}

QString firmwareWarning()
{
    return QObject::tr("<b>The firmware and the loco_config tool must change the same way</b>, at the same "
                       "place, before a loco is sent this layout; then run "
                       "<code>lococonfig/sync_linfo.py</code> against the tool (ADDING_A_FIELD.md).");
}

}  // namespace

// =============================================================================
//  Add
// =============================================================================

LinfoAddFieldDialog::LinfoAddFieldDialog(const LocoInfo::Layout &layout, const QStringList &groups,
                                         const QString &schemaPath, const QString &afterKey, QWidget *parent)
    : QDialog(parent), m_layout(layout), m_schemaPath(schemaPath)
{
    setWindowTitle(tr("Add a field to LOCO_INFO"));
    setObjectName(QStringLiteral("linfoAddFieldDialog"));

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("linfoAddName"));
    m_name->setPlaceholderText(tr("the C member's name, e.g. max_brake_cylinder_pressure"));

    m_type = new QComboBox(this);
    m_type->setObjectName(QStringLiteral("linfoAddType"));
    m_type->addItem(QStringLiteral("uint8_t"), int(LocoInfo::Kind::U8));
    m_type->addItem(QStringLiteral("uint16_t"), int(LocoInfo::Kind::U16));
    m_type->addItem(QStringLiteral("uint32_t"), int(LocoInfo::Kind::U32));
    m_type->addItem(QStringLiteral("float"), int(LocoInfo::Kind::Float));
    m_type->addItem(QStringLiteral("char[N]"), int(LocoInfo::Kind::Char));
    m_count = new QSpinBox(this);
    m_count->setRange(2, 255);
    m_count->setValue(40);
    m_count->setPrefix(QStringLiteral("N = "));
    auto *typeRow = new QHBoxLayout;
    typeRow->addWidget(m_type, 1);
    typeRow->addWidget(m_count);

    m_after = new QComboBox(this);
    m_after->setObjectName(QStringLiteral("linfoAddAfter"));
    for (const LocoInfo::Field &f : layout.fields()) {
        if (!f.isCrc) m_after->addItem(fieldLabel(f), f.key);
    }
    int at = afterKey.isEmpty() ? m_after->count() - 1 : m_after->findData(afterKey);
    m_after->setCurrentIndex(at < 0 ? m_after->count() - 1 : at);
    m_after->setToolTip(tr("The new member goes right after this one, in the same section "
                           "(LOCO_INFO, static_brake_params or national_values)"));

    m_group = new QComboBox(this);
    m_group->setObjectName(QStringLiteral("linfoAddGroup"));
    m_group->setEditable(true);
    m_group->addItems(groups);
    m_group->setToolTip(tr("The editor's group; type a new name for a new group, leave empty for \"Other\""));
    if (const LocoInfo::Field *f = layout.field(m_after->currentData().toString())) {
        m_group->setCurrentText(f->group);
    }

    m_note = new QLineEdit(this);
    m_note->setPlaceholderText(tr("one line of help in the editor, e.g. kPa"));
    m_default = new QLineEdit(this);
    m_default->setObjectName(QStringLiteral("linfoAddDefault"));
    m_default->setPlaceholderText(tr("empty = 0; replaced by the tool's value when sync_linfo.py runs"));
    m_hex = new QCheckBox(tr("Show in hex"), this);

    auto *form = new QFormLayout;
    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("C type:"), typeRow);
    form->addRow(tr("After:"), m_after);
    form->addRow(tr("Group:"), m_group);
    form->addRow(tr("Note:"), m_note);
    form->addRow(tr("Default:"), m_default);
    form->addRow(QString(), m_hex);

    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("linfoAddPreview"));
    m_preview->setWordWrap(true);
    m_preview->setTextFormat(Qt::RichText);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_problem = new QLabel(this);
    m_problem->setWordWrap(true);
    m_problem->setStyleSheet(UiColor::errorStyle());

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_ok = buttons->addButton(tr("Add to kavach.xml"), QDialogButtonBox::AcceptRole);
    m_ok->setObjectName(QStringLiteral("linfoAddOk"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(m_preview);
    root->addWidget(m_problem);
    root->addWidget(buttons);
    resize(640, 0);

    for (QLineEdit *e : { m_name, m_note, m_default })
        connect(e, &QLineEdit::textChanged, this, &LinfoAddFieldDialog::refresh);
    connect(m_type, qOverload<int>(&QComboBox::currentIndexChanged), this, &LinfoAddFieldDialog::refresh);
    connect(m_count, qOverload<int>(&QSpinBox::valueChanged), this, &LinfoAddFieldDialog::refresh);
    connect(m_after, qOverload<int>(&QComboBox::currentIndexChanged), this, &LinfoAddFieldDialog::refresh);
    connect(m_group, &QComboBox::currentTextChanged, this, &LinfoAddFieldDialog::refresh);
    connect(m_hex, &QCheckBox::toggled, this, &LinfoAddFieldDialog::refresh);
    refresh();
}

LinfoEdit::NewField LinfoAddFieldDialog::field() const
{
    LinfoEdit::NewField f;
    f.name = m_name->text().trimmed();
    f.kind = LocoInfo::Kind(m_type->currentData().toInt());
    f.charCount = f.kind == LocoInfo::Kind::Char ? m_count->value() : 0;
    f.afterKey = m_after->currentData().toString();
    f.group = m_group->currentText().trimmed();
    f.note = m_note->text().trimmed();
    f.defaultText = m_default->text().trimmed();
    f.hex = m_hex->isChecked() && m_hex->isEnabled();
    return f;
}

QString LinfoAddFieldDialog::problemText() const { return m_problem->text(); }
QString LinfoAddFieldDialog::previewText() const { return m_preview->text(); }

void LinfoAddFieldDialog::refresh()
{
    const LocoInfo::Kind kind = LocoInfo::Kind(m_type->currentData().toInt());
    m_count->setEnabled(kind == LocoInfo::Kind::Char);
    m_hex->setEnabled(kind == LocoInfo::Kind::U8 || kind == LocoInfo::Kind::U16 || kind == LocoInfo::Kind::U32);

    const LinfoEdit::NewField f = field();
    const QString why = LinfoEdit::problem(m_layout, f);
    m_problem->setText(why);
    m_problem->setVisible(!why.isEmpty());
    m_ok->setEnabled(why.isEmpty());

    const int size = LinfoEdit::sizeOf(f);
    m_preview->setText(tr("Adds to <code>%1</code>:<br><code>%2</code><br><br>"
                          "LOCO_INFO grows %3 byte(s): %4 → %5. Saved configurations get the field at its "
                          "default when the window reopens.<br><br>%6")
                           .arg(m_schemaPath.toHtmlEscaped(),
                                LinfoEdit::element(f).toHtmlEscaped())
                           .arg(size)
                           .arg(m_layout.bodySize())
                           .arg(m_layout.bodySize() + size)
                           .arg(firmwareWarning()));
}

// =============================================================================
//  Remove
// =============================================================================

LinfoRemoveFieldDialog::LinfoRemoveFieldDialog(const LocoInfo::Layout &layout, const QString &schemaPath,
                                               const QString &key, const QHash<QString, QStringList> &lockedIn,
                                               QWidget *parent)
    : QDialog(parent), m_layout(layout), m_schemaPath(schemaPath), m_lockedIn(lockedIn)
{
    setWindowTitle(tr("Remove a field from LOCO_INFO"));
    setObjectName(QStringLiteral("linfoRemoveFieldDialog"));

    m_field = new QComboBox(this);
    m_field->setObjectName(QStringLiteral("linfoRemoveField"));
    for (const LocoInfo::Field &f : layout.fields()) {
        if (LinfoEdit::removeProblem(layout, f.key).isEmpty()) m_field->addItem(fieldLabel(f), f.key);
    }
    const int at = m_field->findData(key);
    if (at >= 0) m_field->setCurrentIndex(at);

    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("linfoRemoveInfo"));
    m_info->setWordWrap(true);
    m_info->setTextFormat(Qt::RichText);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_ok = buttons->addButton(tr("Remove from kavach.xml"), QDialogButtonBox::AcceptRole);
    m_ok->setObjectName(QStringLiteral("linfoRemoveOk"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout;
    form->addRow(tr("Field:"), m_field);
    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(m_info);
    root->addWidget(buttons);
    resize(620, 0);

    connect(m_field, qOverload<int>(&QComboBox::currentIndexChanged), this, &LinfoRemoveFieldDialog::refresh);
    refresh();
}

QString LinfoRemoveFieldDialog::key() const { return m_field->currentData().toString(); }
QString LinfoRemoveFieldDialog::infoText() const { return m_info->text(); }

void LinfoRemoveFieldDialog::refresh()
{
    const LocoInfo::Field *f = m_layout.field(key());
    m_ok->setEnabled(f != nullptr);
    if (f == nullptr) {
        m_info->setText(tr("No field can be removed."));
        return;
    }
    QString text = tr("Removes the line of <b>%1</b> (%2) from <code>%3</code>.<br>"
                      "LOCO_INFO shrinks %4 byte(s): %5 → %6, and every member after it moves up.")
                       .arg(f->key.toHtmlEscaped(), f->typeText(), m_schemaPath.toHtmlEscaped())
                       .arg(f->size)
                       .arg(m_layout.bodySize())
                       .arg(m_layout.bodySize() - f->size);
    text += tr("<br><br>Its value is dropped from every saved configuration when the window reopens.");
    const QStringList locked = m_lockedIn.value(f->key);
    if (!locked.isEmpty())
        text += tr(" It is <b>locked</b> in: %1.").arg(locked.join(QStringLiteral(", ")).toHtmlEscaped());
    text += tr("<br>Past sends in the history keep their bytes; they can no longer be loaded back into "
               "a configuration (a different LOCO_INFO size).<br><br>")
            + firmwareWarning();
    m_info->setText(text);
}
