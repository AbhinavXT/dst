#ifndef STATIONINTERLOCK_H
#define STATIONINTERLOCK_H
// =====================================================================
//  stationinterlock.{h,cpp} -- session 441 on: the station's own
//  signalling, worked from where the locos say they are.
//
//  THE TRACK (Track): the layout's track circuits (StationLayout::circuits:
//  name, line, from startx to endx) and the line each tag number lies on
//  (the lines sheet).
//
//  OCCUPANCY (occupancy(), session 441): a loco occupies every track
//  circuit of its line that its train covers: from ABS_LOCO_LOC back
//  TRAIN_LENGTH against MOVEMENT_DIR (Nominal: rising km), as its last ARP
//  or LSRP says. Its line is the line of the tag it last read
//  (LAST_RFID_TAG). A loco whose tag lies on no line, or that has not said
//  where it is (ABS_LOCO_LOC 0, direction unidentified) occupies nothing,
//  and that is said. A train across a point onto another line is counted
//  on its tag's line only.
//
//  Pure values; Packet Maker keeps the locos and the State.
// =====================================================================
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include "stationkavach.h"
#include "stationlayout.h"

namespace StationInterlock {

struct Track {
    QVector<StationLayout::Circuit> circuits;
    QHash<int, QString> lineOfTag;     // tag number -> line (main tags; first line wins)
    bool isEmpty() const { return circuits.isEmpty(); }
};
Track trackOf(const StationLayout::Layout &l);

struct Occupancy {
    QHash<QString, int> by;            // track circuit -> the loco occupying it
    QStringList notes;                 // a loco that occupies nothing, and why
};
// Where each loco is by its last ARP / LSRP (StationKavach::note).
Occupancy occupancy(const Track &t, const QList<StationKavach::Loco> &locos);

}  // namespace StationInterlock

#endif  // STATIONINTERLOCK_H
