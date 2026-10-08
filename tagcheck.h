#ifndef TAGCHECK_H
#define TAGCHECK_H

// =============================================================================
//  RFID tag check (session 180)
//  -----------------------------------------------------------------------------
//  Each RFID tag the loco read, against what else the capture says about it:
//    main / duplicate   @rfid duplication: the Main Tag and its Duplicate Tag
//                       were each read, or not
//    duplicate missing  NMS DUPLICATE_MISSING_RFID named the tag (the loco
//                       reporting its duplicate was not read)
//    on the route       an SLRP route tag list (tag[i]) named it
//  And a route tag PASSED WITHOUT A READ: two consecutive tag reads A then
//  B, where the latest route list before B holds A and B in that order with
//  tags between them -- those between were passed and not read. Only the
//  order of the list is used; no positions are computed.
//  Observed, not judged: a tag with no duplicate read is listed as such.
// =============================================================================

#include <QString>
#include <QStringList>
#include <QVector>

class LogModel;

namespace TagCheck {

struct Tag {
    QString tag;                 // RFID unique id
    qint64  firstMs = 0;
    bool    main = false, duplicate = false;
    qint64  duplicateMissingMs = 0;   // NMS DUPLICATE_MISSING_RFID, first time; 0 = not reported
    bool    onRoute = false;
};

struct Skipped {
    qint64  ms = 0;              // when B was read
    QString from, to;            // the reads A, B
    QStringList passed;          // the route tags between them
};

struct Report {
    QVector<Tag>     tags;       // in order of first read
    QVector<Skipped> skipped;
    QStringList      duplicateMissingOther;   // tags NMS named that were not read in this window
    int duplicatesRead() const;
    int duplicatesReportedMissing() const;
};

Report build(const LogModel *model, qint64 fromMs = 0, qint64 toMs = 0);
QString toHtml(const Report &r, int maxRows = 200);

}  // namespace TagCheck

#endif // TAGCHECK_H
