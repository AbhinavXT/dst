#ifndef DMICLIP_H
#define DMICLIP_H

// =============================================================================
//  DMI clip (session 262) — the DMI as the loco pilot saw it, over a stretch
//  of a log, as an animated GIF for a report, an e-mail or a presentation.
//  -----------------------------------------------------------------------------
//  THE FRAMES
//    One loco's @dmi frames in the tab, from `fromMs` to `toMs` by receipt
//    time, each drawn as the DMI window draws it (DmiView). The frame that
//    was on the screen at `fromMs` (the latest before it, within the DMI
//    time travel's 10-minute lookback) opens the clip. A frame is held, as
//    the DMI holds it, until the next one: its real-time duration divided
//    by the speed (1x, 2x, 4x, 8x).
//
//  GAPS ARE SAID, NOT HIDDEN
//    When the next frame is more than 3 s away (kDmiStaleMs: where the DMI
//    window draws a frame muted) the frame is held 3 s of real time, then a
//    muted copy says "no new @dmi for 42 s" and is held 1.5 s of clip time,
//    whatever the gap: so a two-minute silence does not become two minutes
//    of a still picture. The caption under every frame gives the moment, so
//    the jump is visible.
//
//  THE CAPTION
//    A strip under the panel: "Loco 1_1 · 12:58:44.120 · 2x" and, on a gap,
//    what is missing. The clip's own record of where it came from.
// =============================================================================

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class LogModel;
class MessageDispatcher;

namespace DmiClip {

constexpr qint64 kMaxSpanMs  = 30 * 60 * 1000;   // longest clip window
constexpr int    kMaxFrames  = 3000;             // ~50 min of 1 Hz frames
constexpr qint64 kGapHoldMs  = 1500;             // clip time a shortened gap is shown

struct Options {
    QString locoKey;           // "1_1"
    qint64  fromMs = 0;
    qint64  toMs = 0;
    int     speed = 1;         // 1, 2, 4, 8
    int     scalePct = 75;     // of the DMI's 800 x 600
};

struct Frame {
    QImage image;
    int    delayMs = 0;
    qint64 atMs = 0;           // the moment the frame shows (receipt time of its @dmi)
    bool   gap = false;        // the muted "no new @dmi" frame
};

struct Clip {
    QVector<Frame> frames;
    int  dmiFrames = 0;        // @dmi frames used (the opening one included)
    int  gaps = 0;             // gaps over 3 s, shortened
    qint64 clipMs = 0;         // how long the clip plays
    QString error;             // set when nothing could be made
};

// The locos with @dmi frames in `model` ("1_1", ...), sorted.
QStringList locosIn(const LogModel *model);
// A tab of `dispatcher` holding @dmi frames of `locoKey` (the DMI window
// knows locos, not tabs). Prefers the tab with the most recent such frame.
QString tabWithDmi(const MessageDispatcher *dispatcher, const QString &locoKey);

// How many @dmi frames of the loco fall in the window (the dialog's preview;
// cheap: no drawing).
int countFrames(const LogModel *model, const Options &o);

// Draw the clip. `progress` (may be null) is called with frames done, of total.
Clip build(const LogModel *model, const Options &o, const std::function<void(int, int)> &progress = {});

// Write it as an animated GIF.
bool writeGif(const Clip &clip, const QString &path, QString *err = nullptr);

}  // namespace DmiClip

#endif // DMICLIP_H
