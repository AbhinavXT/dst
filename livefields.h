#ifndef LIVEFIELDS_H
#define LIVEFIELDS_H
// =============================================================================
//  LiveFields -- "one field of one capture type", for the Live Loco
//  Console's big numbers and for values pinned to the status bar.
//
//  A reference is a label plus one or more SOURCES tried in order, each
//  "<capture token>:<field name>" -- e.g. Speed = "lsrp:TRAIN_SPEED" then
//  "arp:TRAIN_SPEED": the first type that has the field supplies it.
//  Field names are matched as CaptureDecoder::describe() prints them, with
//  the indentation of nested rows ignored.
// =============================================================================
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "capturedecoder.h"

// When a big-number tile turns amber or red (session 82).
//   Fixed     warn / alarm at fixed values, when the value is above them
//             (or below, for things that must not drop: a pressure).
//   Relative  against ANOTHER field of the same loco, e.g. speed against
//             dmi:speed_limit_permissible: amber within `margin` of the
//             limit, red above it. The limit changes as the train runs; a
//             fixed number could not follow it.
struct TileRule {
    enum class Kind { None, Fixed, Relative };
    Kind    kind = Kind::None;
    bool    above = true;         // Fixed: bad when high (true) or low (false)
    double  warn = 0.0, alarm = 0.0;
    QString refSource;            // Relative: "dmi:speed_limit_permissible"
    double  margin = 5.0;         // Relative: amber this close below the limit

    bool isSet() const { return kind != Kind::None; }
    // 0 fine, 1 warning, 2 alarm. Relative with no reference value: 0.
    int  level(double value, bool haveRef = false, double ref = 0.0) const;
    // "fixed:above:70:80", "rel:dmi:speed_limit_permissible:5", or "".
    QString serialise() const;
    static TileRule parse(const QString &text);
    // "amber at 70, red at 80" / "amber within 5 of dmi:speed_limit_permissible, red above it"
    QString describe() const;
};

struct LiveFieldRef {
    QString     label;       // "Speed"
    QStringList sources;     // "lsrp:TRAIN_SPEED", "arp:TRAIN_SPEED"
    TileRule    rule;        // big-number colouring (session 82); pins ignore it

    // "Speed|lsrp:TRAIN_SPEED,arp:TRAIN_SPEED" (for QSettings), with
    // "|<rule>" appended when a rule is set. Older builds read the first two
    // parts and ignore the third.
    QString serialise() const;
    static LiveFieldRef parse(const QString &text);
    bool isValid() const { return !label.isEmpty() && !sources.isEmpty(); }
};

namespace LiveFields {

// The capture token for a type ("lsrp", "nmshlth"), or "#<n>" if none
// round-trips.
QString tokenFor(CapType type);
bool    parseSource(const QString &source, CapType *type, QString *field);
QString sourceFor(CapType type, const QString &field);

// The value of `field` in a describe() result; null if absent.
QString valueIn(const QVector<FieldRow> &rows, const QString &field);

// FRAME_NUM is seconds since midnight + 1: shown as a clock.
bool    isFrameNumber(const QString &field);
QString frameClock(const QString &frameValue);    // "59357" -> "16:29:16"; empty if not a number

// A tile's sources in the order to try them: most recently received first,
// the listed order only breaking ties (session 82 fix). `lastSeenMs` gives
// when a type last arrived, 0 if never. First-listed-wins was wrong: after a
// loco restart it sends ARP again while the LSRP held from before the
// restart still "has" the field, so Speed / Mode / Frame clock froze on the
// old LSRP and never moved to the live ARP.
QStringList byFreshness(const QStringList &sources, const QHash<int, qint64> &lastSeenMs);

// The big-number tiles a new console starts with.
QVector<LiveFieldRef> defaultBigNumbers();

}  // namespace LiveFields

#endif // LIVEFIELDS_H
