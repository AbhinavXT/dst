#ifndef LINFOEDIT_H
#define LINFOEDIT_H
// =============================================================================
//  linfoedit.{h,cpp} -- add or remove one LOCO_INFO member in kavach.xml
//  (session 307: Loco Configuration ▸ ☰ ▸ Fields).
//
//  WHAT IT EDITS
//    The text of <packet name="LINFO">, one line at a time, the way a person
//    would: the new <field> line goes right after the field it follows, with
//    that line's indent and column alignment; a removed field's line goes,
//    and nothing else in the file changes (comments, spacing, the other
//    packets, the line endings). The file is NOT parsed and re-serialised,
//    which would rewrite all 1700 lines.
//
//  WHAT IT CHECKS
//    Before anything is returned, the edited text is read back with the
//    editor's own Layout: it must still be a valid LOCO_INFO, the new field
//    must be at the place asked, of the size asked, and the struct must
//    have grown (or shrunk) by exactly that size. Otherwise the edit is
//    refused, with the reason, and the file is untouched.
//
//  WHAT IT CANNOT DO
//    Change the firmware or the loco_config tool. The C struct has to gain
//    (or lose) the same member at the same place; until it does, the loco
//    and the editor disagree on the layout. The dialog says so, and
//    sync_linfo.py checks it (ADDING_A_FIELD.md).
// =============================================================================
#include "lococonfigcore.h"

#include <QByteArray>
#include <QString>

namespace LinfoEdit {

struct NewField {
    QString        name;              // C member name: [A-Za-z_][A-Za-z0-9_]*
    LocoInfo::Kind kind = LocoInfo::Kind::U8;
    int            charCount = 0;     // Kind::Char: the array size (>= 2)
    QString        afterKey;          // the field it follows (a key of the layout)
    QString        group;             // ui-group, plain text; empty = "Other"
    QString        note;              // ui-note, plain text
    QString        defaultText;       // ui-default, as the editor would accept it typed
    bool           hex = false;       // hexup (integers only)
};

// Bytes the new field takes.
int sizeOf(const NewField &field);

// Why this field cannot be added to `layout`, or empty.
QString problem(const LocoInfo::Layout &layout, const NewField &field);

// The <field .../> element alone, attributes escaped (for the preview).
QString element(const NewField &field);

struct Result {
    bool       ok = false;
    QString    error;
    QByteArray text;        // the whole edited file
    int        line = 0;    // 1-based: the line added, or the line removed
    QString    lineText;    // that line, as it is (was) in the file
    int        oldSize = 0; // LOCO_INFO body bytes, before and after
    int        newSize = 0;
};

// `schemaText` is the whole kavach.xml; `layout` is what it holds now.
Result addField(const QByteArray &schemaText, const LocoInfo::Layout &layout, const NewField &field);
Result removeField(const QByteArray &schemaText, const LocoInfo::Layout &layout, const QString &key);

// Fields the editor will not remove: loco_info_crc (computed, must be last)
// and vcc_crc (the send bar and its confirmation name it). Empty = removable.
QString removeProblem(const LocoInfo::Layout &layout, const QString &key);

}  // namespace LinfoEdit

#endif  // LINFOEDIT_H
