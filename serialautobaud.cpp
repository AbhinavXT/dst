#include "serialautobaud.h"

#include "serialmanager.h"     // SerialLineHealth::lineDecodes

#include <QStringList>
#include <QTimer>

SerialAutoBaud::SerialAutoBaud(QObject *parent) : QObject(parent)
{
    m_link = new SerialLink(this);
    m_link->setIdleFlushMs(300);
    connect(m_link, &SerialLink::lineReceived, this, [this](const QByteArray &line, qint64) {
        if (!m_running || m_index < 0 || m_index >= m_results.size() || line.trimmed().isEmpty()) return;
        Result &r = m_results[m_index];
        ++r.lines;
        if (SerialLineHealth::lineDecodes(line)) ++r.decoded;
    });
    m_dwell = new QTimer(this);
    m_dwell->setSingleShot(true);
    connect(m_dwell, &QTimer::timeout, this, &SerialAutoBaud::endDwell);
}

QVector<qint32> SerialAutoBaud::defaultOrder()
{
    return { 115200, 9600, 57600, 38400, 19200, 230400, 460800, 921600, 4800, 2400, 14400, 1200 };
}

SerialAutoBaud::Result SerialAutoBaud::pickBest(const QVector<Result> &results)
{
    Result best;
    for (const Result &r : results) {
        if (r.decoded == 0) continue;
        const bool better = r.decoded > best.decoded
            || (r.decoded == best.decoded && best.lines > 0
                && double(r.decoded) / r.lines > double(best.decoded) / best.lines);
        if (better) best = r;
    }
    return best;
}

QString SerialAutoBaud::summary(const QVector<Result> &results, const Result &best)
{
    QStringList others;
    for (const Result &r : results) {
        if (r.rate == best.rate && best.rate) continue;
        others << QObject::tr("%1: %2 of %3").arg(r.rate).arg(r.decoded).arg(r.lines);
    }
    const QString rest = others.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(others.join(QStringLiteral(" · ")));
    if (!best.rate) return QObject::tr("no rate gave decodable @ lines%1").arg(rest);
    return QObject::tr("%1: %2 of %3 lines decode%4").arg(best.rate).arg(best.decoded).arg(best.lines).arg(rest);
}

bool SerialAutoBaud::start(const SerialConfig &base, const QVector<qint32> &rates, int dwellMs)
{
    if (m_running || base.portName.trimmed().isEmpty() || rates.isEmpty()) return false;
    m_base = base;
    m_rates = rates;
    m_results.clear();
    m_index = -1;
    m_dwell->setInterval(qMax(50, dwellMs));
    m_running = true;
    next();
    return true;
}

void SerialAutoBaud::stop()
{
    if (!m_running) return;
    m_link->close();
    finish(false, 0, tr("stopped"));
}

void SerialAutoBaud::next()
{
    if (!m_running) return;
    ++m_index;
    if (m_index >= m_rates.size()) {
        const Result best = pickBest(m_results);
        finish(best.rate != 0, best.rate, summary(m_results, best));
        return;
    }
    Result r;
    r.rate = m_rates.at(m_index);
    m_results << r;
    emit trying(r.rate, m_index, m_rates.size());
    SerialConfig c = m_base;
    c.baud = r.rate;
    if (!m_link->open(c)) {
        // The port itself is the problem, not the rate: no use trying more.
        finish(false, 0, tr("%1 would not open: %2").arg(c.portName, m_link->errorText()));
        return;
    }
    m_dwell->start();
}

void SerialAutoBaud::endDwell()
{
    if (!m_running) return;
    m_link->close();                 // delivers a held partial line into this rate's count
    if (plainlyRight(m_results.last())) {
        const Result best = pickBest(m_results);
        finish(true, best.rate, summary(m_results, best));
        return;
    }
    next();
}

void SerialAutoBaud::finish(bool ok, qint32 rate, const QString &message)
{
    if (!m_running) return;
    m_running = false;
    m_dwell->stop();
    m_link->close();
    emit finished(ok, rate, message);
}
