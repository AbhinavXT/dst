#include "gifwriter.h"

#include <QSaveFile>

#include <algorithm>
#include <climits>
#include <vector>

namespace {

int bucketOf(QRgb c) { return ((qRed(c) >> 3) << 10) | ((qGreen(c) >> 3) << 5) | (qBlue(c) >> 3); }

void put16(QByteArray &out, int v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
}

// GIF's LZW: variable-width codes, least significant bit first, in
// sub-blocks of up to 255 bytes. Min code size 8 (a 256-colour palette).
class LzwEncoder
{
public:
    explicit LzwEncoder(QByteArray &out) : m_out(out), m_code(kTable * 256, 0), m_stamp(kTable * 256, 0) {}

    void encode(const std::vector<uchar> &px)
    {
        m_out.append(char(kMin));
        reset();
        putCode(kClear);
        if (px.empty()) { putCode(kEnd); flush(); m_out.append(char(0)); return; }
        int prefix = px[0];
        for (size_t i = 1; i < px.size(); ++i) {
            const int k = px[i];
            const int slot = prefix * 256 + k;
            if (m_stamp[slot] == m_gen) { prefix = m_code[slot]; continue; }
            putCode(prefix);
            if (m_next < kTable) {
                m_stamp[slot] = m_gen;
                m_code[slot] = m_next++;
                if (m_next > (1 << m_width) && m_width < 12) ++m_width;
            } else {
                putCode(kClear);
                reset();
            }
            prefix = k;
        }
        putCode(prefix);
        putCode(kEnd);
        flush();
        m_out.append(char(0));               // block terminator
    }

private:
    static constexpr int kMin = 8, kClear = 256, kEnd = 257, kTable = 4096;

    void reset()
    {
        ++m_gen;
        m_next = kEnd + 1;
        m_width = kMin + 1;
    }
    void putCode(int code)
    {
        m_acc |= quint32(code) << m_bits;
        m_bits += m_width;
        while (m_bits >= 8) {
            m_block.append(char(m_acc & 0xFF));
            m_acc >>= 8;
            m_bits -= 8;
            if (m_block.size() == 255) writeBlock();
        }
    }
    void flush()
    {
        if (m_bits > 0) m_block.append(char(m_acc & 0xFF));
        m_acc = 0;
        m_bits = 0;
        if (!m_block.isEmpty()) writeBlock();
    }
    void writeBlock()
    {
        m_out.append(char(m_block.size()));
        m_out.append(m_block);
        m_block.clear();
    }

    QByteArray &m_out;
    QByteArray m_block;
    std::vector<int> m_code;
    std::vector<int> m_stamp;     // m_code[slot] is live when m_stamp[slot] == m_gen
    int m_gen = 0;
    int m_next = 0;
    int m_width = 9;
    quint32 m_acc = 0;
    int m_bits = 0;
};

}  // namespace

void GifWriter::addFrame(const QImage &image, int delayMs)
{
    if (image.isNull()) return;
    Frame f;
    f.image = image.convertToFormat(QImage::Format_RGB32);
    f.delayCs = qBound(2, (delayMs + 5) / 10, 65535);
    m_frames.append(f);
}

QVector<QRgb> GifWriter::palette() const
{
    // How often each 5-bit-a-channel colour occurs, and the true colours'
    // sum, so a palette entry is the average of what fell in its bucket.
    std::vector<qint64> count(32768, 0), r(32768, 0), g(32768, 0), b(32768, 0);
    for (const Frame &f : m_frames) {
        for (int y = 0; y < f.image.height(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(f.image.constScanLine(y));
            for (int x = 0; x < f.image.width(); ++x) {
                const int k = bucketOf(line[x]);
                ++count[k];
                r[k] += qRed(line[x]);
                g[k] += qGreen(line[x]);
                b[k] += qBlue(line[x]);
            }
        }
    }
    std::vector<int> used;
    for (int k = 0; k < 32768; ++k)
        if (count[k]) used.push_back(k);
    std::stable_sort(used.begin(), used.end(), [&](int a, int c) { return count[a] > count[c]; });
    if (used.size() > 256) used.resize(256);
    QVector<QRgb> pal;
    for (int k : used) pal.append(qRgb(int(r[k] / count[k]), int(g[k] / count[k]), int(b[k] / count[k])));
    return pal;
}

QByteArray GifWriter::encode(QString *err) const
{
    if (m_frames.isEmpty()) {
        if (err) *err = QStringLiteral("no frames to write");
        return {};
    }
    QVector<QRgb> pal = palette();
    while (pal.size() < 256) pal.append(qRgb(0, 0, 0));

    // Each bucket's nearest palette entry, worked out once.
    std::vector<int> lut(32768, -1);
    auto nearest = [&](QRgb c) {
        const int k = bucketOf(c);
        if (lut[k] >= 0) return lut[k];
        const int cr = (k >> 10) * 8 + 4, cg = ((k >> 5) & 31) * 8 + 4, cb = (k & 31) * 8 + 4;
        int best = 0;
        long bestD = LONG_MAX;
        for (int i = 0; i < pal.size(); ++i) {
            const long dr = qRed(pal[i]) - cr, dg = qGreen(pal[i]) - cg, db = qBlue(pal[i]) - cb;
            const long d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;   // the eye: green most, then blue
            if (d < bestD) { bestD = d; best = i; }
        }
        lut[k] = best;
        return best;
    };

    const QSize canvas = m_frames.first().image.size();
    QByteArray out("GIF89a");
    put16(out, canvas.width());
    put16(out, canvas.height());
    out.append(char(0xF7));      // global table, 8 bits of colour resolution, 256 entries
    out.append(char(0));         // background index
    out.append(char(0));         // aspect
    for (QRgb c : pal) {
        out.append(char(qRed(c)));
        out.append(char(qGreen(c)));
        out.append(char(qBlue(c)));
    }
    // Loop forever.
    out.append("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 19);

    for (const Frame &f : m_frames) {
        const int w = qMin(f.image.width(), canvas.width());
        const int h = qMin(f.image.height(), canvas.height());
        // Graphic control: leave the frame in place, this frame's delay.
        out.append("\x21\xF9\x04", 3);
        out.append(char(0x04));
        put16(out, f.delayCs);
        out.append(char(0));
        out.append(char(0));
        // Image descriptor, at the top-left, no local table.
        out.append(char(0x2C));
        put16(out, 0);
        put16(out, 0);
        put16(out, w);
        put16(out, h);
        out.append(char(0));
        std::vector<uchar> px;
        px.reserve(size_t(w) * size_t(h));
        for (int y = 0; y < h; ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(f.image.constScanLine(y));
            for (int x = 0; x < w; ++x) px.push_back(uchar(nearest(line[x])));
        }
        LzwEncoder(out).encode(px);
    }
    out.append(char(0x3B));      // trailer
    return out;
}

bool GifWriter::save(const QString &path, QString *err) const
{
    const QByteArray data = encode(err);
    if (data.isEmpty()) return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (err) *err = f.errorString();
        return false;
    }
    return true;
}
