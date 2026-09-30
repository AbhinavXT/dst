#include "packetbuilder.h"

#include "settings.h"

#include "crypto/kavachmac.h"
#include "schema/schemadecoder.h"   // full type behind kavachSchema()

namespace {

quint32 last4be(const QByteArray &b)
{
    const int n = b.size();
    return (quint32(quint8(b[n - 4])) << 24) | (quint32(quint8(b[n - 3])) << 16)
         | (quint32(quint8(b[n - 2])) << 8)  |  quint32(quint8(b[n - 1]));
}

}  // namespace

PacketBuilder::PacketBuilder()
{
    // The schema the operator is DECODING with, not always the one built into
    // the binary. Loading an external kavach.xml used to change the log's
    // decoding while every builder carried on encoding against the resource —
    // two different definitions live in one session, with nothing on screen
    // saying so. The round-trip validator reports which file it used for the
    // same reason.
    //
    // Falls back to the built-in copy if the external file will not load, so
    // a broken edit leaves the Packet Maker working rather than empty; the
    // note says which one is in force.
    QString err;
    const QString external = Settings::schemaPath();
    if (!external.isEmpty() && m_enc.load(external, &err)) {
        m_schemaPath = external;
        return;
    }
    if (!external.isEmpty()) {
        m_schemaNote = QStringLiteral("external schema %1 could not be loaded (%2) — "
                                      "using the built-in copy").arg(external, err);
    }
    m_enc.load(QStringLiteral(":/schema/kavach.xml"), &err);
}

PacketBuilder::Result PacketBuilder::build(const QString &captype,
                                           QHash<QString, qint64> header,
                                           const QVector<Schema::SubEntry> &subs,
                                           const QByteArray &sessionKey16) const
{
    Result r;
    if (!m_enc.isLoaded()) { r.error = QStringLiteral("schema not loaded"); return r; }

    const Schema::PacketInfo pi = m_enc.packet(captype);
    if (!pi.ok) { r.error = QStringLiteral("unknown packet '%1'").arg(captype); return r; }
    if (!pi.unsupported.isEmpty()) {
        r.error = QStringLiteral("packet header uses grammar not encodable yet: %1")
                      .arg(pi.unsupported.join(", "));
        return r;
    }
    // ARP/LSRP carry a station_id + message-header envelope only in the
    // capture (send) direction; the packet DLConsole transmits has no in-frame
    // envelope — just [body][CRC], with the 8-byte message header prepended at
    // send. So we build at offset 0 and self-verify at offset 0, even though
    // the schema's body_offset (80) describes the other direction. These
    // packets also store their CRC little-endian.
    const bool bodyOffsetType = (pi.bodyOffsetBits > 0);
    const bool crcLE = bodyOffsetType;   // arp/lsrp: LE; slrp/aap/aep: BE

    const bool anxc  = (pi.crc == QLatin1String("anxc_jamcrc"));
    const int  crcSz = anxc ? 4 : 0;

    // Two ways a packet carries its CBC-MAC, both ending up as the 4 bytes
    // just before the CRC (MAC over everything up to them):
    //   * tail  MAC: SLRP reserves 8 tail bytes [MAC][CRC] after its subpackets.
    //   * field MAC: AAP / LSRP declare MAC_CODE as their last field, which the
    //                encoder emits as a 4-byte placeholder for us to fill.
    bool macAsField = false;
    for (const Schema::FieldInfo &f : pi.header) {
        if (!f.isPad && f.name == QLatin1String("MAC_CODE")) { macAsField = true; }
    }
    const bool macInTail = pi.hasSub && pi.reserveTail >= 8 && !macAsField;
    const int  tailMacSz = macInTail ? 4 : 0;
    const bool hasMac    = macInTail || macAsField;

    // Pass 1: measure the body so PKT_LENGTH can be set, then pass 2 with the
    // real length. PKT_LENGTH is a fixed-width field, so the size is stable.
    QString encErr;
    QByteArray body = m_enc.encodeBody(captype, header, subs, &encErr);
    if (body.isEmpty()) { r.error = encErr; return r; }

    const int total = body.size() + tailMacSz + crcSz;
    r.pktLength = total;
    header.insert(QStringLiteral("PKT_LENGTH"), total);
    body = m_enc.encodeBody(captype, header, subs, &encErr);
    if (body.isEmpty()) { r.error = encErr; return r; }
    if (body.size() + tailMacSz + crcSz != total) {
        r.error = QStringLiteral("length did not converge (%1 vs %2)")
                      .arg(body.size() + tailMacSz + crcSz).arg(total);
        return r;
    }

    QByteArray frame = body;

    // Place the MAC. For a field MAC it overwrites the last 4 body bytes; for a
    // tail MAC it is appended. Either way it covers the bytes before it.
    if (hasMac) {
        r.hasMac = true;
        const int macCovers = macAsField ? (body.size() - 4) : body.size();
        const QByteArray macInput = frame.left(macCovers);
        QByteArray mac;
        if (sessionKey16.size() == 16) {
            mac = KavachMac::cbcMacWire(macInput, sessionKey16);
            r.macKeyed = true;
        } else {
            mac = QByteArray(4, '\0');
            r.notes << QStringLiteral("no session key given — MAC is a zero placeholder");
        }
        if (macAsField) { frame.replace(macCovers, 4, mac); }
        else            { frame += mac; }
        r.macHex = QString::fromLatin1(mac.toHex());
    }

    // CRC over body(+MAC). Big-endian for slrp/aap/aep, little-endian for
    // arp/lsrp, matching verifyCrc.
    if (anxc) {
        const quint32 crc = CaptureDecoder::jamcrc(frame, 0, frame.size());
        if (crcLE) {
            frame.append(char(crc));       frame.append(char(crc >> 8));
            frame.append(char(crc >> 16)); frame.append(char(crc >> 24));
        } else {
            frame.append(char(crc >> 24)); frame.append(char(crc >> 16));
            frame.append(char(crc >> 8));  frame.append(char(crc));
        }
        r.crcHex = QStringLiteral("%1").arg(crc, 8, 16, QChar('0')).toUpper();
    }

    // ---- self-verify ----
    //
    // This used to be `roundTripped = !decoded.isEmpty()`, which asked only
    // whether the frame decoded to SOMETHING. A frame packed in the wrong bit
    // order decodes to something; so does one whose field values were silently
    // truncated to fit. Both passed.
    //
    // The real question is whether reading the frame back gives the values it
    // was built from. parseBody() is encodeBody()'s inverse, so the built body
    // is re-parsed and its header compared field by field against what was
    // asked for. PKT_LENGTH and MAC_CODE are excluded: this builder computes
    // both, so they are outputs, not requests.
    const Schema::Decoder &dec = kavachSchema();
    r.decoded = dec.decode(frame, {}, captype, bodyOffsetType ? 0 : -1);

    r.roundTripped = false;
    if (r.decoded.isEmpty()) {
        r.error = QStringLiteral("built frame did not decode");
        return r;
    }

    const Schema::ParsedPacket back = m_enc.parseBody(captype, body);
    if (!back.ok) {
        r.error = QStringLiteral("built frame did not read back: %1").arg(back.error);
        return r;
    }
    struct Shape { int bits = 64; bool isSigned = false; };
    QHash<QString, Shape> shapeOf;
    for (const Schema::FieldInfo &f : pi.header) {
        if (!f.isPad) { shapeOf.insert(f.name, Shape{ f.bits, f.isSigned }); }
    }

    QStringList drifted;
    for (auto it = header.constBegin(); it != header.constEnd(); ++it) {
        const QString &name = it.key();
        if (name == QLatin1String("PKT_LENGTH") || name == QLatin1String("MAC_CODE")) {
            continue;
        }
        if (!back.header.contains(name)) { continue; }   // not a field of this packet

        // Two rules, and the difference between them is the whole check.
        //
        // A SIGNED field is written from the caller's raw bit pattern and read
        // back as a negative number: DIST_PKT_START goes in as 31368 and comes
        // out as -1400, which are the same fifteen bits. Those are compared
        // masked, or every signed field would be reported as a fault.
        //
        // An UNSIGNED field is compared as written, WITHOUT masking. Masking
        // here would defeat the point: asking for 0x7FFFFFFF in a four-bit
        // field writes 15, and 0x7FFFFFFF masked to four bits is also 15, so
        // a masked comparison would call a truncation a match. That silent
        // truncation is one of the two failures this check exists to catch.
        const Shape sh = shapeOf.value(name, Shape{});
        qint64 asked = it.value();
        qint64 got   = back.header.value(name);
        if (sh.isSigned && sh.bits > 0 && sh.bits < 63) {
            const qint64 mask = (qint64(1) << sh.bits) - 1;
            asked &= mask;
            got   &= mask;
        }
        if (asked != got) {
            drifted << QStringLiteral("%1: asked %2, frame carries %3")
                           .arg(name).arg(it.value()).arg(back.header.value(name));
        }
    }
    if (!drifted.isEmpty()) {
        // Almost always a value too wide for its field. Naming the fields
        // matters: "the frame is not what you asked for" without saying which
        // part sends the operator back through every editor by hand.
        r.error = QStringLiteral("built frame does not read back as requested — %1")
                      .arg(drifted.join(QStringLiteral("; ")));
        return r;
    }
    r.roundTripped = true;

    if (anxc && frame.size() >= 8) {
        const quint32 want = CaptureDecoder::jamcrc(frame, 0, frame.size() - 4);
        const int m = frame.size();
        const quint32 got = crcLE
            ? (quint32(quint8(frame[m-4])) | (quint32(quint8(frame[m-3])) << 8)
               | (quint32(quint8(frame[m-2])) << 16) | (quint32(quint8(frame[m-1])) << 24))
            : last4be(frame);
        r.crcVerified = (want == got);
    } else {
        r.crcVerified = !anxc;   // nothing to check
    }

    if (!r.crcVerified) { r.error = QStringLiteral("CRC self-check failed"); return r; }
    if (!m_schemaNote.isEmpty()) { r.notes << m_schemaNote; }

    r.frame = frame;
    r.ok = true;
    return r;
}
