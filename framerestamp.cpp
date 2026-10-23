#include "framerestamp.h"

#include "capturedecoder.h"
#include "Structures.h"

#include <QStringList>

int FrameRestamp::shifted(int frameNum, qint64 deltaSec)
{
    const qint64 day = 86400;
    return int((((frameNum - 1 + deltaSec) % day) + day) % day) + 1;
}

namespace {
quint32 readBits(const QByteArray &b, int at, int bits)   // msb-first
{
    quint32 v = 0;
    for (int i = 0; i < bits; ++i) {
        const int bit = at + i;
        v = (v << 1) | ((quint8(b.at(bit / 8)) >> (7 - bit % 8)) & 1u);
    }
    return v;
}

void writeBits(QByteArray &b, int at, int bits, quint32 v)
{
    char *d = b.data();
    for (int i = 0; i < bits; ++i) {
        const int bit = at + i;
        const char mask = char(0x80 >> (bit % 8));
        d[bit / 8] = ((v >> (bits - 1 - i)) & 1u) ? char(d[bit / 8] | mask) : char(d[bit / 8] & ~mask);
    }
}
}  // namespace

QString FrameRestamp::line(const QString &text, qint64 deltaSec, int *frames)
{
    if (frames) *frames = 0;
    CaptureLine cap = CaptureDecoder::parseLine(text);
    if (!cap.valid) return text;
    QStringList tok = text.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (cap.rtc.isValid()) tok[1] = cap.rtc.addSecs(deltaSec).toString(Qt::ISODate);

    // The four Annexure-C packets, msb-first, each with a JAMCRC remakeCrc
    // knows. Their FRAME_NUM rows give the bits, relative to the frame.
    const bool restampable = cap.type == CapType::ARP || cap.type == CapType::ArpRecv || cap.type == CapType::LSRP
                             || cap.type == CapType::SLRP || cap.type == CapType::AAP;
    if (restampable && !cap.bytes.isEmpty()) {
        QByteArray b = cap.bytes;
        int n = 0;
        for (const FieldRow &r : CaptureDecoder::describe(cap)) {
            if (r.field.trimmed() != QLatin1String("FRAME_NUM") || !r.hasSpan() || r.byteEnd() >= b.size()) continue;
            const int fn = int(readBits(b, r.bitOffset, r.bitLength));
            if (fn < 1 || fn > 86400) continue;   // not a time: leave it as it was
            writeBits(b, r.bitOffset, r.bitLength, quint32(shifted(fn, deltaSec)));
            ++n;
        }
        if (n > 0 && CaptureDecoder::remakeCrc(cap.type, b)) {
            if (frames) *frames = n;
            tok = tok.mid(0, 3);
            tok << QString::fromLatin1(b.toHex(' ').toUpper());
        }
    }
    return tok.join(QLatin1Char(' '));
}

QByteArray FrameRestamp::wire(const QByteArray &wire, qint64 deltaSec)
{
    constexpr int kHdr = int(sizeof(STRUCT_MESSAGE_HEADER));
    if (wire.size() <= kHdr) return wire;
    QByteArray body = wire.mid(kHdr);
    int pad = 0;   // the backend pads fixed buffers with NULs: keep them after the line
    while (pad < body.size() && body.at(body.size() - 1 - pad) == '\0') ++pad;
    const QString text = QString::fromUtf8(body.constData(), body.size() - pad);
    if (!text.startsWith(QLatin1Char('@'))) return wire;
    QByteArray out = wire.left(kHdr) + line(text, deltaSec).toUtf8() + QByteArray(pad, '\0');
    const int len = out.size() - kHdr;
    out[3] = char(len & 0xFF);          // message_len, little-endian (Structures.h)
    out[4] = char((len >> 8) & 0xFF);
    return out;
}
