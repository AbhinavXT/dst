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

}  // namespace RfidExport

#endif  // RFIDEXPORT_H
