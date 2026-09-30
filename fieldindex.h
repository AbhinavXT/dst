#ifndef FIELDINDEX_H
#define FIELDINDEX_H

// =============================================================================
//  FieldIndex
//  -----------------------------------------------------------------------------
//  Makes a message's DECODED fields queryable.
//
//  WHY
//    The query language can match message text, source, severity, hex and
//    length — everything except the thing the schema actually works out.
//    `SIG_OV` is a bit inside a binary packet; searching for the string
//    "SIG_OV" finds text diagnostics that happen to mention it and misses
//    every real occurrence.
//
//    That gap matters most for acceptance testing. SIF 0533 is written in
//    terms of field values — "SIG_OV shall be set 1", "Info_Ack as 2",
//    "frame numbers ending 011" — and 86 of the schema's field names appear
//    in that document by name. The console decodes all of them and cannot
//    be asked about any of them.
//
//  LAZY, AND CACHED ON THE ENTRY
//    Schema-decoding every message at ingest would be far too expensive:
//    it is orders of magnitude dearer than anything else on that path, and
//    the overwhelming majority of messages are never queried by field.
//    So decoding happens on first field-query touch and the result is
//    cached on the LogEntry, which is built once and inspected many times.
//
//    The cache is mutable state on a shared object. Every caller today is
//    on the GUI thread — the archive-search worker builds its OWN entries
//    and discards them — so no lock is taken. If that ever changes this
//    needs revisiting; see the note on the cache member in logentry.h.
// =============================================================================

#include <QString>

#include "logentry.h"

namespace FieldIndex {

// Decode `e` if it has not been decoded yet. Cheap on repeat calls.
// Returns false if the entry carries nothing decodable (a text diagnostic,
// a synthetic banner) — that is a normal outcome, not an error.
bool ensureDecoded(const LogEntry &e);

// Rendered value of a decoded field, or a null QString if absent.
// Name matching is case-insensitive and ignores the schema's leading
// indent, so `field:sig_ov` finds "  SIG_OV".
QString value(const LogEntry &e, const QString &fieldName);

// Numeric form of the same, using the display string's leading number
// ("42", "42 km/h", "0x2A"). `ok` is false when the field is absent or
// its value is not numeric — an enum label, say.
double number(const LogEntry &e, const QString &fieldName, bool *ok);

// Every decoded field name on this entry, for completion and diagnostics.
QStringList names(const LogEntry &e);

// The capture type of this entry ("lsrp", "dip1", …), or empty when the
// message is not a capture frame. Used to scope a field to the packet the
// catalogue says carries it, so a name that exists in two packets does not
// silently match in the wrong one.
QString captype(const LogEntry &e);

}  // namespace FieldIndex

#endif // FIELDINDEX_H
