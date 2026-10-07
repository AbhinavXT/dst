#ifndef WATCHRULES_H
#define WATCHRULES_H
// =============================================================================
//  watchrules.{h,cpp} — ready-made watches (session 161).
//
//  Named Kavach conditions an operator ticks on in the Watch panel instead
//  of writing the query. Each is an ORDINARY watch underneath: the same
//  query language, shown in the panel, editable by removing and re-adding,
//  pasteable into the filter bar to see every frame it matches.
//
//  Each definition follows what the console already uses elsewhere, so a
//  ready-made watch and the report that counts the same thing agree:
//    EB / FSB applied   @dmi brake_type 4 / 3 (the incident report's EB/FSB
//                       episodes)
//    SoS                any @lsos frame (the two-loco view's SoS events)
//    TSR acted on       TSR_STATUS 2 — TSR entries act only then
//    Trip, System failure, Isolation   LOCO_MODE 7, 12, 13 (locoMode enum)
//    CRC failed         an @dmi or @rfid frame whose CRC did not match
//                       (not @ccsys / @dlsys: every recorded frame fails)
//
//  NOT HERE, AND WHY. A watch is a condition on ONE frame. "Mode changed"
//  needs the frame before it, and "frame too old / too early" needs the
//  arrival time against FRAME_NUM: neither is a condition on one frame.
//  They are not imitated with something that only looks like them.
// =============================================================================
#include <QString>
#include <QVector>

struct WatchRule {
    QString id;      // stable, for tests: "eb"
    QString label;   // the watch's name: "EB applied"
    QString expr;    // the query, exactly as a watch holds it
    QString why;     // one line: what it matches and where that comes from
};

namespace WatchRules {
const QVector<WatchRule> &all();
const WatchRule *byId(const QString &id);
}

#endif  // WATCHRULES_H
