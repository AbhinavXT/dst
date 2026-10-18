#include "schemaelementdialog.h"

#include "uicolors.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPoint>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

using Place = SchemaDoc::Document::Place;

namespace {

QString nameOf(const SchemaDoc::Document &doc, int node)
{
    const SchemaDoc::Node &n = doc.node(node);
    if (n.tag == QLatin1String("pad")) return QObject::tr("the %1-bit pad").arg(n.value(QStringLiteral("bits")));
    if (n.tag == QLatin1String("when")) return QObject::tr("the condition %1").arg(n.value(QStringLiteral("test")));
    if (n.tag == QLatin1String("note")) return QObject::tr("the note %1").arg(n.value(QStringLiteral("text")));
    const QString nm = n.value(QStringLiteral("name"));
    return nm.isEmpty() ? SchemaEdit::title(n.tag) : nm;
}

QString sizeText(int bits)
{
    if (bits < 0) return QObject::tr("variable");
    return bits % 8 == 0 ? QObject::tr("%1 B").arg(bits / 8) : QObject::tr("%1 bits").arg(bits);
}

}  // namespace

SchemaElementDialog::SchemaElementDialog(const SchemaEdit::Session &session, int anchor, QWidget *parent)
    : QDialog(parent), m_session(session)
{
    const SchemaDoc::Document &doc = session.doc();
    const int container = doc.container(anchor);
    setWindowTitle(tr("Add to %1").arg(doc.node(container).value(QStringLiteral("name"))));
    auto *lay = new QVBoxLayout(this);

    auto *top = new QFormLayout;
    m_kind = new QComboBox(this);
    m_kind->addItem(tr("Field — a value shown as a row"), QStringLiteral("field"));
    m_kind->addItem(tr("Pad — bits read and discarded"), QStringLiteral("pad"));
    m_kind->addItem(tr("Note — a label row, no bits"), QStringLiteral("note"));
    top->addRow(tr("Add"), m_kind);
    m_where = new QComboBox(this);
    auto addWhere = [this](const QString &text, int a, Place p) {
        m_where->addItem(text, QVariant::fromValue(QPoint(a, int(p))));
    };
    if (anchor == container) {
        addWhere(tr("At the end of %1").arg(nameOf(doc, anchor)), anchor, Place::LastChild);
        addWhere(tr("At the start of %1").arg(nameOf(doc, anchor)), anchor, Place::FirstChild);
    } else {
        addWhere(tr("After %1").arg(nameOf(doc, anchor)), anchor, Place::After);
        addWhere(tr("Before %1").arg(nameOf(doc, anchor)), anchor, Place::Before);
        const QString tag = doc.node(anchor).tag;
        if (tag == QLatin1String("when") || tag == QLatin1String("repeat") || tag == QLatin1String("group")) {
            addWhere(tr("Inside %1, first").arg(nameOf(doc, anchor)), anchor, Place::FirstChild);
            addWhere(tr("Inside %1, last").arg(nameOf(doc, anchor)), anchor, Place::LastChild);
        }
        addWhere(tr("At the end of %1").arg(nameOf(doc, container)), container, Place::LastChild);
    }
    top->addRow(tr("Where"), m_where);
    lay->addLayout(top);

    // Field.
    m_fieldBox = new QWidget(this);
    auto *ff = new QFormLayout(m_fieldBox);
    ff->setContentsMargins(0, 0, 0, 0);
    m_name = new QLineEdit(m_fieldBox);
    m_name->setPlaceholderText(tr("e.g. WEATHER_CODE"));
    ff->addRow(tr("Name"), m_name);
    m_type = new QComboBox(m_fieldBox);
    m_type->addItem(tr("Integer"), QString());
    m_type->addItem(tr("float (4 bytes)"), QStringLiteral("float"));
    m_type->addItem(tr("double (8 bytes)"), QStringLiteral("double"));
    m_type->addItem(tr("Text (char[N])"), QStringLiteral("char"));
    ff->addRow(tr("Type"), m_type);
    m_bits = new QSpinBox(m_fieldBox);
    m_bits->setRange(1, 64);
    m_bits->setValue(8);
    m_bitsLabel = new QLabel(tr("Bits"), m_fieldBox);
    ff->addRow(m_bitsLabel, m_bits);
    m_count = new QSpinBox(m_fieldBox);
    m_count->setRange(1, 255);
    m_count->setValue(8);
    m_countLabel = new QLabel(tr("Characters"), m_fieldBox);
    ff->addRow(m_countLabel, m_count);
    m_signed = new QCheckBox(tr("Two's-complement signed"), m_fieldBox);
    ff->addRow(tr("Signed"), m_signed);
    m_unit = new QLineEdit(m_fieldBox);
    m_unit->setPlaceholderText(tr("optional, e.g. m, km/h"));
    ff->addRow(tr("Unit"), m_unit);
    m_enum = new QComboBox(m_fieldBox);
    m_enum->addItem(tr("(none)"), QString());
    QStringList enums;
    for (int e : doc.all(QStringLiteral("enum"))) enums << doc.node(e).value(QStringLiteral("name"));
    enums.sort(Qt::CaseInsensitive);
    for (const QString &e : enums) m_enum->addItem(e, e);
    m_enum->setMaxVisibleItems(20);
    ff->addRow(tr("Value names"), m_enum);
    m_hex = new QSpinBox(m_fieldBox);
    m_hex->setRange(0, 16);
    m_hex->setSpecialValueText(tr("decimal"));
    ff->addRow(tr("Hex digits"), m_hex);
    lay->addWidget(m_fieldBox);

    // Pad.
    m_padBox = new QWidget(this);
    auto *pf = new QFormLayout(m_padBox);
    pf->setContentsMargins(0, 0, 0, 0);
    m_padBits = new QSpinBox(m_padBox);
    m_padBits->setRange(1, 4096);
    m_padBits->setValue(1);
    pf->addRow(tr("Bits"), m_padBits);
    lay->addWidget(m_padBox);

    // Note.
    m_noteBox = new QWidget(this);
    auto *nf = new QFormLayout(m_noteBox);
    nf->setContentsMargins(0, 0, 0, 0);
    m_note = new QLineEdit(m_noteBox);
    m_note->setPlaceholderText(tr("e.g. national_values (section heading)"));
    nf->addRow(tr("Text"), m_note);
    lay->addWidget(m_noteBox);

    auto *inFile = new QLabel(tr("It will be written as:"), this);
    inFile->setStyleSheet(UiColor::mutedStyle());
    lay->addSpacing(6);
    lay->addWidget(inFile);
    m_preview = new QLabel(this);
    m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_preview->setTextFormat(Qt::PlainText);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_preview->setWordWrap(true);
    lay->addWidget(m_preview);
    m_effect = new QLabel(this);
    m_effect->setWordWrap(true);
    lay->addWidget(m_effect);
    m_reason = new QLabel(this);
    m_reason->setWordWrap(true);
    m_reason->setStyleSheet(UiColor::errorStyle());
    lay->addWidget(m_reason);
    lay->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_add = buttons->addButton(tr("Add"), QDialogButtonBox::AcceptRole);
    m_add->setDefault(true);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // A pad by default when the neighbour is one; a field otherwise.
    if (anchor != container && doc.node(anchor).tag == QLatin1String("pad")) m_kind->setCurrentIndex(1);

    for (QComboBox *c : { m_kind, m_where, m_type, m_enum }) {
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SchemaElementDialog::update);
    }
    for (QSpinBox *sp : { m_bits, m_count, m_hex, m_padBits }) {
        connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this, &SchemaElementDialog::update);
    }
    for (QLineEdit *e : { m_name, m_unit, m_note }) connect(e, &QLineEdit::textChanged, this, &SchemaElementDialog::update);
    connect(m_signed, &QCheckBox::toggled, this, &SchemaElementDialog::update);
    // One label column for the four forms, so they read as one.
    int labelWidth = 0;
    QVector<QWidget *> labels;
    for (QFormLayout *f : { top, ff, pf, nf }) {
        for (int r = 0; r < f->rowCount(); ++r) {
            if (QLayoutItem *item = f->itemAt(r, QFormLayout::LabelRole)) {
                if (QWidget *w = item->widget()) {
                    labels << w;
                    labelWidth = std::max(labelWidth, w->sizeHint().width());
                }
            }
        }
    }
    for (QWidget *w : labels) w->setMinimumWidth(labelWidth);
    resize(560, sizeHint().height());
    update();
    m_name->setFocus();
}

int SchemaElementDialog::anchor() const { return m_where->currentData().toPoint().x(); }

Place SchemaElementDialog::place() const { return Place(m_where->currentData().toPoint().y()); }

SchemaEdit::NewElement SchemaElementDialog::element() const
{
    SchemaEdit::NewElement e;
    e.tag = m_kind->currentData().toString();
    auto add = [&e](const QString &name, const QString &value) { e.attrs.push_back(qMakePair(name, value)); };
    if (e.tag == QLatin1String("pad")) {
        add(QStringLiteral("bits"), QString::number(m_padBits->value()));
    } else if (e.tag == QLatin1String("note")) {
        add(QStringLiteral("text"), m_note->text().trimmed());
    } else {
        add(QStringLiteral("name"), m_name->text().trimmed());
        const QString type = m_type->currentData().toString();
        if (type.isEmpty()) add(QStringLiteral("bits"), QString::number(m_bits->value()));
        else add(QStringLiteral("type"), type);
        if (type == QLatin1String("char")) add(QStringLiteral("count"), QString::number(m_count->value()));
        if (type.isEmpty() && m_signed->isChecked()) add(QStringLiteral("signed"), QStringLiteral("true"));
        if (!m_unit->text().trimmed().isEmpty()) add(QStringLiteral("unit"), m_unit->text().trimmed());
        if (type.isEmpty() && !m_enum->currentData().toString().isEmpty()) add(QStringLiteral("enum"), m_enum->currentData().toString());
        if (type.isEmpty() && m_hex->value() > 0) add(QStringLiteral("hex"), QString::number(m_hex->value()));
    }
    return e;
}

void SchemaElementDialog::update()
{
    const QString kind = m_kind->currentData().toString();
    m_fieldBox->setVisible(kind == QLatin1String("field"));
    m_padBox->setVisible(kind == QLatin1String("pad"));
    m_noteBox->setVisible(kind == QLatin1String("note"));
    const QString type = m_type->currentData().toString();
    m_bits->setEnabled(type.isEmpty());
    m_bitsLabel->setEnabled(type.isEmpty());
    m_count->setEnabled(type == QLatin1String("char"));
    m_countLabel->setEnabled(type == QLatin1String("char"));
    m_signed->setEnabled(type.isEmpty());
    m_enum->setEnabled(type.isEmpty());
    m_hex->setEnabled(type.isEmpty());

    const SchemaEdit::NewElement e = element();
    m_preview->setText(SchemaEdit::elementLine(e));
    QByteArray text;
    int node = -1;
    m_problem.clear();
    const bool ok = m_session.previewInsert(anchor(), place(), e, &text, &node, &m_problem);
    m_reason->setText(m_problem);
    m_add->setEnabled(ok);
    if (!ok) {
        m_effect->clear();
        return;
    }
    SchemaDoc::Document next;
    next.parse(text);
    const int c = next.container(node);
    const SchemaDoc::Document &doc = m_session.doc();
    const int c0 = doc.container(anchor());
    const QString owner = next.node(c).value(QStringLiteral("name"));
    const int before = SchemaEdit::fixedBits(doc, c0), after = SchemaEdit::fixedBits(next, c);
    const int bits = SchemaEdit::fixedBits(next, node);
    QStringList effect;
    if (bits == 0) {
        effect << tr("Reads no bits: nothing moves.");
    } else if (before >= 0 && after >= 0) {
        effect << tr("%1: %2 → %3. Everything after it moves %n bit(s) later.", nullptr, bits)
                      .arg(owner, sizeText(before), sizeText(after));
    } else {
        effect << tr("Everything after it in %1 moves %n bit(s) later.", nullptr, bits).arg(owner);
    }
    if (kind == QLatin1String("field")) {
        const QString nm = m_name->text().trimmed();
        for (int i = 0; i < doc.count(); ++i) {
            if (doc.container(i) == c0 && doc.node(i).value(QStringLiteral("name")) == nm) {
                effect << tr("%1 already has a %2: conditions naming it will see whichever is read last.").arg(owner, nm);
                break;
            }
        }
    }
    if (bits > 0 && bits % 8 != 0 && (after >= 0 && after % 8 != 0)) {
        effect << tr("%1 is no longer a whole number of bytes: adjust a pad.").arg(owner);
    }
    m_effect->setText(effect.join(QLatin1Char(' ')));
}
