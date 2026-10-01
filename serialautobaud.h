#ifndef SERIALAUTOBAUD_H
#define SERIALAUTOBAUD_H

// =============================================================================
//  SerialAutoBaud (session 111) — find the rate a card is printing at.
//  -----------------------------------------------------------------------------
//  Try each standard rate for a moment (a second by default) and pick the
//  one where the most lines decode as @ capture frames — the same test
//  line health uses (SerialLineHealth::lineDecodes). At the wrong rate the
//  bytes come out as garbage with no '@' and no shape, so the right rate is
//  not a close call.
//
//  It probes on a link of its own, so the garbage read at wrong rates never
//  reaches the console tab or the port's line health. The port must not be
//  open elsewhere while it runs. Common rates are tried first, and it stops
//  early once a rate is plainly right (3+ lines, 90%+ decoding). When no
//  rate decodes at all it says so rather than guessing — the card may be
//  silent, or not print @ lines.
// =============================================================================

#include <QObject>
#include <QString>
#include <QVector>

#include "seriallink.h"

class QTimer;

class SerialAutoBaud : public QObject
{
    Q_OBJECT
public:
    struct Result { qint32 rate = 0; int lines = 0; int decoded = 0; };

    explicit SerialAutoBaud(QObject *parent = nullptr);

    // The order rates are tried in: the ones cards are usually set to first.
    static QVector<qint32> defaultOrder();
    // The best of `results`: most decoded lines, then the higher share;
    // rate 0 when none decoded anything.
    static Result pickBest(const QVector<Result> &results);
    static bool plainlyRight(const Result &r) { return r.decoded >= 3 && r.decoded * 10 >= r.lines * 9; }
    // "57600: 12 of 12 lines decode (115200: 0 of 9 · 9600: 0 of 2)".
    static QString summary(const QVector<Result> &results, const Result &best);

    // False if one is running or `base.portName` is empty. `base` gives the
    // port and the other line settings; only the baud is varied.
    bool start(const SerialConfig &base, const QVector<qint32> &rates = defaultOrder(), int dwellMs = 1000);
    void stop();
    bool isRunning() const { return m_running; }
    QVector<Result> results() const { return m_results; }

signals:
    void trying(qint32 rate, int index, int count);
    // ok: `rate` decoded; otherwise `message` says why not.
    void finished(bool ok, qint32 rate, const QString &message);

private:
    void next();
    void endDwell();
    void finish(bool ok, qint32 rate, const QString &message);

    SerialLink      *m_link = nullptr;
    QTimer          *m_dwell = nullptr;
    SerialConfig     m_base;
    QVector<qint32>  m_rates;
    QVector<Result>  m_results;
    int              m_index = -1;
    bool             m_running = false;
};

#endif // SERIALAUTOBAUD_H
