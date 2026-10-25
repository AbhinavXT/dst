#ifndef DMICHANGES_H
#define DMICHANGES_H

// =============================================================================
//  DMI changes (session 372) — what changed on a loco's DMI, frame to frame.
//  -----------------------------------------------------------------------------
//  The DMI is sent about once a second and mostly says the same thing. What
//  a reviewer looks for is the moment it changed: "Mode FS → OS",
//  "Brake none → EB", "Target speed 60 km/h → 30 km/h", a system message
//  appearing. Each @dmi frame of the loco is compared with the one before
//  (DmiDiff, session 371); a frame that changed something is one entry.
//
//  Speed, distances and location move every frame. They are left out unless
//  asked for (`withContinuous`), or every frame of a moving train would be a
//  change.
// =============================================================================

#include <QString>
#include <QStringList>
#include <QVector>

#include "dmidiff.h"

class LogModel;

namespace DmiChanges {

struct Change {
    qint64 ms = 0;                    // the @dmi frame that shows it
    int row = -1;                     // its row in the model
    QVector<DmiDiff::Item> items;     // what changed from the frame before
    QString text() const;             // "Mode FS → OS; Brake none → EB"
};

// The @dmi locos in `model` ("1_1", ...), sorted.
QStringList locos(const LogModel *model);

// Every change on loco `loco`'s DMI in `model`, in time order. The first
// frame is not a change.
QVector<Change> list(const LogModel *model, const QString &loco, bool withContinuous = false);

// The change after / before `ms` (strictly); null when none.
const Change *next(const QVector<Change> &changes, qint64 ms);
const Change *previous(const QVector<Change> &changes, qint64 ms);

}  // namespace DmiChanges

#endif // DMICHANGES_H
