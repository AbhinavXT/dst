#ifndef STATIONANSWER_H
#define STATIONANSWER_H
// =====================================================================
//  stationanswer.{h,cpp} -- session 378: the Packet Maker answering a
//  running loco as its station would.
//
//    ARP  -> AAP : the operator's AAP preset, with FRAME_NUM and
//                  DEST_LOCO_ID from the ARP and a fresh station random.
//    LSRP -> SLRP: the SLRP in the form, with FRAME_NUM from the LSRP (so
//                  every FRAME_OFFSET is 0) and LAST_REF_RFID moved to the
//                  tag the loco last read.
//                  DIST_PKT_START and APPR_SIG_DIST are both counted from
//                  the ref tag, so both shrink by the distance the ref moved
//                  and the start signal and the signal stay where the form
//                  put them. Every other distance counts from the start
//                  signal and is unchanged.
//
//  Seen in replay/2026-10-08/loco_1_1_08102026_105200.cap: the station moved
//  ref 18 (166864 m) -> 906 (167064 m) and sent DIST_PKT_START 0 -> -200,
//  APPR_SIG_DIST 1116 -> 916. travel = -1 when PKT_DIR == 2, else +1.
//
//  Pure values; the dialog sends. A reply that cannot be made correctly
//  is refused with the reason, never clamped.
// =====================================================================
#include <QHash>
#include <QString>
#include <QVector>

#include "schema/schemaencoder.h"   // Schema::SubEntry
#include "stationlayout.h"

namespace StationAnswer {

// Unique tag id -> abs location, for every tag in the layout that decodes.
QHash<int, qint64> tagLocations(const StationLayout::Layout &l);

// The AAP answering `arp`: `aap` (the preset's fields) with FRAME_NUM and
// DEST_LOCO_ID from the ARP and STN_RND_NUM_RS = stnRandom.
QHash<QString, qint64> aapFor(QHash<QString, qint64> aap, const QHash<QString, qint64> &arp,
                              quint16 stnRandom);

struct Reply {
    bool    ok = false;
    QString why;        // refused: the reason
    QString note;       // made: what was recomputed, one line
};

// Updates `slrp` / `subs` (the form's copy, its LAST_REF_RFID in `tags`) to
// answer `lsrp`. `refTag` is the ref tag in use, in and out: moved to the
// LSRP's LAST_RFID_TAG when that tag is in `tags`, kept otherwise.
Reply slrpFor(const QHash<QString, qint64> &lsrp, const QHash<int, qint64> &tags, int *refTag,
              QHash<QString, qint64> *slrp, QVector<Schema::SubEntry> *subs);

}  // namespace StationAnswer

#endif  // STATIONANSWER_H
