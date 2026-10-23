#ifndef TABHEALTH_H
#define TABHEALTH_H

// =============================================================================
//  tabhealth.h -- the status bar's chip for the tab on show (session 336)
//  ---------------------------------------------------------------------------
//  "21_1 · 12/s · 0.4 s ago · 3 gaps". The Sources panel has said rate and
//  silence per source since session 119, but it is closed on a first run
//  (session 275) and is one more thing to look at. This is the tab you are
//  reading, always in view, in the same thresholds' colours (Settings ▸
//  offline warn / error), plus what the panel never said: how many times a
//  kind of line came more than twice its usual gap late (the Δt column's
//  ▲, session 288), i.e. lines that may be missing.
// =============================================================================

#include <QString>

namespace TabHealth {

enum class Tone { Idle, Ok, Warn, Fail };

struct Reading {
    QString name;               // the tab's name
    double  rate = 0;           // rows per second over the last tick
    qint64  silentMs = -1;      // since the last row; -1 = nothing yet
    int     lateGaps = 0;       // ▲ gaps since the tab opened
    qint64  sinceLastGapMs = -1;  // message time from the last ▲ to the newest row; -1 = none
    int     warnSec = 10, errSec = 30;   // Settings' offline thresholds (0 = off)
};

struct Shown {
    QString text;
    QString tip;
    Tone    tone = Tone::Idle;
};

// A ▲ gap keeps the chip amber for this long (message time) after it.
constexpr qint64 kGapRecentMs = 60'000;

Shown describe(const Reading &r);

}  // namespace TabHealth

#endif  // TABHEALTH_H
