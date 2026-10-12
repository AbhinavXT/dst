#ifndef GIFWRITER_H
#define GIFWRITER_H

// =============================================================================
//  GifWriter (session 262) — an animated GIF from a list of images.
//  -----------------------------------------------------------------------------
//  For the DMI clip: no ffmpeg, no Qt Multimedia, nothing to install on the
//  bench PC. A GIF plays in PowerPoint, an e-mail, WhatsApp and a browser.
//
//  ONE PALETTE FOR THE CLIP
//    GIF holds 256 colours. The DMI is flat fills, a few dozen colours plus
//    the anti-aliased edges between them, so the palette is the 256 colours
//    the clip uses most (counted at 5 bits a channel over every frame), and
//    every pixel takes its nearest. One palette for every frame, so a colour
//    does not shift from one frame to the next.
//
//  FRAMES
//    Each frame is a whole image with its own delay (1/100 s units on the
//    wire: GIF's). The clip loops (NETSCAPE2.0). Frames after the first may
//    differ in size: each is drawn at the top-left of the first's canvas.
// =============================================================================

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>
#include <QRgb>

class GifWriter
{
public:
    // A frame shown for `delayMs` (rounded to 10 ms; at least 20 ms, below
    // which many players substitute 100 ms).
    void addFrame(const QImage &image, int delayMs);
    int frameCount() const { return m_frames.size(); }

    // The whole file. Empty, with *err, when there are no frames.
    QByteArray encode(QString *err = nullptr) const;
    bool save(const QString &path, QString *err = nullptr) const;

    // Exposed for the tests: the palette encode() would use (at most 256).
    QVector<QRgb> palette() const;

private:
    struct Frame { QImage image; int delayCs = 2; };
    QVector<Frame> m_frames;
};

#endif // GIFWRITER_H
