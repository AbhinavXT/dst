#ifndef TRACKDIAGRAM_H
#define TRACKDIAGRAM_H

// =============================================================================
//  Track diagram by absolute location (session 99) — Tools ▸ Monitor ▸
//  Track diagram…
//  -----------------------------------------------------------------------------
//  One tab, one line: a linear diagram (no map, no internet) of the track the
//  loco actually reported running on — RFID tags, signals, the movement
//  authority end — with the loco riding it over time and events pinned where
//  they happened.
//
//  THE AXIS
//    Absolute location, the same scale SpeedDistance already plots against
//    (abs_loco_loc / @dmi, falling back to @lsrp): one loco's own location is
//    self-consistent across a session (unlike session 98's two-loco gap,
//    which compares two DIFFERENT locos' origins — there is only one origin
//    here, so no anchoring problem to solve).
//
//  RFID TAGS
//    @rfid frames with a real absolute-location fix (CaptureDecoder::
//    decodeRfid; the same "0 and 0x7FFFFF both mean no fix" rule
//    ReplayWindow's track axis uses). First fix per tag id only — a tag does
//    not move.
//
//  SIGNALS AND THE MOVEMENT AUTHORITY END
//    Neither has an absolute-location field anywhere in the schema: @dmi
//    gives appr_sig_dist (distance AHEAD OF THE LOCO NOW) and ma_w_r_t_sig
//    (distance ahead of THAT SIGNAL, dmiAssumptions() in dmipanel.cpp). So:
//        signal location = loco's location that frame + appr_sig_dist
//        MA end location = signal location + ma_w_r_t_sig
//    (targetLocation(), the same helper SpeedDistance uses to place a
//    target), both walked in the trace's direction of travel. Decoded via
//    dmiStateFromCapture() (dmipanel.h) — the live DMI panel's own decode,
//    not a second copy of its field lookups or the signal-name regex.
//
//    The SAME physical signal is reported on every frame while it is ahead,
//    at a slightly different computed location each time (the loco's own
//    estimate moves). Deduplicated by name, keeping the reading with the
//    SMALLEST appr_sig_dist — the closest, and so the most accurate, fix on
//    where it actually is. Its MA end is taken from that same frame, so the
//    two numbers are not stitched from different instants.
//
//  EVENTS, PINNED BY LOCATION
//    RunReport::summarise() (mode changes, overspeed, emergency-status) and
//    TwoLocoView::extractEvents() (SoS, head-on, rear-end — single-tab use
//    is fine; pairing is Two-loco view's concern, not this module's) are
//    read UNWINDOWED over the whole tab — no new decoding. Each event's own
//    row (or, for faults, which carry no row, its time) is matched against
//    the trace's samples for "the loco's last known location at or before
//    this": the same at-or-before rule DMI time travel (dmitimetravel.h)
//    uses for a frame, applied to a location instead.
//
//    Reject-rule findings are NOT pinned: RunReport::Summary keeps only
//    per-clause COUNTS (rejectClauses), not the row of each match, so there
//    is nothing to look a location up against without changing RunReport's
//    output shape — left for later rather than guessed at here.
// =============================================================================

#include "speeddistance.h"

#include <QString>
#include <QVector>

class LogModel;

namespace TrackDiagram {

struct RfidMark {
    qint64 epochMs = 0;
    double locM    = 0.0;
    qint64 uniqueId = 0;
    int    type     = 0;     // RfidInfo::type (9 normal, 10 LC gate, 11 adjacent-line, 12 adjustment)
    int    row      = -1;
};

struct SignalMark {
    QString name;
    double  locM        = 0.0;
    int     aspect       = 0;
    bool    hasMa        = false;
    double  maEndLocM    = 0.0;
    qint64  epochMs       = 0;   // the frame this reading (and its MA) came from
    int     row          = -1;
};

struct EventMark {
    qint64  epochMs = 0;
    double  locM    = 0.0;
    QString kind;      // "mode" / "overspeed" / "emergency" / "fault" / "sos" / "head-on" / "rear-end"
    QString label;
    int     row     = -1;
};

struct Diagram {
    QString tabKey, tabName;
    SpeedDistance::Trace trace;     // unchanged from SpeedDistance::extract()
    QVector<RfidMark>   tags;
    QVector<SignalMark> signalMarks;
    QVector<EventMark>  events;
    double minLocM = 0.0, maxLocM = 1.0;   // the trace's own span, widened by tags/signals/MA
    bool isEmpty() const { return trace.isEmpty(); }
};

Diagram build(const LogModel *model, const QString &tabKey, const QString &tabName, int maxRows = 200000);

}  // namespace TrackDiagram

#endif // TRACKDIAGRAM_H
