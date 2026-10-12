#include "portedit.h"

#include <QIntValidator>

PortEdit::PortEdit(QWidget *parent) : QLineEdit(parent)
{
    setValidator(new QIntValidator(1, 65535, this));
    setText(QString::number(m_value));
    connect(this, &QLineEdit::textEdited, this, [this](const QString &text) {
        bool ok = false;
        const int port = text.toInt(&ok);
        if (!ok || port < 1 || port > 65535 || port == m_value) return;
        m_value = port;
        emit valueChanged(m_value);
    });
    connect(this, &QLineEdit::editingFinished, this, [this]() {
        if (text().toInt() != m_value) setText(QString::number(m_value));
    });
}

void PortEdit::setValue(int port)
{
    if (port < 1 || port > 65535) return;    // as QSpinBox would not take it either
    const bool changed = port != m_value;
    m_value = port;
    if (text() != QString::number(port)) setText(QString::number(port));
    if (changed && !signalsBlocked()) emit valueChanged(m_value);
}
