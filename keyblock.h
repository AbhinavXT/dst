#ifndef KEYBLOCK_H
#define KEYBLOCK_H

// =============================================================================
//  KeyBlock
//  -----------------------------------------------------------------------------
//  Decodes the AUTH_KEYS block the loco prints when it reports its
//  authentication key sets.
//
//  WHY THIS IS DIFFERENT FROM EVERY OTHER DECODER HERE
//    Everything else decodes ONE message. This is a multi-line block, and
//    each line arrives as its own UDP datagram — so by the time the console
//    sees them they are separate LogEntry rows with nothing tying them
//    together. Reading it therefore needs the lines correlated first, which
//    is why there is an assembler as well as a parser.
//
//  THE SHAPE, from key_info.txt:
//
//      typedef uint8_t KEY[16];
//      typedef struct { KEY_TIME start_time;
//                       KEY_TIME end_time;
//                       KEY      key[2];  } KEY_SET_INFO;
//
//    So a SET is two 16-byte keys with one validity window covering both.
//    The four hex lines under AUTH_KEYS are therefore TWO sets, not four
//    keys — lines 1-2 are set 0, lines 3-4 are set 1. Reading them as four
//    independent keys would pair every key with the wrong validity window.
//
//      AUTH_KEYS
//      3B 0C 84 47 ... C3        set 0, key 0
//      6A 2B 03 74 ... F1        set 0, key 1
//      F5 6C D7 34 ... C8        set 1, key 0
//      80 C5 00 F9 ... 93        set 1, key 1
//      KEY IDX: [0] START TIME: [26][5][24][0]
//      KEY IDX: [0] END TIME:   [26][9][1][0]
//      KEY IDX: [1] START TIME: [26][5][24][0]
//      KEY IDX: [1] END TIME:   [26][9][11][0]
//      KEY_IDX: [0] START: [1779580800] END:[1788220800] CURR:[1787569048]
//
//    KEY_TIME is [yy][mm][dd][hh]; the epoch line on the end agrees with it
//    exactly, which is what confirms the field order.
//
//  WHAT IT IS FOR
//    Key validity is not a curiosity: REMAINING_KEYS_LESS_THAN_5 and
//    SESSION_KEY_MISMATCH are both loco health faults, so knowing how long
//    the current set has left turns a fault that shows up mid-run into one
//    that can be predicted before it does. The parser therefore reports
//    days remaining, not just the raw times.
// =============================================================================

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "capturedecoder.h"   // FieldRow

namespace KeyBlock {

// Days remaining below which a key set is called "expiring soon".
//
// The captured block runs 2026-05-24 to 2026-09-01 — about 100 days — and
// had 7.5 days left, so a one-week warning would have fired only after 92%
// of the life had gone. Two weeks gives notice that is actually actionable.
// A guess until somebody says what the site's key-rotation lead time is;
// the value is here rather than buried so it is easy to change.
static constexpr double kWarnDays = 14.0;

struct KeySet {
    int        index = -1;
    QByteArray key0;          // 16 bytes
    QByteArray key1;
    // [yy][mm][dd][hh] as printed, and the epoch form when the trailing
    // line supplies it.
    int  startY = -1, startM = -1, startD = -1, startH = -1;
    int  endY   = -1, endM   = -1, endD   = -1, endH   = -1;
    qint64 startEpoch = 0, endEpoch = 0;

    bool hasKeys()  const { return key0.size() == 16 && key1.size() == 16; }
    bool hasTimes() const { return startY >= 0 && endY >= 0; }
    QString startText() const;
    QString endText() const;
};

struct Result {
    QVector<KeySet> sets;
    qint64  currentEpoch = 0;      // CURR, when present
    bool    valid = false;
    QString error;

    // Days from CURR to the end of the set that is currently valid, or a
    // negative number if it has already expired. 0 sets or no CURR gives
    // a null result.
    double daysRemaining(bool *ok) const;
};

// True if this line begins an AUTH_KEYS block.
bool isBlockStart(const QString &line);

// How many lines the block occupies, counting the marker. Used by the
// assembler to know when it has collected enough.
int expectedLineCount();

// Parse an assembled block. `lines` should start with the AUTH_KEYS marker.
Result parse(const QStringList &lines);

// Render as inspector rows.
QVector<FieldRow> describe(const Result &r);

}  // namespace KeyBlock

#endif // KEYBLOCK_H
