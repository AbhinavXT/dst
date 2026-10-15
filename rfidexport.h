#ifndef RFIDEXPORT_H
#define RFIDEXPORT_H

// =============================================================================
//  Tag route exports (session 197) — the RFID Tag Builder's Export menu,
//  beyond route.xml (rfidtag.h).
//  -----------------------------------------------------------------------------
//  CONFIGURATION1.XML
//    The RFID simulator's file: <LOCO> with the station, its tracks and many
//    <route_data> blocks, kept by hand. A route goes INTO an existing one:
//    its two blocks (the route and its REV route, as route.xml writes them)
//    replace the blocks of the same route_name, or are added after the last
//    block. Everything else in the file is left exactly as it was, byte for
//    byte: the edit is a splice of text, not a rewrite of the document. The
//    result is read back and refused unless it parses and holds the route's
//    rows.
//
//  TEXT FILES (tags_sim's generate_xml, before it stopped writing them)
//    <first>_<last>_rfid.txt           ~rfid,tin,type,loc,entrySig,dir,1,0,exitSig^^
//    <first>_<last>_sigID.txt          ~signal,sigId,loc,footTag,#^^
//    <first>_<last>_tag_link_info.txt  per signal-to-signal stretch: entry,
//                                      exit, the tags, the metres between them
//                                      and each tag's duplicate side; then
//                                      the "sum dist" lines
//    Ported line by line from tags_sim (its quirks too: the direction of a
//    row comes from its next row's location; signals are found by their
//    order in the signals table). Checked against tags_sim's own outputs.
//    Duplicate tags are not listed (tags_sim leaves them out); they set
//    their main tag's duplicate side.
// =============================================================================

#include "rfidtag.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace RfidExport {

struct TextFiles {
    QString stem;                  // "<first>_<last>_"
    QStringList rfid, sigId, tagLinkInfo;
};
TextFiles textFiles(const RfidTag::Route &route);

// The route's blocks spliced into `config` (a Configuration1.xml). Empty and
// *err set when refused; *notes says what was replaced or added.
QByteArray mergeIntoConfiguration(const QByteArray &config, const RfidTag::Route &route,
                                  QString *err = nullptr, QStringList *notes = nullptr);

// Session 259: the route as a Configuration1.xml route, to paste in by hand:
// its two <route_data> blocks (the route and its REV route, the same blocks
// mergeIntoConfiguration() splices in), no XML declaration and no <route>
// wrapper, indented as Configuration1.xml's own blocks are. Read back inside
// a <route> before it is returned; empty and *err set when refused.
QByteArray configurationRoute(const RfidTag::Route &route, QString *err = nullptr);

// Session 281: one tag of the route, `index` in its rows, as the single
// <rfid_data .../> line route.xml writes for it in the route's own direction
// (rfid_id, tag_name, tag_type, abs_loc, next_rfid_abs_loc, track_id,
// page_x, page_y: the locations from its place in the route), unindented,
// to paste into a <route_data> by hand. Taken from toRouteXml()'s own text,
// so it is the same line route.xml holds; read back inside a <route_data>
// and refused unless it gives this tag's 16 bytes. Refused while the
// route's direction is unset, as route.xml is.
QByteArray tagRow(const RfidTag::Route &route, int index, QString *err = nullptr);

// Session 199: every <rfid_data> in `config` (a Configuration1.xml or a
// route.xml) whose CRC-30 fails gets the CRC its contents give: page_y
// rewritten in that element only (page_x and every data bit kept), the
// rest of the file byte for byte. *changes: one line per tag fixed,
// "<route>: <rfid_id>  page_y <old> -> <new>". Empty and *err set if the
// file has no rfid_data rows or the result does not read back with every
// CRC passing.
QByteArray fixCrcs(const QByteArray &config, QStringList *changes = nullptr, QString *err = nullptr);

}  // namespace RfidExport

#endif  // RFIDEXPORT_H
