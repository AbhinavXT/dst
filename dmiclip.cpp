#include "dmiclip.h"

#include "capturedecoder.h"
#include "dmipanel.h"
#include "dmitimetravel.h"
#include "gifwriter.h"
#include "logmodel.h"
#include "messagedispatcher.h"

#include <QDateTime>
#include <QFont>
#include <QPainter>

#include <algorithm>

namespace DmiClip {

namespace {

const QSize kDmiSize(800, 600);

QString clock(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss.zzz")); }

struct Source { qint64 ms; QString text; };

// The loco's @dmi rows in the window, the opening one (latest before
// fromMs, within the lookback) first.
QVector<Source> collect(const LogModel *model, const Options &o)
{
    QVector<Source> out;
    if (!model || o.locoKey.isEmpty() || o.toMs <= o.fromMs) return out;
    const int start = logModelRowAfter(model, o.fromMs - 1);           // first row at or after fromMs
    for (int i = start - 1; i >= 0; --i) {
        const LogEntry *e = model->entryPtrAt(i);
        if (!e) continue;
        if (e->epochMs < o.fromMs - kDmiLookbackMs) break;
        if (dmiKeyOfText(e->text) == o.locoKey) { out.append({ e->epochMs, e->text }); break; }
    }
    for (int i = start; i < model->count(); ++i) {
        const LogEntry *e = model->entryPtrAt(i);
        if (!e) continue;
        if (e->epochMs > o.toMs) break;
        if (dmiKeyOfText(e->text) == o.locoKey) out.append({ e->epochMs, e->text });
        if (out.size() > kMaxFrames) break;
    }
    return out;
}

QImage draw(const QString &text, bool stale, int scalePct)
{
    DmiView view;
    view.resize(kDmiSize);
    view.setState(dmiStateFromCapture(CaptureDecoder::parseLine(text)));
    view.setStale(stale);
    QImage img = view.grab().toImage().convertToFormat(QImage::Format_RGB32);
    if (img.size() != kDmiSize) img = img.scaled(kDmiSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (scalePct != 100)
        img = img.scaled(kDmiSize.width() * scalePct / 100, kDmiSize.height() * scalePct / 100,
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

// The panel with its caption strip under it.
QImage captioned(const QImage &panel, const QString &left, const QString &right, bool warn)
{
    const int strip = qMax(20, panel.height() / 16);
    QImage out(panel.width(), panel.height() + strip, QImage::Format_RGB32);
    out.fill(QColor(0x14, 0x17, 0x1C));
    QPainter p(&out);
    p.drawImage(0, 0, panel);
    QFont f;
    f.setPixelSize(qMax(10, strip * 55 / 100));
    p.setFont(f);
    const QRect r(8, panel.height(), panel.width() - 16, strip);
    // White on near-black (15:1); the gap note in amber (9:1 on the strip).
    p.setPen(QColor(0xF2, 0xF2, 0xF2));
    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, left);
    p.setPen(warn ? QColor(0xFF, 0xC1, 0x4D) : QColor(0xB8, 0xBE, 0xC6));
    p.drawText(r, Qt::AlignRight | Qt::AlignVCenter, right);
    return out;
}

}  // namespace

QStringList locosIn(const LogModel *model)
{
    QStringList out;
    if (!model) return out;
    for (int i = 0; i < model->count(); ++i) {
        const LogEntry *e = model->entryPtrAt(i);
        if (!e) continue;
        const QString k = dmiKeyOfText(e->text);
        if (!k.isEmpty() && !out.contains(k)) out << k;
    }
    out.sort();
    return out;
}

QString tabWithDmi(const MessageDispatcher *dispatcher, const QString &locoKey)
{
    if (!dispatcher || locoKey.isEmpty()) return QString();
    QString best;
    qint64 bestMs = -1;
    for (const QString &tab : dispatcher->knownKeys()) {
        const LogModel *m = dispatcher->modelForKey(tab);
        if (!m) continue;
        for (int i = m->count() - 1; i >= 0; --i) {           // newest first: one hit is enough
            const LogEntry *e = m->entryPtrAt(i);
            if (e && dmiKeyOfText(e->text) == locoKey) {
                if (e->epochMs > bestMs) { bestMs = e->epochMs; best = tab; }
                break;
            }
        }
    }
    return best;
}

int countFrames(const LogModel *model, const Options &o)
{
    return collect(model, o).size();
}

Clip build(const LogModel *model, const Options &opt, const std::function<void(int, int)> &progress)
{
    Clip clip;
    Options o = opt;
    o.speed = qBound(1, o.speed, 64);
    o.scalePct = qBound(25, o.scalePct, 100);
    if (o.toMs <= o.fromMs) { clip.error = QStringLiteral("the end is not after the start"); return clip; }
    if (o.toMs - o.fromMs > kMaxSpanMs) {
        clip.error = QStringLiteral("a clip covers at most %1 minutes").arg(kMaxSpanMs / 60000);
        return clip;
    }
    const QVector<Source> src = collect(model, o);
    if (src.isEmpty()) {
        clip.error = QStringLiteral("no @dmi frames of loco %1 in that window").arg(o.locoKey);
        return clip;
    }
    if (src.size() > kMaxFrames) {
        clip.error = QStringLiteral("more than %1 DMI frames: pick a shorter window").arg(kMaxFrames);
        return clip;
    }
    clip.dmiFrames = src.size();
    const QString speed = o.speed == 1 ? QStringLiteral("real time") : QStringLiteral("%1×").arg(o.speed);
    for (int i = 0; i < src.size(); ++i) {
        if (progress) progress(i, src.size());
        const qint64 shownFrom = qMax(src.at(i).ms, o.fromMs);
        const qint64 until = i + 1 < src.size() ? src.at(i + 1).ms : o.toMs;
        const qint64 held = qMax<qint64>(0, until - shownFrom);
        QString left = QStringLiteral("Loco %1 · %2").arg(o.locoKey, clock(shownFrom));
        if (src.at(i).ms < o.fromMs) left += QStringLiteral(" (frame of %1)").arg(clock(src.at(i).ms).left(8));
        const QImage panel = draw(src.at(i).text, false, o.scalePct);
        Frame f;
        f.atMs = shownFrom;
        f.image = captioned(panel, left, speed, false);
        if (held <= kDmiStaleMs) {
            f.delayMs = int(qMax<qint64>(held / o.speed, 20));
            clip.frames.append(f);
            clip.clipMs += f.delayMs;
            continue;
        }
        // A gap: the frame as long as the DMI would hold it fresh, then the
        // muted frame that says how long nothing came.
        f.delayMs = int(kDmiStaleMs / o.speed);
        clip.frames.append(f);
        clip.clipMs += f.delayMs;
        Frame g;
        g.gap = true;
        g.atMs = shownFrom + kDmiStaleMs;
        const QString what = i + 1 < src.size()
            ? QStringLiteral("no new @dmi for %1 s (shortened)").arg((held + 500) / 1000)
            : QStringLiteral("no new @dmi to %1").arg(clock(o.toMs).left(8));
        g.image = captioned(draw(src.at(i).text, true, o.scalePct),
                            QStringLiteral("Loco %1 · %2").arg(o.locoKey, clock(g.atMs)), what, true);
        g.delayMs = int(kGapHoldMs);
        clip.frames.append(g);
        clip.clipMs += g.delayMs;
        ++clip.gaps;
    }
    if (progress) progress(src.size(), src.size());
    return clip;
}

bool writeGif(const Clip &clip, const QString &path, QString *err)
{
    if (clip.frames.isEmpty()) {
        if (err) *err = clip.error.isEmpty() ? QStringLiteral("no frames") : clip.error;
        return false;
    }
    GifWriter gif;
    for (const Frame &f : clip.frames) gif.addFrame(f.image, f.delayMs);
    return gif.save(path, err);
}

}  // namespace DmiClip
