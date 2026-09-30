#include "framenumberwatch.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "schema/schemaencoder.h"

namespace {

// The packets that carry FRAME_NUM and arrive constantly. Checked before any
// parsing so the ingest path pays a string compare for everything else.
bool carriesFrameNum(const QString &captype)
{
    // arprecv carries FRAME_NUM in the same place, but it is ANOTHER loco's
    // counter, not this one's. Watching it here would compare two unrelated
    // sequences and report a gap every time either moved.
    //
    // slrp carries the STATIONARY end's counter. It is admitted, and then
    // stored separately — see observeLine.
    return captype == QLatin1String("arp") || captype == QLatin1String("lsrp")
        || captype == QLatin1String("slrp");
}

// The encoder used to look up field offsets. One instance, loaded once; it is
// only ever read here.
const Schema::Encoder &encoder()
{
    static Schema::Encoder enc;
    static bool loaded = false;
    if (!loaded) {
        QString err;
        enc.load(QStringLiteral(":/schema/kavach.xml"), &err);
        loaded = true;
    }
    return enc;
}

}  // namespace


FrameNumberWatch::Field FrameNumberWatch::fieldFor(const QString &captype)
{
    const auto cached = m_fields.constFind(captype);
    if (cached != m_fields.constEnd()) { return cached.value(); }

    Field f;
    const Schema::PacketInfo pi = encoder().packet(captype);
    if (pi.ok) {
        // Walk the header adding widths until FRAME_NUM, then add the
        // in-capture envelope the body sits behind. Computed from the schema
        // rather than written down, so a schema edit that moves the field
        // moves this too.
        int bit = pi.bodyOffsetBits > 0 ? pi.bodyOffsetBits : 0;
        for (const Schema::FieldInfo &fi : pi.header) {
            if (fi.name == QLatin1String("FRAME_NUM")) {
                f.bitOffset = bit;
                f.bits      = fi.bits;
                break;
            }
            bit += fi.bits;
        }
    }
    m_fields.insert(captype, f);      // cached even when absent
    return f;
}

void FrameNumberWatch::observe(const LogEntryPtr &entry)
{
    if (entry.isNull()) { return; }
    // Cheap gate on the raw text: the common case — a frame that is neither
    // arp nor lsrp — costs two prefix compares and no parsing at all. This
    // runs on every frame that arrives.
    const QString &t = entry->text;
    if (!(t.startsWith(QLatin1String("@arp"),  Qt::CaseInsensitive) ||
          t.startsWith(QLatin1String("@lsrp"), Qt::CaseInsensitive) ||
          t.startsWith(QLatin1String("@slrp"), Qt::CaseInsensitive))) {
        return;
    }
    observeLine(CaptureDecoder::parseLine(t));
}

void FrameNumberWatch::observeLine(const CaptureLine &c)
{
    if (!c.valid || !carriesFrameNum(c.typeToken)) { return; }

    const Field f = fieldFor(c.typeToken);
    if (f.bits <= 0 || f.bitOffset < 0) { return; }
    if ((f.bitOffset + f.bits) > c.bytes.size() * 8) { return; }   // short frame

    // Plain msb-first bit read. Both packets are msb-first; a type that was
    // not would have been rejected above.
    const uchar *p = reinterpret_cast<const uchar *>(c.bytes.constData());
    qint64 v = 0;
    for (int i = 0; i < f.bits; ++i) {
        const int bit = f.bitOffset + i;
        v = (v << 1) | ((p[bit >> 3] >> (7 - (bit & 7))) & 1);
    }

    Seen s;
    s.value   = v;
    s.atMs    = c.rtc.isValid() ? c.rtc.toMSecsSinceEpoch()
                                : QDateTime::currentMSecsSinceEpoch();
    s.captype = c.typeToken;
    s.locoId  = c.locoId;

    if (c.typeToken == QLatin1String("slrp")) {
        // The station's clock. It does not go into m_latest or m_byLoco: the
        // Packet Maker builds loco-side frames and must keep seeding them from
        // the loco's counter, and latestFor(locoId) means "what that loco is
        // counting", which an SLRP addressed TO it does not answer.
        m_station = s;
        emit observed(v, c.locoId);
        return;
    }

    m_byLoco.insert(c.locoId, s);
    // Latest wins on arrival order, not on value: FRAME_NUM wraps, and
    // "biggest so far" would stick at the top of the range for the rest of
    // the session the first time it did.
    m_latest = s;

    emit observed(v, c.locoId);
}

qint64 FrameNumberWatch::ageMs() const
{
    if (!m_latest.valid()) { return -1; }
    return QDateTime::currentMSecsSinceEpoch() - m_latest.atMs;
}

void FrameNumberWatch::clear()
{
    m_byLoco.clear();
    m_latest  = Seen();
    m_station = Seen();
}
