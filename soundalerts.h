#ifndef SOUNDALERTS_H
#define SOUNDALERTS_H

// =============================================================================
//  soundalerts.h -- View ▸ Sounds (session 341)
//  ---------------------------------------------------------------------------
//  The system sound for the events that matter while nobody is looking at
//  the screen. A watch can already beep when it fires (session 82), so
//  anything a query can say is covered there; these are the three it
//  cannot: a source going offline, the loco and station clocks falling out
//  of each other's accept window, and an error notification. Each is a
//  switch, off until ticked, kept in the INI (sound/<id>).
//
//  At most one sound every two seconds, whatever asks: a burst of errors
//  is one sound, not a drone.
// =============================================================================

#include <QString>

namespace SoundAlerts {

enum class Event { SourceOffline, ClockSkew, ErrorNote };

QString id(Event e);      // "source_offline", the INI key's tail
QString title(Event e);   // "A source goes offline", the menu's text

bool enabled(Event e);
void setEnabled(Event e, bool on);

constexpr qint64 kQuietMs = 2000;

// Sound for `e` at `nowMs` if it is enabled and the last sound was at least
// kQuietMs ago. Returns whether it sounded.
bool ring(Event e, qint64 nowMs);

// Tests: what sounds instead of the system beep (null = the beep), and
// forget the last sound.
void setSounder(void (*sounder)());
void resetQuiet();

}  // namespace SoundAlerts

#endif  // SOUNDALERTS_H
