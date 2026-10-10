#ifndef PLANRUN_H
#define PLANRUN_H

// =============================================================================
//  Plan vs run (session 198) — the RFID Tag Builder's Run tab.
//  -----------------------------------------------------------------------------
//  A tag route (the plan) against a loco's log (the run): every @rfid frame
//  the loco read, tag by tag, against the route's rows.
//
//    read           a frame with the tag's 16 bytes exactly
//    different      the same tag id and main/duplicate, other bytes: the
//                   fields that differ are named (a tag that was rewritten,
//                   moved or mis-programmed after the route was made)
//    not read       no frame of it in the log (or the window)
//    out of order   read before a tag that comes earlier in the route
//    not planned    tags read that are not in the route at all
//
//  Only the first read of each tag counts: a log with several passes over the
//  route is compared on the first. A route run in the opposite direction
//  reads its tags backwards and shows as out of order: set From/To to one
//  pass, or open the route's REV.
//
//  The other way round, routeFromRun() makes a route from what the loco read:
//  each tag (main, and its duplicate) in the order first read, exactly as
//  read. The start of a route for a stretch of track nobody wrote down.
// =============================================================================

#include "rfidtag.h"

#include <QString>
#include <QStringList>
#include <QVector>

class LogModel;

namespace PlanRun {

struct Read {
    qint64     ms = 0;
    QByteArray bytes;             // the 16 tag bytes
    int        unique = 0;
    bool       duplicate = false;
    int        reader = 0;        // the frame's reader-id byte (1, 2), session 205
};
// The @rfid frames of a log (or of [fromMs, toMs]), in order.
QVector<Read> readsOf(const LogModel *model, qint64 fromMs = 0, qint64 toMs = 0);

enum class State { Read, Different, NotRead, OutOfOrder };

struct Planned {
    int     row = 0;              // route row
    QString name;
    State   state = State::NotRead;
    qint64  firstMs = 0;          // first read (0 = none)
    QString detail;
};
struct Result {
    QVector<Planned> planned;
    QStringList      notPlanned;  // tags read that the route does not hold (name, first read order)
    QVector<qint64>  notPlannedMs;
    int count(State s) const;
};
Result compare(const RfidTag::Route &route, const QVector<Read> &reads);

QString stateText(State s);

RfidTag::Route routeFromRun(const QVector<Read> &reads);

}  // namespace PlanRun

#endif  // PLANRUN_H
