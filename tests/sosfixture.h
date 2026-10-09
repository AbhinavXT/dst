#ifndef SOSFIXTURE_H
#define SOSFIXTURE_H

// The SYNTHETIC SoS captures (sessions 184-187): schema/fixtures/
// sos_synthetic_loco{1,2}.log, made by tests/sosgen from the logging code
// in SOS_handoff/01_SOS_LOGGING_PACKETS.md. No LKAVACH build logs @sos yet,
// so these prove the layout and the views, not the firmware.
//
// The scenario (sosgen.c) runs from 2026-10-09 10:00:00; atSos("10:00:10")
// is that moment in ms. loco 1 logs as tab "1_1", loco 2 as "2_1".

#include "logmodel.h"
#include "messagedispatcher.h"

#include <QDateTime>
#include <QFile>

namespace SosFixture {

inline qint64 atSos(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-09T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}

// Ingests `loco`'s fixture into `d` and returns its tab's model ("<loco>_1").
inline LogModel *load(MessageDispatcher &d, int loco)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/schema/fixtures/sos_synthetic_loco%1.log").arg(loco));
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        d.ingestLocal(quint8(loco), 1, l,
                      QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    d.drainNow();
    return d.modelForKey(QStringLiteral("%1_1").arg(loco));
}

}  // namespace SosFixture

#endif // SOSFIXTURE_H
