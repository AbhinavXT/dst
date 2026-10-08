#ifndef FAULTTIMELINE_H
#define FAULTTIMELINE_H

// =============================================================================
//  Fault timeline (session 174) — Tools ▸ Monitor ▸ Fault timeline…
//  -----------------------------------------------------------------------------
//  Every fault the tab shows, as a bar from raised to cleared, one row per
//  card / module, under the loco's mode -- so "what was faulted when it went
//  to System_Failure" is read off one picture.
//
//    NMS faults (@nmsflt)  each frame lists what its reporting subsystem
//                          asserts now: a fault is raised when it first
//                          appears, cleared when a later frame FROM THE SAME
//                          SUBSYSTEM no longer lists it, open at the end of
//                          the log otherwise (the fault panel's rule)
//    LCU elements (@ccsys) lcu_elem_status1/2 bits (can0, can1, radio1,
//                          radio2, gps1, gps2): a bar while one reads 0
//    modes                 the missions' modes (missionreport.h: the DMI's,
//                          the ARP's while the DMI is silent)
// =============================================================================

#include <QString>
#include <QStringList>
#include <QVector>

class LogModel;

namespace FaultTimeline {

struct Bar {
    QString row;                // "VCC Mc-2", "LCU-1 radio1"
    QString fault;              // "safety error", "down"
    qint64  fromMs = 0, toMs = 0;
    bool    open = false;       // still raised when the log ends
    QString source;             // "NMS" / "CCSYS"
};

struct ModeSpan {
    qint64  fromMs = 0, toMs = 0;
    QString mode;               // "4 (Full_Supervision)"
};

struct Timeline {
    qint64 fromMs = 0, toMs = 0;
    QStringList rows;           // in order of first fault
    QVector<Bar> bars;
    QVector<ModeSpan> modes;
    QVector<qint64> failures;   // each System_Failure onset
    QVector<Bar> activeAt(qint64 ms) const;
};

Timeline build(const LogModel *model);

// Plain HTML: per System_Failure, the faults active then (for the window and
// the tests).
QString failuresHtml(const Timeline &t);

}  // namespace FaultTimeline

#endif // FAULTTIMELINE_H
