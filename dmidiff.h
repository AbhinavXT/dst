#ifndef DMIDIFF_H
#define DMIDIFF_H

// =============================================================================
//  DmiDiff (session 371) — two DMI pictures compared, in words.
//  -----------------------------------------------------------------------------
//  What a reviewer looks for on a DMI is what is different: from the frame
//  before ("target speed 60 → 30", session 372's change list) or from the
//  other run at the same moment (session 371's two runs in step). Both are
//  the same comparison of two DmiStates, field by field, in the panel's own
//  terms (regions A-M of Annexure B), with the units the DMI shows.
//
//  "Steady" fields are the ones whose change is an event: mode, brake,
//  target, signal, messages, tag. Speed, distances and location move every
//  frame; they are compared too, and marked, so a change list can leave them
//  out and a side-by-side can keep them.
// =============================================================================

#include <QString>
#include <QVector>

struct DmiState;

namespace DmiDiff {

struct Item {
    QString key;      // "mode", "speed", ... (stable, for tests and filters)
    QString label;    // "Mode", "Speed", ...
    QString a, b;     // the two values as the DMI shows them ("FS", "45 km/h")
    bool continuous = false;   // speed, distances, location: change every frame
    QString text() const;      // "Mode FS → OS"
};

// Every field that differs between `a` and `b`, in panel order. Two invalid
// states give nothing; one invalid gives one "DMI" item ("no frame" → ...).
QVector<Item> compare(const DmiState &a, const DmiState &b);

// "none", "overspeed warning", "NB", "FSB", "EB", "LE"; the number otherwise.
QString brakeName(int brakeType);

}  // namespace DmiDiff

#endif // DMIDIFF_H
