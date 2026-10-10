#ifndef READERDIR_H
#define READERDIR_H

// =============================================================================
//  Reader direction (session 194) — @rdir READER_INFO, and RDSO FRS 18
//  -----------------------------------------------------------------------------
//  The firmware sends one @rdir per RFID tag read, after deciding the
//  direction (SOS_handoff/04_READER_DIRECTION_LOGGING.md):
//
//      reader_id u8 · tag_id u16 LE · reader_dir u8 · movement_dir u8
//
//  directions 0 U (undefined), 1 N (nominal), 2 R (reverse). The RDSO test
//  cases FRS 18.1-18.7 ("direction determination based on tag read by RFID
//  reader-1 and reader-2") tabulate, step by step: Reader-1 direction,
//  Reader-2 direction, OVK (onboard) direction, and the tag reported to the
//  Stationary KAVACH. This unit reads the first three from @rdir and the
//  fourth from the loco's own ARP (LAST_RFID_TAG, when it changes after a
//  read), and checks the rows against the RDSO table chosen.
//
//  OBSERVED, NOT JUDGED: a row is "seen" (at an observed read) or "not seen
//  in order". The OK / Not OK column stays the signatory's.
// =============================================================================

#include <QHash>
#include <QString>
#include <QVector>

class LogModel;

namespace ReaderDir {

enum Dir { U = 0, N = 1, R = 2 };
QString letter(int dir);                 // "U" / "N" / "R"; the number otherwise

struct Read {
    int     row = -1;                    // source row in the tab
    qint64  ms = 0;
    int     reader = 0;                  // 1 or 2
    quint16 tag = 0;
    int     readerDir = 0;               // as sent: this reader's direction
    int     movementDir = 0;             // as sent: the loco's (OVK) direction
    int     r1Dir = 0, r2Dir = 0;        // each reader's direction after this read
    int     reported = -1;               // the tag the next ARP reported, when it changed; -1 none
    int     reportedRow = -1;
};

struct Log {
    QVector<Read>           reads;
    QHash<quint16, double>  tagLocM;     // from @rfid: a tag's absolute location
    int                     arpFrames = 0;
    int                     badFrames = 0;   // @rdir not 5 bytes
};

// Bytes of one @rdir: false for anything but 5 bytes.
bool decode(const QByteArray &b, Read *out);

// Every @rdir, @arp and @rfid row of `model`.
Log extract(const LogModel *model);
bool hasRdir(const LogModel *model);     // cheap: stops at the first

// The reads from index `from` to `to` (both included; -1: to the last), both
// readers' directions starting at U again: a test case starts from a fresh
// start of mission. Bounding `to` keeps a later test run in the same log
// from satisfying this one's rows.
QVector<Read> fromRead(const Log &log, int from, int to = -1);

// The tags in R1, R2, ... order: by location from @rfid where every tag has
// one, else by tag id. `byLocation` says which.
QVector<quint16> tagOrder(const Log &log, const QVector<Read> &reads, bool *byLocation = nullptr);
QString tagLabel(int tag, const QVector<quint16> &order);   // "R2 (102)"; "" for -1

// ---- RDSO FRS 18 ---------------------------------------------------------------------
struct ExpectedRow { int r1 = 0, r2 = 0, ovk = 0; int tag = 0; };   // tag: 1 = R1 ... 0 = none
struct TestCase {
    QString id;                          // "18.4"
    QString title;
    QString input;                       // the input table, in words
    QVector<ExpectedRow> rows;
};
const QVector<TestCase> &frs18();

// For each expected row, the index of the observed read it was seen at
// (in order: each after the previous one's), or -1 not seen. A row is seen
// at a read whose reader-1, reader-2 and OVK directions are the row's and
// whose reported tag is the row's (none for a blank).
QVector<int> match(const TestCase &tc, const QVector<Read> &reads, const QVector<quint16> &order);

}  // namespace ReaderDir

#endif // READERDIR_H
