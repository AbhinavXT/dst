#ifndef LCCHECK_H
#define LCCHECK_H

// =============================================================================
//  Level crossings approached (session 181)
//  -----------------------------------------------------------------------------
//  Each approach to a level crossing, as the DMI showed it (@dmi lc.id not 0:
//  its id, manning, auto-whistle flag and lc.distance), against the horn
//  as the DIO recorded it (@dip1 horn1_solenoid_no / horn2_solenoid_no
//  reading other than their usual value, as on the cab inputs timeline,
//  176). The question: did the horn sound while the loco approached?
//  Observed: an approach with no horn is listed as such, not judged; the
//  auto-whistle flag is the DMI's own.
// =============================================================================

#include <QString>
#include <QVector>

class LogModel;

namespace LcCheck {

struct Approach {
    QString lc;                  // "191 suf=1"
    QString manning;             // "Manned" / "Unmanned"
    QString autoWhistle;         // the DMI's lc.auto_whistle
    qint64  fromMs = 0, toMs = 0;
    int     fromDistM = 0, toDistM = 0;
    QVector<QPair<qint64, qint64>> horn;   // spells the horn solenoid was on during the approach
    bool    dioSeen = false;     // any @dip1 during the approach (else: horn not known)
};

QVector<Approach> build(const LogModel *model, qint64 fromMs = 0, qint64 toMs = 0);
QString toHtml(const QVector<Approach> &a, int maxRows = 200);

}  // namespace LcCheck

#endif // LCCHECK_H
