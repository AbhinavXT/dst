#include "serialfilesender.h"

#include "seriallink.h"

#include <QSettings>
#include <QTimer>

QString SerialSendOptions::summary() const
{
    if (mode == Mode::Chunks)
        return QObject::tr("%1-byte chunks every %2 ms").arg(chunkBytes).arg(delayMs);
    if (prompt.isEmpty()) return QObject::tr("line by line, %1 ms apart").arg(delayMs);
    return QObject::tr("line by line, waiting for “%1” (up to %2 s)")
        .arg(prompt).arg(promptTimeoutMs / 1000.0, 0, 'f', 1);
}

void SerialSendOptions::save(QSettings &s) const
{
    s.beginGroup(QStringLiteral("serial/fileSend"));
    s.setValue(QStringLiteral("mode"), mode == Mode::Lines ? QStringLiteral("lines") : QStringLiteral("chunks"));
    s.setValue(QStringLiteral("chunkBytes"), chunkBytes);
    s.setValue(QStringLiteral("delayMs"), delayMs);
    s.setValue(QStringLiteral("prompt"), prompt);
    s.setValue(QStringLiteral("promptTimeoutMs"), promptTimeoutMs);
    s.endGroup();
}

SerialSendOptions SerialSendOptions::load(QSettings &s)
{
    SerialSendOptions o;
    s.beginGroup(QStringLiteral("serial/fileSend"));
    o.mode = s.value(QStringLiteral("mode")).toString() == QLatin1String("lines") ? Mode::Lines : Mode::Chunks;
    o.chunkBytes = qBound(1, s.value(QStringLiteral("chunkBytes"), 256).toInt(), 1 << 20);
    o.delayMs = qBound(0, s.value(QStringLiteral("delayMs"), 20).toInt(), 60000);
    o.prompt = s.value(QStringLiteral("prompt")).toString();
    o.promptTimeoutMs = qBound(100, s.value(QStringLiteral("promptTimeoutMs"), 5000).toInt(), 600000);
    s.endGroup();
    return o;
}

QVector<QByteArray> serialSendPieces(const QByteArray &data, const SerialSendOptions &o)
{
    QVector<QByteArray> out;
    if (o.mode == SerialSendOptions::Mode::Chunks) {
        const int n = qMax(1, o.chunkBytes);
        for (int i = 0; i < data.size(); i += n) out << data.mid(i, n);
        return out;
    }
    int start = 0;
    for (int i = 0; i < data.size(); ++i) {
        if (data.at(i) == '\n') { out << data.mid(start, i - start + 1); start = i + 1; }
    }
    if (start < data.size()) out << data.mid(start);
    return out;
}

SerialFileSender::SerialFileSender(QObject *parent) : QObject(parent)
{
    m_step = new QTimer(this);
    m_step->setSingleShot(true);
    connect(m_step, &QTimer::timeout, this, &SerialFileSender::sendNext);
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        finish(false, tr("no “%1” from the card within %2 s after line %3 of %4; stopped "
                         "(%5 of %6 bytes sent)")
                          .arg(m_opt.prompt).arg(m_opt.promptTimeoutMs / 1000.0, 0, 'f', 1)
                          .arg(m_next).arg(m_pieces.size()).arg(m_sent).arg(m_total));
    });
}

bool SerialFileSender::start(SerialLink *link, const QByteArray &data, const SerialSendOptions &options)
{
    if (m_running || !link || !link->isOpen() || data.isEmpty()) return false;
    m_link = link;
    m_opt = options;
    m_pieces = serialSendPieces(data, options);
    m_next = 0;
    m_sent = 0;
    m_total = data.size();
    m_running = true;
    m_waitingPrompt = false;
    m_heard.clear();
    connect(link, &SerialLink::bytesReceived, this, [this](const QByteArray &b, qint64) { onBytes(b); });
    connect(link, &SerialLink::closed, this, [this]() {
        finish(false, tr("the port closed; %1 of %2 bytes sent").arg(m_sent).arg(m_total));
    });
    m_clock.start();
    emit progress(0, m_total);
    sendNext();
    return true;
}

void SerialFileSender::stop()
{
    if (!m_running) return;
    finish(false, tr("stopped at %1 of %2 bytes").arg(m_sent).arg(m_total));
}

void SerialFileSender::sendNext()
{
    if (!m_running) return;
    if (!m_link || !m_link->isOpen()) {
        finish(false, tr("the port closed; %1 of %2 bytes sent").arg(m_sent).arg(m_total));
        return;
    }
    if (m_next >= m_pieces.size()) {
        finish(true, tr("sent %1 bytes in %2 s").arg(m_total).arg(m_clock.elapsed() / 1000.0, 0, 'f', 1));
        return;
    }
    const QByteArray piece = m_pieces.at(m_next++);
    m_heard.clear();
    if (m_link->write(piece) != piece.size()) {
        finish(false, tr("the write failed at %1 of %2 bytes").arg(m_sent).arg(m_total));
        return;
    }
    m_sent += piece.size();
    emit progress(m_sent, m_total);
    if (m_next >= m_pieces.size()) {
        finish(true, tr("sent %1 bytes in %2 s").arg(m_total).arg(m_clock.elapsed() / 1000.0, 0, 'f', 1));
        return;
    }
    if (m_opt.mode == SerialSendOptions::Mode::Lines && !m_opt.prompt.isEmpty()) {
        m_waitingPrompt = true;
        m_timeout->start(m_opt.promptTimeoutMs);
        return;
    }
    m_step->start(m_opt.delayMs);
}

void SerialFileSender::onBytes(const QByteArray &bytes)
{
    if (!m_running || !m_waitingPrompt) return;
    m_heard += bytes;
    if (m_heard.size() > 65536) m_heard = m_heard.right(4096);
    if (!m_heard.contains(m_opt.prompt.toUtf8())) return;
    m_waitingPrompt = false;
    m_timeout->stop();
    m_step->start(m_opt.delayMs);
}

void SerialFileSender::finish(bool ok, const QString &message)
{
    if (!m_running) return;
    m_running = false;
    m_waitingPrompt = false;
    m_step->stop();
    m_timeout->stop();
    if (m_link) disconnect(m_link, nullptr, this, nullptr);
    emit finished(ok, message);
}
