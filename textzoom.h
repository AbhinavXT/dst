#ifndef TEXTZOOM_H
#define TEXTZOOM_H
// =============================================================================
//  TextZoom -- one text size for the whole application (View ▸ Text size,
//  Ctrl + / Ctrl − / Ctrl 0).
//
//  WHAT IT SCALES, TOGETHER
//    1. The application font. Every widget that has not set a font of its
//       own follows it.
//    2. Every widget that HAS set its own font -- section titles, the
//       monospace hex and value columns, the flasher's big numbers. Their
//       size relative to the rest is kept: a title 1.5x the body stays
//       1.5x the body.
//    3. Every table's fixed row height. Log tables, the Live Loco Console,
//       the flash queue and the field tables all use fixed rows (fixed is
//       what keeps a 50 000-row log fast), so without this bigger text
//       would be cut off.
//
//    Windows already open are rescaled in place; windows opened later come
//    out at the new size, because UiStyle::monoFont() and
//    Settings::rowHeightFor() both apply factor().
//
//  HOW IT KNOWS A WIDGET'S "100 %" SIZE
//    From what it set last time: a widget's size divided by the factor in
//    effect when it was sized. Remembered per widget (a dynamic property),
//    and re-derived if something else has changed the size since -- a
//    density change re-sizing a table's rows, say -- so repeated zooming
//    never drifts.
//
//  PRESENTATION MODE adds a temporary boost of one or more steps on top of
//  the chosen size (setBoost); only the chosen size is saved.
// =============================================================================
#include <QObject>
#include <QVector>

namespace TextZoom {

// The sizes offered, in percent. 100 is the system font.
QVector<int> steps();

// Read the saved size and apply it. Call once at startup, after the theme
// and UiStyle have been applied. Until this runs, factor() is 1.0.
void initialise();
bool isInitialised();

int    percent();            // the chosen size (saved)
int    effectivePercent();   // chosen size plus any presentation boost
double factor();             // effectivePercent() / 100

void setPercent(int percent);   // snaps to the nearest step, saves, applies
void zoomIn();
void zoomOut();
void reset();                   // 100 %

// Presentation mode's extra steps (0 = none). Not saved.
void setBoost(int stepsUp);
int  boost();

// Re-apply the effective size to the application and every open window.
void apply();

// Emits changed() after every apply(), for anything that caches sizes.
class Notifier : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void changed();
};
Notifier *notifier();

}  // namespace TextZoom

#endif  // TEXTZOOM_H
