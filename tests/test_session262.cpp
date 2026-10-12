#include "testutil.h"

#include "dmiclip.h"
#include "dmiclipdialog.h"
#include "dmipanel.h"
#include "gifwriter.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QImageReader>
#include <QLabel>
#include <QRandomGenerator>
#include <QTemporaryDir>

// =============================================================================
//  Session 262 — the DMI clip: an animated GIF of the DMI over a stretch of
//  a log. GifWriter is read back by Qt's own GIF reader (frames, sizes,
//  delays, colours); the clip is made from the real @dmi frames of
//  replay/loco_1_1_27062026_140226.cap (about one a second), and from the
//  same capture with 14:03:00-14:03:19 of @dmi taken out, for a gap.
// =============================================================================

namespace {
qint64 at262(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-06-27T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}

struct ReadBack { int frames = 0; QSize size; QVector<int> delays; QVector<QImage> images; };
ReadBack readGif(const QByteArray &data)
{
    ReadBack r;
    QBuffer buf;
    buf.setData(data);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf, "gif");
    r.frames = reader.imageCount();
    r.size = reader.size();
    for (int i = 0; i < 200; ++i) {
        const QImage img = reader.read();
        if (img.isNull()) break;
        r.images << img;
        r.delays << reader.nextImageDelay();
    }
    return r;
}

// The capture into a dispatcher, @dmi between `cutFrom` and `cutTo` left out.
LogModel *ingest(MessageDispatcher &disp, qint64 cutFrom = 0, qint64 cutTo = 0)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    const qint64 end = at262("14:06:00");
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
        if (ms >= end) break;
        if (l.startsWith("@dmi_") && ms >= cutFrom && ms < cutTo) continue;
        disp.ingestLocal(21, 1, l, ms, QString());
    }
    disp.drainNow();
    return disp.modelForKey(QStringLiteral("21_1"));
}
}  // namespace

TEST_SUITE(session262)
{
    ThemeUtil::apply(Theme::Dark);
    UiStyle::apply();

    // ---- GifWriter, read back by Qt -----------------------------------------------------------
    {
        QImage a(64, 48, QImage::Format_RGB32), b(64, 48, QImage::Format_RGB32);
        a.fill(QColor(0x10, 0x20, 0x30));
        b.fill(QColor(0xE0, 0xC0, 0x20));
        for (int x = 0; x < 64; ++x) a.setPixelColor(x, 10, QColor(0xFF, 0xFF, 0xFF));
        GifWriter gif;
        gif.addFrame(a, 500);
        gif.addFrame(b, 1230);
        gif.addFrame(a, 5);                       // under GIF's floor: 20 ms
        const ReadBack r = readGif(gif.encode());
        CHECK(r.frames == 3 && r.size == QSize(64, 48) && r.images.size() == 3,
              QByteArray("three frames of 64 x 48 read back (") + QByteArray::number(r.images.size()) + ")");
        CHECK(r.delays.size() == 3 && r.delays.at(0) == 500 && r.delays.at(1) == 1230 && r.delays.at(2) == 20,
              "each frame's delay: 500, 1230, and 20 ms for one asked at 5");
        CHECK(r.images.size() == 3 && r.images.at(0).pixelColor(5, 5) == QColor(0x10, 0x20, 0x30)
                  && r.images.at(0).pixelColor(5, 10) == QColor(0xFF, 0xFF, 0xFF)
                  && r.images.at(1).pixelColor(30, 30) == QColor(0xE0, 0xC0, 0x20),
              "flat colours come back exact (they are in the palette)");
        // Enough colours and size that the LZW table fills and restarts.
        QImage noise(320, 240, QImage::Format_RGB32);
        QRandomGenerator rng(262);
        for (int y = 0; y < noise.height(); ++y)
            for (int x = 0; x < noise.width(); ++x) noise.setPixel(x, y, qRgb(rng.bounded(256), rng.bounded(256), rng.bounded(256)));
        GifWriter big;
        big.addFrame(noise, 100);
        const QByteArray bytes = big.encode();
        const ReadBack n = readGif(bytes);
        bool close = n.images.size() == 1;
        int worst = 0;
        if (close) {
            const QImage back = n.images.first().convertToFormat(QImage::Format_RGB32);
            for (int y = 0; y < 240; y += 7)
                for (int x = 0; x < 320; x += 7) {
                    const QColor p = back.pixelColor(x, y), q = noise.pixelColor(x, y);
                    worst = qMax(worst, qAbs(p.red() - q.red()) + qAbs(p.green() - q.green()) + qAbs(p.blue() - q.blue()));
                }
        }
        CHECK(close && worst < 400 && GifWriter().encode().isEmpty(),
              QByteArray("random colours past the 4096-code table read back whole, each pixel near (worst ")
                  + QByteArray::number(worst) + "); no frames, no file");
        CHECK(big.palette().size() == 256 && gif.palette().size() == 3, "the palette: the 256 most used; 3 for 3 colours");
    }

    // ---- the clip, from real @dmi frames ------------------------------------------------------
    MessageDispatcher disp;
    LogModel *model = ingest(disp);
    CHECK(model != nullptr, "fixture: the capture's tab");
    if (!model) return;
    CHECK(DmiClip::locosIn(model) == QStringList{ QStringLiteral("1_1") }, "the tab's DMI loco: 1_1");
    DmiClip::Options o;
    o.locoKey = QStringLiteral("1_1");
    o.fromMs = at262("14:03:00");
    o.toMs = at262("14:03:30");
    o.scalePct = 50;
    const int inWindow = DmiClip::countFrames(model, o);
    CHECK(inWindow >= 25 && inWindow <= 33, QByteArray("about one @dmi a second: ") + QByteArray::number(inWindow) + " in 30 s");
    {
        const DmiClip::Clip c = DmiClip::build(model, o);
        CHECK(c.error.isEmpty() && c.dmiFrames == inWindow && c.gaps == 0 && c.frames.size() == inWindow,
              QByteArray("one GIF frame per @dmi frame, no gaps: ") + c.error.toUtf8());
        CHECK(qAbs(c.clipMs - 30000) <= 1100,
              QByteArray("at 1x the clip plays as long as the window: ") + QByteArray::number(c.clipMs) + " ms");
        CHECK(!c.frames.isEmpty() && c.frames.first().image.width() == 400 && c.frames.first().image.height() > 300,
              "50 %: 400 wide, the panel (300) and the caption under it");
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("clip.gif"));
        QString err;
        CHECK(DmiClip::writeGif(c, path, &err), QByteArray("written: ") + err.toUtf8());
        QFile gf(path);
        gf.open(QIODevice::ReadOnly);
        const ReadBack r = readGif(gf.readAll());
        CHECK(r.frames == c.frames.size() && r.size == c.frames.first().image.size(),
              QByteArray("Qt reads the file back: ") + QByteArray::number(r.frames) + " frames");
        o.speed = 4;
        const DmiClip::Clip fast = DmiClip::build(model, o);
        CHECK(qAbs(fast.clipMs - 7500) <= 400, QByteArray("4x plays a quarter as long: ") + QByteArray::number(fast.clipMs) + " ms");
        o.speed = 1;
        // A panel drawn, not blank: the DMI draws on near-black; many colours.
        const QImage first = c.frames.first().image;
        QSet<QRgb> colours;
        for (int y = 0; y < first.height(); y += 3)
            for (int x = 0; x < first.width(); x += 3) colours.insert(first.pixel(x, y));
        CHECK(colours.size() > 20, QByteArray("the DMI is drawn in the frame (") + QByteArray::number(colours.size()) + " colours)");
    }
    {
        // The opening frame: the window starts between two @dmi frames.
        DmiClip::Options mid = o;
        mid.fromMs = at262("14:03:00") + 500;
        const DmiClip::Clip c = DmiClip::build(model, mid);
        CHECK(!c.frames.isEmpty() && c.frames.first().atMs == mid.fromMs,
              "a window starting between frames opens on the frame on screen then, at the window's start");
        DmiClip::Options none = o;
        none.locoKey = QStringLiteral("9_9");
        CHECK(DmiClip::build(model, none).error.contains(QLatin1String("no @dmi")), "a loco with no @dmi: refused, said why");
        DmiClip::Options tooLong = o;
        tooLong.toMs = o.fromMs + DmiClip::kMaxSpanMs + 1000;
        CHECK(!DmiClip::build(model, tooLong).error.isEmpty(), "longer than 30 minutes: refused");
    }

    // ---- a gap ---------------------------------------------------------------------------------
    {
        MessageDispatcher gapDisp;
        LogModel *gm = ingest(gapDisp, at262("14:03:10"), at262("14:03:20"));
        const DmiClip::Clip c = gm ? DmiClip::build(gm, o) : DmiClip::Clip();
        int gapFrames = 0;
        for (const DmiClip::Frame &f : c.frames) gapFrames += f.gap ? 1 : 0;
        // DMI_CLIP_OUT=<dir>: the gap clip and its frames as PNGs, to look at.
        if (!qEnvironmentVariableIsEmpty("DMI_CLIP_OUT")) {
            const QString out = qEnvironmentVariable("DMI_CLIP_OUT");
            DmiClip::writeGif(c, out + QStringLiteral("/gap.gif"));
            for (int i = 0; i < c.frames.size(); ++i)
                if (c.frames.at(i).gap || i == 0) c.frames.at(i).image.save(out + QStringLiteral("/frame%1.png").arg(i));
        }
        CHECK(c.gaps == 1 && gapFrames == 1,
              QByteArray("a 10 s silence: one gap, one muted frame saying so (") + QByteArray::number(c.gaps) + ")");
        // 30 s of log, of which the 10 s gap plays as 3 s fresh + 1.5 s muted.
        CHECK(c.clipMs < 30000 - 4000 && c.clipMs > 20000,
              QByteArray("the gap is shortened, the rest plays in real time: ") + QByteArray::number(c.clipMs) + " ms");
    }

    // ---- the dialog and the DMI window ---------------------------------------------------------
    {
        DmiClipDialog dlg(model, QStringLiteral("21_1"), at262("14:03:15"), QStringLiteral("1_1"));
        CHECK(dlg.locos() == QStringList{ QStringLiteral("1_1") } && dlg.options().toMs - dlg.options().fromMs == 60000,
              "the dialog: the tab's loco, the moment ±30 s");
        CHECK(dlg.previewLabel()->text().contains(QLatin1String("DMI frame")),
              QByteArray("it says how many frames: ") + dlg.previewLabel()->text().toUtf8());
        dlg.setScale(50);
        dlg.setSpeed(8);
        QTemporaryDir dir;
        CHECK(dlg.exportTo(dir.filePath(QStringLiteral("d.gif"))) && dlg.status()->text().contains(QLatin1String("d.gif")),
              QByteArray("Export GIF writes it: ") + dlg.status()->text().toUtf8());
        DmiWindow win(&disp);
        win.setSelectedSource(QStringLiteral("1_1"));
        QString why;
        DmiClipDialog *fromWin = win.makeClipDialog(&why);
        CHECK(fromWin && fromWin->options().locoKey == QLatin1String("1_1"),
              QByteArray("the DMI window's Export clip: panel A's loco, its tab found: ") + why.toUtf8());
        delete fromWin;
        MessageDispatcher empty;
        DmiWindow none(&empty);
        CHECK(!none.makeClipDialog(&why) && !why.isEmpty(), QByteArray("with no loco it says so: ") + why.toUtf8());
    }
}
