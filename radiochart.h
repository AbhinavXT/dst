#ifndef RADIOCHART_H
#define RADIOCHART_H

// =============================================================================
//  Radio chart (session 345) — the radio conversation over a stretch of a
//  log as one picture: a message-sequence diagram, for a report or a dispute.
//  -----------------------------------------------------------------------------
//  Two lifelines, the loco on the left and the stations on the right, and one
//  arrow per frame in arrival order: the loco's LSRP and ARP going right, each
//  SLRP coming back from its station. The label gives the type, FRAME_NUM (and
//  the time it stands for) and, for an SLRP, its station and age on arrival.
//
//  ✕  a frame the receiver would not process: an SLRP over 4 s old or 2 s
//     early by the loco's clock (as RadioLinks reads the age), or any frame a
//     reject rule fires on (with the loco's identity, as the run report does).
//     The clause is written beside it.
//  ┄  a silence: on one link, a FRAME_NUM step of two cycles or more (the
//     cycle read from the traffic, as RadioLinks does). Over 30 s it is said
//     as no contact.
//
//  Observed, not judged: the chart says what the receiver would do.
//  Exported as PNG or SVG from one list of drawing steps, so both match.
// =============================================================================

#include <QColor>
#include <QImage>
#include <QLineF>
#include <QString>
#include <QStringList>
#include <QVector>

class LogModel;
class QPainter;

namespace RadioChart {

constexpr qint64 kMaxSpanMs   = 10 * 60 * 1000;   // longest window
constexpr int    kMaxMessages = 1200;             // keeps a PNG under 32k px tall

struct Message {
    qint64  ms = 0;            // arrival / log time
    int     row = -1;
    QString type;              // "slrp", "lsrp", "arp"
    bool    fromLoco = true;   // LSRP, ARP
    int     station = -1;      // SLRP: SOURCE_STN_ID
    qint64  frameNum = -1;
    bool    hasAge = false;    // SLRP with the loco's own clock heard within 10 s
    int     ageS = 0;
    QStringList drops;         // why the receiver would not process it; empty = none
};

struct Silence {
    qint64  fromMs = 0, toMs = 0;   // arrival of the frames either side
    int     afterIndex = -1;        // drawn after messages[afterIndex], the last frame before it
    QString link;                   // "SLRP from station 527", "LSRP", "ARP"
    int     seconds = 0;            // the FRAME_NUM step
    int     missing = 0;            // frames the cycle expected in between
    bool    outage = false;         // over 30 s, or backwards: no contact
};

struct Chart {
    QString title;
    qint64  fromMs = 0, toMs = 0;
    QVector<Message> messages;
    QVector<Silence> silences;
    QString error;             // set when nothing could be made
    int dropped() const;
    QString summary() const;   // "SLRP 30 received (12 would not be processed) · …"
};

Chart build(const LogModel *model, const QString &tabName, qint64 fromMs, qint64 toMs);

// The drawing steps. PNG and SVG are both made from these.
struct Step {
    enum Kind { Line, Text, Rect } kind = Line;
    QLineF  line;              // Line; Text: p1 is the anchor
    QRectF  rect;              // Rect
    QColor  color;
    qreal   width = 1;
    bool    dashed = false;
    bool    bold = false;
    int     align = 0;         // Text: -1 right-anchored, 0 left, 1 centred
    QString text;
};
struct Drawing {
    QSize size;
    QVector<Step> steps;
};
Drawing draw(const Chart &c);
void paint(QPainter &p, const Drawing &d);
QImage toImage(const Drawing &d);
QByteArray toSvg(const Drawing &d);

// By suffix: .svg writes SVG, anything else PNG.
bool save(const Chart &c, const QString &path, QString *error = nullptr);

}  // namespace RadioChart

#endif // RADIOCHART_H
