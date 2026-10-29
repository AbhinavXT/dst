#include "tagsenddialog.h"
#include "locosim.h"
#include "locosimwindow.h"
#include "rfidcheck.h"
#include "rfidtag.h"
#include "statusline.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

TagSendDialog::TagSendDialog(LocoSimWindow *window) : QDialog(window), m_window(window)
{
    setObjectName(QStringLiteral("tagSendDialog"));
    setWindowTitle(tr("Send a tag"));
    auto *root = new QVBoxLayout(this);
    auto *note = new QLabel(tr("One tag, now, on reader 1 and / or reader 2, from its page_x and page_y (as the "
                               "simulator's SPAD scenario RFID interface): whatever the loco is doing."), this);
    note->setWordWrap(true);
    root->addWidget(note);

    auto *from = new QHBoxLayout;
    from->addWidget(new QLabel(tr("From the route"), this));
    m_fromRoute = new QComboBox(this);
    m_fromRoute->setObjectName(QStringLiteral("tagSendFromRoute"));
    m_fromRoute->setToolTip(tr("Fills every ticked reader's page_x and page_y with this tag's"));
    from->addWidget(m_fromRoute, 1);
    root->addLayout(from);

    auto *grid = new QGridLayout;
    for (int r = 0; r < 2; ++r) {
        const int col = r * 2;
        m_on[r] = new QCheckBox(tr("Reader %1").arg(r + 1), this);
        m_on[r]->setChecked(r == 0);
        grid->addWidget(m_on[r], 0, col, 1, 2);
        grid->addWidget(new QLabel(tr("page_x"), this), 1, col);
        m_pageX[r] = new QLineEdit(this);
        m_pageX[r]->setObjectName(QStringLiteral("tagSendPageX%1").arg(r + 1));
        m_pageX[r]->setPlaceholderText(tr("16 hex digits"));
        grid->addWidget(m_pageX[r], 1, col + 1);
        grid->addWidget(new QLabel(tr("page_y"), this), 2, col);
        m_pageY[r] = new QLineEdit(this);
        m_pageY[r]->setObjectName(QStringLiteral("tagSendPageY%1").arg(r + 1));
        m_pageY[r]->setPlaceholderText(tr("16 hex digits"));
        grid->addWidget(m_pageY[r], 2, col + 1);
        m_says[r] = new QLabel(this);
        m_says[r]->setWordWrap(true);
        grid->addWidget(m_says[r], 3, col, 1, 2);
        connect(m_pageX[r], &QLineEdit::textChanged, this, [this, r]() { showTag(r + 1); });
        connect(m_pageY[r], &QLineEdit::textChanged, this, [this, r]() { showTag(r + 1); });
        showTag(r + 1);
    }
    root->addLayout(grid);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_send = new QPushButton(tr("Send"), this);
    m_send->setObjectName(QStringLiteral("tagSendSend"));
    m_send->setDefault(true);
    buttons->addWidget(m_send);
    root->addLayout(buttons);
    m_status = new StatusLine(this);
    root->addWidget(m_status);

    connect(m_fromRoute, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index) { fillFromRoute(m_fromRoute->itemData(index).toInt()); });
    connect(m_send, &QPushButton::clicked, this, [this]() { send(); });
    refreshRoute();
    resize(640, 280);
}

void TagSendDialog::refreshRoute()
{
    m_fromRoute->clear();
    const QVector<QPair<QString, QByteArray>> tags = m_window->routeTags();
    if (tags.isEmpty()) m_fromRoute->addItem(tr("(no route open)"), -1);
    for (int i = 0; i < tags.size(); ++i) m_fromRoute->addItem(tags.at(i).first, i);
}

void TagSendDialog::setReader(int reader, bool on)
{
    if (reader == 1 || reader == 2) m_on[reader - 1]->setChecked(on);
}

void TagSendDialog::setPages(int reader, const QString &pageX, const QString &pageY)
{
    if (reader != 1 && reader != 2) return;
    m_pageX[reader - 1]->setText(pageX);
    m_pageY[reader - 1]->setText(pageY);
}

void TagSendDialog::fillFromRoute(int routeRow)
{
    const QVector<QPair<QString, QByteArray>> tags = m_window->routeTags();
    if (routeRow < 0 || routeRow >= tags.size()) return;
    const QByteArray tag = tags.at(routeRow).second;
    for (int r = 1; r <= 2; ++r)
        if (m_on[r - 1]->isChecked()) setPages(r, RfidTag::pageX(tag), RfidTag::pageY(tag));
}

QString TagSendDialog::says(int reader) const
{
    return reader == 1 || reader == 2 ? m_says[reader - 1]->text() : QString();
}

void TagSendDialog::showTag(int reader)
{
    const int r = reader - 1;
    const QString x = m_pageX[r]->text().trimmed(), y = m_pageY[r]->text().trimmed();
    const QByteArray frame = LocoSim::simulatorTagFrame(reader, x, y);
    if (x.isEmpty() && y.isEmpty()) {
        m_says[r]->setText(QString());
        return;
    }
    if (frame.isEmpty()) {
        m_says[r]->setText(tr("page_y and page_x together must be exactly 32 hex digits"));
        return;
    }
    const QByteArray tag = frame.mid(4, 16);
    const RfidTag::Summary s = RfidTag::summary(tag);
    m_says[r]->setText(tr("Tag %1, %2, %3, CRC-30 %4")
                           .arg(RfidTag::nameOf(tag), RfidTag::typeName(s.type),
                                s.absLoc == RfidCheck::kNotApplicable ? tr("no location") : tr("at %1 m").arg(s.absLoc),
                                s.crcOk ? tr("matches") : tr("does not match: a loco would not process it")));
}

int TagSendDialog::send()
{
    int sent = 0;
    QStringList said;
    for (int r = 1; r <= 2; ++r) {
        if (!m_on[r - 1]->isChecked()) continue;
        QString why;
        if (m_window->sendTagByHand(r, m_pageX[r - 1]->text(), m_pageY[r - 1]->text(), &why)) {
            ++sent;
            said << tr("reader %1 sent it").arg(r);
        } else {
            said << tr("reader %1: %2").arg(r).arg(why);
        }
    }
    if (said.isEmpty()) m_status->warn(tr("Tick reader 1 or reader 2."));
    else if (sent == int(said.size())) m_status->ok(said.join(QStringLiteral("; ")));
    else m_status->fail(said.join(QStringLiteral("; ")));
    return sent;
}
