#ifndef RFIDCHECK_H
#define RFIDCHECK_H

// =============================================================================
//  Tag route checks (session 196) — the RFID Tag Builder's Checks tab.
//  -----------------------------------------------------------------------------
//  What a route's tags say about themselves and each other, in the order the
//  loco meets them. Observed, not judged: a finding says what is there and,
//  where the specification says, what a loco would do with it. No distance
//  limits are invented: the specification gives none here, so gaps are
//  shown (the Δ column), not graded.
//
//    each tag      CRC-30 (a loco would not process a tag whose CRC fails);
//                  a type that is not a tag type; LC gate tags (type 10)
//                  noted as never seen in a capture
//    main + dup    a duplicate tag right after its main tag, further along
//                  the direction of travel, carrying the same fields; a main
//                  tag listed twice; a main tag with no duplicate (info)
//    location      along the direction: a tag behind or level with the one
//                  before it. Not across an adjustment tag (type 12), where
//                  the numbering may legitimately change, and not for "N/A"
//    TIN           where the TIN for this direction changes (info)
//    signals       a signal-foot tag for this direction with no signal; a
//                  signal whose foot tag is not a main tag of the route; a
//                  signal on a tag that is not a foot tag for this direction
//    adjustment    what an adjustment tag tells a loco running this way:
//                  dir_corr_1 read for nominal, dir_corr_2 for reverse, as
//                  tags_sim reads them (not confirmed against firmware)
//    simulator     (session 204) a route.xml / Configuration1.xml row's own
//                  attributes, which the RFID simulator moves and sends by
//                  (SimPreview): tag_type and tag_name against the tag,
//                  page digits (it joins page_y + page_x as written), rfid_id,
//                  abs_loc against what route.xml would write, a repeated
//                  rfid_id, and the abs_loc -> next_rfid_abs_loc chain (a
//                  row with no length, a gap, an overlap, the last row)
//
//  Location order, TIN and signals need the route's direction; with none set
//  they are skipped, and the first finding says so.
// =============================================================================

#include "rfidtag.h"

#include <QString>
#include <QVector>

namespace RfidCheck {

enum class Level { Attention, Info };

struct Finding {
    int     row = -1;            // route row (0-based); -1 = the route as a whole
    QString tag;                 // the tag's name, "" for the route
    Level   level = Level::Info;
    QString text;
};

QVector<Finding> check(const RfidTag::Route &route);

// Metres from the previous tag along the direction (+ = further along), for
// the route table's Δ column. Empty for the first row, an N/A location, and
// a row after an adjustment tag; plain difference when no direction is set.
QString deltaText(const RfidTag::Route &route, int row);

constexpr qint64 kNotApplicable = 8388607;   // 23-bit all-ones: "N/A"
constexpr int    kDuplicateGap  = 4;          // m: main to duplicate in the Hafizpet KAV_CONFIG routes

}  // namespace RfidCheck

#endif  // RFIDCHECK_H
