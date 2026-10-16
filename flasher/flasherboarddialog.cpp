#include "flasherboarddialog.h"

#include "portedit.h"
#include "statusline.h"
#include "uistyle.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QUdpSocket>
#include <QVBoxLayout>

#include <cstring>

namespace {
const int  kRoundMs = 400;          // one HOLD / INFO_REQ / POLL round
const char kHold    = 0x07;
const char kInfoReq = 0x04;
const char kBoot    = 0x08;
const char kInfo    = 0x05;
const char kStatus  = 0x03;
}  // namespace

// =============================================================================
//  BoardQuery
// =============================================================================

BoardQuery::BoardQuery(QObject *parent)
    : QObject(parent)
    , m_socket(new QUdpSocket(this))
    , m_timer(new QTimer(this))
{
    m_socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0);
    connect(m_socket, &QUdpSocket::readyRead, this, &BoardQuery::readReplies);
    m_timer->setInterval(kRoundMs);
    connect(m_timer, &QTimer::timeout, this, &BoardQuery::sendRound);
}

void BoardQuery::ask(const QHostAddress &address, quint16 port, int waitMs)
{
    stop();
    drain();
    m_job = Job::Ask;
    m_address = address;
    m_port = port;
    m_waitMs = waitMs;
    m_sawStatus = false;
    m_roundsSinceStatus = 0;
    m_clock.start();
    sendRound();
    m_timer->start();
}

void BoardQuery::release(const QHostAddress &address, quint16 port, int waitMs)
{
    stop();
    drain();   // a late INFO from the asking must not pass for BOOT's acknowledgement
    m_job = Job::Release;
    m_address = address;
    m_port = port;
    m_waitMs = waitMs;
    m_clock.start();
    sendRound();
    m_timer->start();
}

void BoardQuery::stop()
{
    m_timer->stop();
    m_job = Job::None;
}

void BoardQuery::drain()
{
    while (m_socket->hasPendingDatagrams()) {
        char sink[2048];
        m_socket->readDatagram(sink, sizeof(sink));
    }
}

bool BoardQuery::isBusy() const
{
    return m_job != Job::None;
}

void BoardQuery::sendRound()
{
    if (m_job == Job::None) {
        return;
    }
    if (m_clock.elapsed() >= m_waitMs) {
        const Job job = m_job;
        stop();
        if (job == Job::Release) {
            emit released(false);
        } else if (m_sawStatus) {
            emit olderUpdater();
        } else {
            emit noAnswer();
        }
        return;
    }
    // A STATUS alone for two rounds: an updater that answers POLL but has no
    // INFO (before 2.0). Two, because a newer board's INFO may come late.
    if (m_job == Job::Ask && m_sawStatus && ++m_roundsSinceStatus > 2) {
        stop();
        emit olderUpdater();
        return;
    }
    if (m_job == Job::Release) {
        m_socket->writeDatagram(&kBoot, 1, m_address, m_port);
    } else {
        m_socket->writeDatagram(&kHold, 1, m_address, m_port);
        m_socket->writeDatagram(&kInfoReq, 1, m_address, m_port);
        const char poll[5] = { 0x02, 0, 0, 0, 0 };
        m_socket->writeDatagram(poll, sizeof(poll), m_address, m_port);
    }
    emit waiting(qMax(0, int((m_waitMs - m_clock.elapsed() + 999) / 1000)));
}

void BoardQuery::readReplies()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_socket->pendingDatagramSize()));
        QHostAddress from;
        m_socket->readDatagram(datagram.data(), datagram.size(), &from);
        if (m_job == Job::None || datagram.isEmpty() || !from.isEqual(m_address, QHostAddress::TolerantConversion)) {
            continue;
        }
        if (datagram.at(0) == kInfo && datagram.size() >= Flasher::kInfoV1Bytes) {
            const Job job = m_job;
            stop();
            if (job == Job::Release) {
                emit released(true);
            } else {
                emit answered(Flasher::parseInfoReply(datagram));
            }
            return;
        }
        if (datagram.at(0) == kStatus && datagram.size() >= 16) {
            m_sawStatus = true;
        }
    }
}

// =============================================================================
//  FlasherBoardDialog
// =============================================================================

FlasherBoardDialog::FlasherBoardDialog(const QString &address, quint16 port, int waitSeconds,
                                       const QVector<QPair<QString, quint32>> &queued, QWidget *parent)
    : QDialog(parent)
    , m_query(new BoardQuery(this))
    , m_waitSeconds(waitSeconds)
    , m_queued(queued)
{
    setWindowTitle(tr("Ask the board"));
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(tr("What a board's updater (2.0 or later) holds: its version, controller ID, both "
                                "flash banks and the firmware in them, and the card results a VCC collected. "
                                "Press Ask; if the board is running its program, power-cycle the chassis "
                                "while the flasher asks. An updater 2.2 or later is then held there for 2 "
                                "minutes, or until Release."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Board"), this));
    m_address = new QLineEdit(address, this);
    m_address->setObjectName(QStringLiteral("boardAddress"));
    m_address->setFont(UiStyle::monoFont());
    row->addWidget(m_address, 1);
    row->addWidget(new QLabel(tr("Port"), this));
    m_port = new PortEdit(this);
    m_port->setValue(port);
    row->addWidget(m_port);
    m_askButton = new QPushButton(tr("Ask"), this);
    m_askButton->setDefault(true);
    row->addWidget(m_askButton);
    m_stopButton = new QPushButton(tr("Stop"), this);
    row->addWidget(m_stopButton);
    layout->addLayout(row);

    m_status = new StatusLine(this);
    m_status->state(tr("Not asked yet."));
    layout->addWidget(m_status);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setFont(UiStyle::monoFont());
    m_text->setMinimumSize(640, 300);
    layout->addWidget(m_text, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_releaseButton = buttons->addButton(tr("Release (BOOT)"), QDialogButtonBox::ActionRole);
    m_releaseButton->setToolTip(tr("Tell the board to leave the updater and start its program now"));
    m_copyButton = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_askButton, &QPushButton::clicked, this, &FlasherBoardDialog::ask);
    connect(m_releaseButton, &QPushButton::clicked, this, &FlasherBoardDialog::release);
    connect(m_stopButton, &QPushButton::clicked, this, [this]() {
        m_query->stop();
        setBusy(false, QString());
        m_status->warn(tr("Stopped."));
    });
    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_text->toPlainText());
        m_status->ok(tr("Copied."));
    });

    connect(m_query, &BoardQuery::waiting, this, [this](int secondsLeft) {
        m_status->state(tr("Asking %1 — power-cycle the chassis if it is running its program · %2 s left")
                            .arg(m_address->text().trimmed()).arg(secondsLeft));
    });
    connect(m_query, &BoardQuery::answered, this, [this](const Flasher::BoardInfo &info) {
        m_info = info;
        setBusy(false, QString());
        m_text->setPlainText(Flasher::boardInfoText(info, m_queued));
        if (info.canHold()) {
            m_status->ok(tr("Updater %1 answered and is held in the updater (2 min idle, or Release).")
                             .arg(info.updaterVersionText()));
        } else {
            m_status->ok(tr("Updater %1 answered (it cannot be held: older than 2.2).")
                             .arg(info.updaterVersionText()));
        }
        m_releaseButton->setEnabled(info.canHold());
        emit finishedTalking();
    });
    connect(m_query, &BoardQuery::olderUpdater, this, [this]() {
        setBusy(false, QString());
        m_status->warn(tr("A board answered POLL but not INFO: its updater is older than 2.0 and has "
                          "nothing to tell. Use the Classic updater for it."));
        emit finishedTalking();
    });
    connect(m_query, &BoardQuery::noAnswer, this, [this]() {
        setBusy(false, QString());
        m_status->fail(tr("No answer in %1 s. Check the address and the cable, and that the chassis "
                          "was power-cycled while asking.").arg(m_waitSeconds));
        emit finishedTalking();
    });
    connect(m_query, &BoardQuery::released, this, [this](bool acknowledged) {
        setBusy(false, QString());
        if (acknowledged) {
            m_status->ok(tr("The board acknowledged BOOT and is starting its program."));
            m_releaseButton->setEnabled(false);
        } else {
            m_status->warn(tr("No acknowledgement. A held board boots by itself after 2 minutes without traffic."));
        }
        emit finishedTalking();
    });

    m_releaseButton->setEnabled(false);
    m_stopButton->setEnabled(false);
}

bool FlasherBoardDialog::target(QHostAddress *address, quint16 *port)
{
    // The engine's strict parser: QHostAddress takes "192.168.25" as
    // 192.168.0.25 (inet_aton shorthand) and would ask the wrong machine.
    uint32_t hostOrder = 0;
    const QString text = m_address->text().trimmed();
    const QHostAddress parsed(text);
    if (!kflash::parse_ipv4(text.toStdString(), &hostOrder) || parsed.isNull()) {
        m_status->fail(tr("“%1” is not an IPv4 address.").arg(m_address->text().trimmed()));
        return false;
    }
    *address = parsed;
    *port = static_cast<quint16>(m_port->value());
    return true;
}

void FlasherBoardDialog::ask()
{
    QHostAddress address;
    quint16 port = 0;
    if (!target(&address, &port)) {
        return;
    }
    m_info = Flasher::BoardInfo();
    m_text->clear();
    setBusy(true, tr("Asking…"));
    m_query->ask(address, port, m_waitSeconds * 1000);
}

void FlasherBoardDialog::release()
{
    QHostAddress address;
    quint16 port = 0;
    if (!target(&address, &port)) {
        return;
    }
    setBusy(true, tr("Sending BOOT…"));
    m_query->release(address, port, 5000);
}

void FlasherBoardDialog::setBusy(bool busy, const QString &what)
{
    m_askButton->setEnabled(!busy);
    m_stopButton->setEnabled(busy);
    m_address->setEnabled(!busy);
    m_port->setEnabled(!busy);
    if (busy) {
        m_releaseButton->setEnabled(false);
        m_status->state(what);
    }
}

QString FlasherBoardDialog::resultText() const
{
    return m_text->toPlainText();
}

QString FlasherBoardDialog::statusText() const
{
    return m_status->text();
}
