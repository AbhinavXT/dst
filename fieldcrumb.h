#ifndef FIELDCRUMB_H
#define FIELDCRUMB_H

// =============================================================================
//  fieldcrumb.h -- the breadcrumb over the decoded fields (session 333)
//  ---------------------------------------------------------------------------
//  "21_1 › SLRP › subpkt[3] TSR_PROFILE › TSR_ID": where the selected field
//  sits. In an SLRP of 120 rows, a TSR_ID row looks the same in every
//  sub-packet; the crumb says which one, and each part is a link back up.
//
//  The decoded rows are flat, nested only by their leading spaces: a
//  sub-packet header ("subpkt[N] NAME @byte B") at the left, its fields
//  two spaces in, a flag field's bits two more. A row's parents are the
//  rows above it with less indent. The envelope rows ("type", "CRC") sit
//  at the left too, but are not parents of the packet's fields.
// =============================================================================

#include <QString>
#include <QVector>

struct FieldRow;

namespace FieldCrumb {

// Rows that contain `row`, outermost first. Empty for a top-level field.
QVector<int> parents(const QVector<FieldRow> &rows, int row);

// A row as a crumb part: trimmed, and a sub-packet header without its
// "@byte B" ("subpkt[3] TSR_PROFILE").
QString label(const FieldRow &r);

}  // namespace FieldCrumb

#endif  // FIELDCRUMB_H
