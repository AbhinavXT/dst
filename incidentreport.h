#ifndef INCIDENTREPORT_H
#define INCIDENTREPORT_H

// =============================================================================
//  Incident report pack (session 97) — Tools ▸ Monitor ▸ Incident report…
//  -----------------------------------------------------------------------------
//  One self-contained HTML file about a chosen moment on one tab: the loco
//  pilot's DMI as it stood at the key moments around it, the speed/permitted
//  /target plot over the window, mode changes, EB/FSB applications, SLRP
//  reject-rule matches, and the raw frames in the window — what a reviewer
//  needs to read an incident without reopening the capture. Same rule as
//  RunReport (runreport.h): OBSERVED only, no verdict.
//
//  THE WINDOW
//    [atMs - beforeMs, atMs + afterMs], one tab (one loco). Every section is
//    computed over rows in that window only.
//
//  KEY MOMENTS (DMI time travel)
//    The window's start and end, plus the onset of every mode change and
//    every EB/FSB application inside it — each rendered from the tab's own
//    latest @dmi at or before that instant (dmiMomentFromModels; "at or
//    before" is the same rule the live DMI window's "Follow cursor" uses).
//    Capped at Options::maxDmiMoments in time order; the excess is dropped
//    (keyMomentsCapped says so) because rendering a DMI panel is not free.
//
//  EB/FSB
//    @dmi brake_type (schema enum dmiBrakeType): 3 = FULL_SERVICE_BRAKE,
//    4 = EMERGENCY_BRAKE. Tracked as episodes with RunReport::EpisodeTracker,
//    the same way RunReport tracks overspeed and emergency-status spells.
//
//  EVERYTHING ELSE
//    Reuses RunReport::summarise() with a window (mode changes, emergencies,
//    overspeed, tags, clock skew, faults, reject clauses) and
//    SpeedDistance::extract() with a window (the plot), so those two engines
//    stay the single source of truth for what they already compute — this
//    module does not re-decode what they already decode.
// =============================================================================

#include "runreport.h"
#include "speeddistance.h"

#include <QByteArray>
#include <QString>
#include <QVector>

class LogModel;

namespace IncidentReport {

struct Options {
    qint64 beforeMs     = 60000;   // window before atMs
    qint64 afterMs      = 60000;   // window after atMs
    int    maxDmiMoments = 8;      // DMI panels rendered, at most
    int    maxRawFrames  = 500;    // raw lines listed, at most
    int    maxListRows   = 200;    // handed down to the HTML tables
};

// One rendered DMI panel, or the lack of one, at a moment of interest.
struct KeyMoment {
    qint64     ms = 0;
    QString    label;        // "Window start" / "Mode: Full_Supervision -> Trip" / "EB applied"
    bool       hasDmi = false;
    QString    dmiKey;       // the loco the frame belongs to ("1_1")
    QByteArray dmiPng;       // rendered DmiView, PNG bytes; empty if !hasDmi
};

struct RawFrame { qint64 ms = 0; QString text; };

struct Summary {
    QString tabKey, tabName;
    qint64  atMs = 0, fromMs = 0, toMs = 0;
    bool    valid = false;    // the tab has at least one row in the window

    RunReport::Summary          run;             // windowed
    QVector<RunReport::Episode> brakeEpisodes;    // FSB/EB, from @dmi brake_type

    SpeedDistance::Trace speedTrace;   // windowed
    QByteArray           speedPlotPng; // rendered SpeedDistanceCanvas; empty if no trace

    QVector<KeyMoment> keyMoments;
    bool               keyMomentsCapped = false;

    QVector<RawFrame> rawFrames;
    int               rawFrameTotal = 0;   // rows in the window, before Options::maxRawFrames
};

// Builds the pack for one tab around `atMs`. `tabKey`/`tabName` are for the
// title only (mirrors RunReport::summarise's signature).
Summary build(LogModel *tabModel, const QString &tabKey, const QString &tabName,
              qint64 atMs, const Options &options = Options());

QString toHtml(const Summary &s, const Options &options = Options());

}  // namespace IncidentReport

#endif // INCIDENTREPORT_H
