#ifndef ROWCARD_H
#define ROWCARD_H

// =============================================================================
//  rowcard.h -- the hover card on a log row (session 334)
//  ---------------------------------------------------------------------------
//  Hovering a capture row shows what the frame is and its key values (the
//  inspector card's: loco mode, speed, location, frame number, last tag,
//  packet type), its CRC, and whether a receiver would process it — without
//  selecting the row or opening the decode pane. Decoded only when asked
//  (a tooltip is asked for on hover), so the ingest path is untouched.
// =============================================================================

#include <QString>

namespace RowCard {

// Rich text for the row's tooltip; empty when `text` is not a capture line
// with bytes (the caller then shows what it showed before).
QString html(const QString &text);

}  // namespace RowCard

#endif  // ROWCARD_H
