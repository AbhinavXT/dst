#include "tageditdialog.h"
#include "fielddocs.h"
#include "rfidtag.h"
#include "uicolors.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

const int kTypes[] = { 9, 10, 11, 12 };

QString hex8(quint32 v) { return QStringLiteral("%1").arg(v, 8, 16, QLatin1Char('0')).toUpper(); }

// Annexure D's description of a field, as the Tag Builder's form shows it (session 304).
QString tagDoc(const QString &field, int tagType)
{
    return FieldDocs::instance().tooltip(QStringLiteral("rfid"), field, tagType);
}

}  // namespace

TagEditDialog::TagEditDialog(const QByteArray &tag, const QString &title, const QString &okText, QWidget *parent)
    : QDialog(parent), m_tag(tag)
{
    setObjectName(QStringLiteral("tagEditDialog"));
    setWindowTitle(title);
    auto *root = new QVBoxLayout(this);
    m_name = new QLabel(this);
    m_name->setObjectName(QStringLiteral("tagEditName"));
    root->addWidget(m_name);

    auto *typeRow = new QFormLayout;
    m_type = new QComboBox(this);
    m_type->setObjectName(QStringLiteral("tagEditType"));
    for (int t : kTypes) m_type->addItem(QStringLiteral("%1  %2").arg(t).arg(RfidTag::typeName(t)), t);
    typeRow->addRow(tr("Type:"), m_type);
    root->addLayout(typeRow);

    // The fields can be more than a laptop screen holds: they scroll.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *host = new QWidget(scroll);
    m_form = new QFormLayout(host);
    scroll->setWidget(host);
    root->addWidget(scroll, 1);

    m_pages = new QLabel(this);
    m_pages->setObjectName(QStringLiteral("tagEditPages"));
    m_pages->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_pages);
    m_crc = new QLabel(this);
    m_crc->setObjectName(QStringLiteral("tagEditCrc"));
    m_crc->setWordWrap(true);
    root->addWidget(m_crc);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    m_ok->setText(okText);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_updating) setType(m_type->currentData().toInt());
    });

    showTag();
    resize(460, 620);
}

void TagEditDialog::rebuildForm()
{
    const int type = RfidTag::summary(m_tag).type;
    if (type == m_formType) return;
    m_formType = type;
    while (m_form->rowCount() > 0) m_form->removeRow(0);
    m_editors.clear();
    QWidget *host = m_form->parentWidget();
    for (const RfidTag::Field &f : RfidTag::fieldsOf(type)) {
        QWidget *w = nullptr;
        // As the Tag Builder's form: a short coded field a choice, a long one a number.
        if (!f.enumName.isEmpty() && f.bits <= 4) {
            auto *c = new QComboBox(host);
            c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            c->setMinimumContentsLength(12);
            for (qint64 v = 0; v < (qint64(1) << f.bits); ++v) c->addItem(RfidTag::enumLabel(f.enumName, v), v);
            connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
                if (!m_updating) buildFromForm();
            });
            w = c;
        } else {
            auto *s = new QSpinBox(host);
            s->setRange(0, int(qMin<qint64>((qint64(1) << f.bits) - 1, 0x7FFFFFFF)));
            if (f.enumName == QLatin1String("rfidAbs")) s->setSuffix(tr(" m"));
            connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
                if (!m_updating) buildFromForm();
            });
            w = s;
        }
        w->setObjectName(f.name);
        w->setToolTip(tagDoc(f.name, type));
        m_form->addRow(QStringLiteral("%1:").arg(f.name), w);
        m_editors.insert(f.name, w);
    }
}

void TagEditDialog::fillForm()
{
    const QHash<QString, qint64> v = RfidTag::values(m_tag);
    for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it) {
        const qint64 x = v.value(it.key());
        if (auto *c = qobject_cast<QComboBox *>(it.value())) c->setCurrentIndex(c->findData(x));
        else if (auto *s = qobject_cast<QSpinBox *>(it.value())) s->setValue(int(x));
    }
}

void TagEditDialog::buildFromForm()
{
    QHash<QString, qint64> v;
    v.insert(QStringLiteral("type"), m_type->currentData().toInt());
    for (auto it = m_editors.cbegin(); it != m_editors.cend(); ++it) {
        if (auto *c = qobject_cast<QComboBox *>(it.value())) v.insert(it.key(), c->currentData().toLongLong());
        else if (auto *s = qobject_cast<QSpinBox *>(it.value())) v.insert(it.key(), s->value());
    }
    QString err;
    const QByteArray t = RfidTag::build(v, &err);
    if (t.isEmpty()) {
        m_crc->setText(tr("Not built: %1").arg(err));
        m_crc->setStyleSheet(UiColor::style(UiColor::error()));
        return;
    }
    m_tag = t;
    showTag();
}

bool TagEditDialog::setField(const QString &name, qint64 value)
{
    QWidget *w = m_editors.value(name);
    if (!w) return false;
    if (auto *c = qobject_cast<QComboBox *>(w)) c->setCurrentIndex(c->findData(value));
    else if (auto *s = qobject_cast<QSpinBox *>(w)) s->setValue(int(value));
    return true;
}

void TagEditDialog::setType(int type)
{
    // The fields both types carry keep their values; the rest start at 0.
    QHash<QString, qint64> v = RfidTag::values(m_tag);
    v.insert(QStringLiteral("type"), type);
    const QByteArray t = RfidTag::build(v);
    if (t.isEmpty()) return;
    m_tag = t;
    showTag();
}

void TagEditDialog::showTag()
{
    m_updating = true;
    const RfidTag::Summary s = RfidTag::summary(m_tag);
    const int ti = m_type->findData(s.type);
    m_type->setCurrentIndex(ti);
    rebuildForm();
    fillForm();
    m_name->setText(ti < 0 ? tr("Tag type %1: not a type this editor builds (9 to 12)").arg(s.type)
                           : tr("Tag %1, %2 tag").arg(RfidTag::nameOf(m_tag), RfidTag::typeName(s.type)));
    m_pages->setText(tr("page_x %1 · page_y %2").arg(RfidTag::pageX(m_tag), RfidTag::pageY(m_tag)));
    if (s.crcOk) {
        m_crc->setText(tr("CRC-30 %1: matches the contents").arg(hex8(s.crcStored)));
        m_crc->setStyleSheet(UiColor::style(UiColor::ok()));
    } else {
        m_crc->setText(tr("CRC-30 stored %1, the contents give %2: a loco would not process this tag")
                           .arg(hex8(s.crcStored), hex8(s.crcCalc)));
        m_crc->setStyleSheet(UiColor::style(UiColor::error()));
    }
    m_updating = false;
}
