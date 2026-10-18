#include "schemacontainerdialog.h"

#include "uicolors.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

SchemaContainerDialog::SchemaContainerDialog(const SchemaEdit::Session &session, const QString &tag, QWidget *parent)
    : QDialog(parent), m_session(session)
{
    const SchemaDoc::Document &doc = session.doc();
    setWindowTitle(tr("New"));
    auto *lay = new QVBoxLayout(this);

    auto *top = new QFormLayout;
    m_kind = new QComboBox(this);
    for (const QString &t : SchemaEdit::containerTags()) {
        m_kind->addItem(t == QLatin1String("crc") ? tr("CRC definition") : SchemaEdit::title(t), t);
    }
    m_kind->setCurrentIndex(std::max(0, m_kind->findData(tag)));
    top->addRow(tr("New"), m_kind);
    m_name = new QLineEdit(this);
    top->addRow(tr("Name"), m_name);
    lay->addLayout(top);

    // Packet.
    m_packetBox = new QWidget(this);
    auto *pf = new QFormLayout(m_packetBox);
    pf->setContentsMargins(0, 0, 0, 0);
    m_captures = new QLineEdit(m_packetBox);
    m_captures->setPlaceholderText(tr("capture tokens, e.g. weather or a,b"));
    m_captures->setToolTip(tr("The @token of the capture lines it decodes (the decoder's match=\"captype==…\")"));
    pf->addRow(tr("Captures"), m_captures);
    m_wire = new QComboBox(m_packetBox);
    m_wire->addItem(tr("lsb-first (Annexure D, logs)"), QStringLiteral("lsb-first"));
    m_wire->addItem(tr("msb-first (radio, Annexure C)"), QStringLiteral("msb-first"));
    pf->addRow(tr("Bit order"), m_wire);
    m_crc = new QComboBox(m_packetBox);
    m_crc->addItem(tr("(none)"), QString());
    for (int c : doc.all(QStringLiteral("crc"))) {
        if (SchemaEdit::isContainer(doc, c)) m_crc->addItem(doc.node(c).value(QStringLiteral("name")), doc.node(c).value(QStringLiteral("name")));
    }
    pf->addRow(tr("CRC"), m_crc);
    m_trailer = new QSpinBox(m_packetBox);
    m_trailer->setRange(0, 64);
    m_trailer->setSuffix(tr(" bytes"));
    m_trailer->setToolTip(tr("Bytes at the end that are not body (a stored CRC)"));
    pf->addRow(tr("Trailer"), m_trailer);
    m_bodyOffset = new QSpinBox(m_packetBox);
    m_bodyOffset->setRange(0, 100000);
    m_bodyOffset->setSpecialValueText(tr("bit 0"));
    m_bodyOffset->setToolTip(tr("The bit the body starts at, past a header"));
    pf->addRow(tr("Body from"), m_bodyOffset);
    lay->addWidget(m_packetBox);

    // Struct.
    m_structBox = new QWidget(this);
    auto *sf = new QFormLayout(m_structBox);
    sf->setContentsMargins(0, 0, 0, 0);
    m_casePacket = new QComboBox(m_structBox);
    m_casePacket->addItem(tr("Not yet (register it later)"), QString());
    for (const QString &p : SchemaEdit::packetsWithSubpackets(doc)) m_casePacket->addItem(tr("A sub-packet of %1").arg(p), p);
    sf->addRow(tr("Decode it as"), m_casePacket);
    m_caseType = new QSpinBox(m_structBox);
    m_caseType->setRange(0, 65535);
    sf->addRow(tr("Type"), m_caseType);
    lay->addWidget(m_structBox);

    // CRC definition.
    m_crcDefBox = new QWidget(this);
    auto *cf = new QFormLayout(m_crcDefBox);
    cf->setContentsMargins(0, 0, 0, 0);
    m_crcType = new QComboBox(m_crcDefBox);
    m_crcType->addItems({ QStringLiteral("crc32"), QStringLiteral("crc30") });
    cf->addRow(tr("Type"), m_crcType);
    m_poly = new QLineEdit(QStringLiteral("0x04C11DB7"), m_crcDefBox);
    cf->addRow(tr("Polynomial"), m_poly);
    m_init = new QLineEdit(QStringLiteral("0xFFFFFFFF"), m_crcDefBox);
    cf->addRow(tr("Initial"), m_init);
    m_xor = new QLineEdit(QStringLiteral("0x00000000"), m_crcDefBox);
    cf->addRow(tr("XOR out"), m_xor);
    m_reflectIn = new QCheckBox(tr("Bytes bit-reversed going in"), m_crcDefBox);
    cf->addRow(tr("Reflect in"), m_reflectIn);
    m_reflectOut = new QCheckBox(tr("Result bit-reversed"), m_crcDefBox);
    cf->addRow(tr("Reflect out"), m_reflectOut);
    m_crcTrailer = new QSpinBox(m_crcDefBox);
    m_crcTrailer->setRange(1, 8);
    m_crcTrailer->setValue(4);
    m_crcTrailer->setSuffix(tr(" bytes"));
    cf->addRow(tr("Stored as"), m_crcTrailer);
    lay->addWidget(m_crcDefBox);

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
    m_effect->setStyleSheet(UiColor::mutedStyle());
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

    // One label column for the forms.
    int labelWidth = 0;
    QVector<QWidget *> labels;
    for (QFormLayout *f : { top, pf, sf, cf }) {
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

    // The captures follow the name until they are typed into.
    connect(m_captures, &QLineEdit::textEdited, this, [this]() { m_capturesTouched = true; });
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString &n) {
        if (!m_capturesTouched) {
            const QSignalBlocker block(m_captures);
            m_captures->setText(n.trimmed().toLower());
        }
    });
    connect(m_casePacket, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        const QString p = m_casePacket->currentData().toString();
        if (!p.isEmpty()) m_caseType->setValue(std::max(0, SchemaEdit::freeCaseType(m_session.doc(), p)));
    });
    for (QComboBox *c : { m_kind, m_wire, m_crc, m_casePacket, m_crcType }) {
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SchemaContainerDialog::update);
    }
    for (QSpinBox *sp : { m_trailer, m_bodyOffset, m_caseType, m_crcTrailer }) {
        connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this, &SchemaContainerDialog::update);
    }
    for (QLineEdit *e : { m_name, m_captures, m_poly, m_init, m_xor }) connect(e, &QLineEdit::textChanged, this, &SchemaContainerDialog::update);
    for (QCheckBox *b : { m_reflectIn, m_reflectOut }) connect(b, &QCheckBox::toggled, this, &SchemaContainerDialog::update);
    resize(560, sizeHint().height());
    update();
    m_name->setFocus();
}

SchemaEdit::NewContainer SchemaContainerDialog::container() const
{
    SchemaEdit::NewContainer c;
    c.tag = m_kind->currentData().toString();
    c.name = m_name->text().trimmed();
    auto add = [&c](const QString &n, const QString &v) { c.attrs.push_back(qMakePair(n, v)); };
    if (c.tag == QLatin1String("packet")) {
        QStringList tokens;
        for (const QString &t : m_captures->text().split(QLatin1Char(','))) {
            if (!t.trimmed().isEmpty()) tokens << t.trimmed();
        }
        add(QStringLiteral("match"), QStringLiteral("captype==") + tokens.join(QLatin1Char(',')));
        add(QStringLiteral("wire"), m_wire->currentData().toString());
        if (!m_crc->currentData().toString().isEmpty()) add(QStringLiteral("crc"), m_crc->currentData().toString());
        add(QStringLiteral("trailer_bytes"), QString::number(m_trailer->value()));
        if (m_bodyOffset->value() > 0) add(QStringLiteral("body_offset"), QString::number(m_bodyOffset->value()));
    } else if (c.tag == QLatin1String("struct")) {
        c.casePacket = m_casePacket->currentData().toString();
        c.caseType = c.casePacket.isEmpty() ? -1 : m_caseType->value();
    } else if (c.tag == QLatin1String("crc")) {
        add(QStringLiteral("type"), m_crcType->currentText());
        add(QStringLiteral("poly"), m_poly->text().trimmed());
        add(QStringLiteral("init"), m_init->text().trimmed());
        add(QStringLiteral("reflect_in"), m_reflectIn->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        add(QStringLiteral("reflect_out"), m_reflectOut->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        add(QStringLiteral("xor_out"), m_xor->text().trimmed());
        add(QStringLiteral("trailer_bytes"), QString::number(m_crcTrailer->value()));
    }
    return c;
}

void SchemaContainerDialog::update()
{
    const QString tag = m_kind->currentData().toString();
    setWindowTitle(tr("New %1").arg(m_kind->currentText().toLower()));
    m_packetBox->setVisible(tag == QLatin1String("packet"));
    m_structBox->setVisible(tag == QLatin1String("struct"));
    m_crcDefBox->setVisible(tag == QLatin1String("crc"));
    m_caseType->setEnabled(!m_casePacket->currentData().toString().isEmpty());
    m_name->setPlaceholderText(tag == QLatin1String("packet") ? tr("e.g. WEATHER")
                             : tag == QLatin1String("struct") ? tr("e.g. WeatherProfile")
                             : tag == QLatin1String("crc") ? tr("e.g. anxx_crc32") : tr("e.g. weatherCode"));

    const SchemaEdit::NewContainer c = container();
    QByteArray text;
    int node = -1;
    m_problem.clear();
    const bool ok = m_session.previewContainer(c, &text, &node, &m_problem);
    m_reason->setText(m_problem);
    m_add->setEnabled(ok);
    // The block as written: the new element and, for a struct, its case line.
    QStringList shown;
    if (ok) {
        SchemaDoc::Document next;
        next.parse(text);
        int from = 0, to = 0;
        next.lineSpan(node, &from, &to);
        QString block = QString::fromUtf8(text.mid(from, to - from)).trimmed();
        const QString indent = QString::fromUtf8(next.indentOf(node));
        block.replace(QLatin1Char('\n') + indent, QStringLiteral("\n"));
        shown << block;
        if (!c.casePacket.isEmpty()) {
            for (int k : next.all(QStringLiteral("case"))) {
                if (next.node(k).value(QStringLiteral("struct")) == c.name) {
                    shown << tr("and in %1's <subpackets>:").arg(c.casePacket) << next.startTag(k);
                }
            }
        }
    }
    m_preview->setText(shown.join(QLatin1Char('\n')));
    if (tag == QLatin1String("packet")) m_effect->setText(tr("Empty: add its fields next. It decodes the captures named here."));
    else if (tag == QLatin1String("struct")) {
        m_effect->setText(c.casePacket.isEmpty() ? tr("Empty: add its fields next. It decodes nothing until a packet's sub-packet case names it.")
                                                 : tr("Empty: add its fields next. %1 decodes sub-packets of type %2 with it.")
                                                       .arg(c.casePacket).arg(c.caseType));
    } else if (tag == QLatin1String("crc")) {
        m_effect->setText(tr("The program computes CRCs with algorithms registered in code; this records the parameters "
                             "packets refer to with crc=."));
    } else {
        m_effect->setText(tr("Empty: add its entries next (a later phase of the editor adds them from a form; "
                             "Duplicate copies one now)."));
    }
}
