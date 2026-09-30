#include "schema/schemaencoder.h"
// For captypeTokens(): the two halves agree on what a match= attribute means,
// which is the point of it living in one place.
#include "schema/schemadecoder.h"

#include <QFile>

namespace {

// msb-first bit writer: the inverse of Decoder::Cursor::take(). Bit 0 is the
// MSB of byte 0, and put() emits a field's bits most-significant first, so a
// value written here reads back identically through take().
struct BitWriter {
    QByteArray buf;
    int nbits = 0;

    void ensure(int byte) { while (buf.size() <= byte) { buf.append(char(0)); } }

    void put(quint64 v, int n) {
        for (int i = n - 1; i >= 0; --i) {
            const int byte = nbits >> 3;
            ensure(byte);
            if ((v >> i) & 1) {
                buf[byte] = char(uchar(buf[byte]) | uchar(0x80 >> (nbits & 7)));
            }
            ++nbits;
        }
    }

    // Overwrite n bits starting at an absolute bit position (already emitted).
    void poke(int bitPos, quint64 v, int n) {
        for (int i = n - 1; i >= 0; --i) {
            const int byte = bitPos >> 3;
            if (byte >= buf.size()) { break; }
            const uchar mask = uchar(0x80 >> (bitPos & 7));
            if ((v >> i) & 1) { buf[byte] = char(uchar(buf[byte]) | mask); }
            else              { buf[byte] = char(uchar(buf[byte]) & uchar(~mask)); }
            ++bitPos;
        }
    }

    void alignByte() { if (nbits & 7) { put(0, 8 - (nbits & 7)); } }
};

// msb-first bit reader: the exact inverse of BitWriter::put(). Reads past the
// end return 0 and set `over`, so a truncated buffer is reported rather than
// silently producing plausible-looking zeros.
struct BitReader {
    const QByteArray &buf;
    int pos   = 0;      // absolute bit position
    int limit = 0;      // absolute bit position one past the last readable bit
    bool over = false;

    BitReader(const QByteArray &b, int startBit, int limitBit)
        : buf(b), pos(startBit), limit(limitBit) {}

    quint64 take(int n) {
        quint64 v = 0;
        for (int i = 0; i < n; ++i) {
            if (pos >= limit || (pos >> 3) >= buf.size()) { over = true; v <<= 1; ++pos; continue; }
            const uchar byte = uchar(buf[pos >> 3]);
            v = (v << 1) | quint64((byte >> (7 - (pos & 7))) & 1);
            ++pos;
        }
        return v;
    }

    // Two's-complement sign extension for <field signed="true">.
    qint64 takeSigned(int n) {
        const quint64 raw = take(n);
        if (n <= 0 || n >= 64) { return qint64(raw); }
        const quint64 sign = quint64(1) << (n - 1);
        return (raw & sign) ? qint64(raw) - qint64(quint64(1) << n) : qint64(raw);
    }

    void alignByte() { if (pos & 7) { pos += 8 - (pos & 7); } }
};

// Mirror of Decoder::condOk for the subset of operators the SLRP structs use
// (plain comparisons and "name & MASK == VALUE"). Kept local so the encoder
// does not depend on decoder internals.
bool condOk(const QString &when, const QHash<QString, qint64> &vals)
{
    if (when.isEmpty()) { return true; }

    // Conjunctions, e.g. an outer <when> combined with a field's own when.
    if (when.contains(QLatin1String("&&"))) {
        for (const QString &part : when.split(QStringLiteral("&&"))) {
            if (!condOk(part.trimmed(), vals)) { return false; }
        }
        return true;
    }

    const int ampAt = when.indexOf(QLatin1Char('&'));
    if (ampAt >= 0) {
        const QString lhs = when.left(ampAt).trimmed();
        const QString rest = when.mid(ampAt + 1).trimmed();
        const int eqAt = rest.indexOf(QLatin1String("=="));
        const int neAt = rest.indexOf(QLatin1String("!="));
        const int opAt = (eqAt >= 0) ? eqAt : neAt;
        if (opAt >= 0) {
            const qint64 mask = rest.left(opAt).trimmed().toLongLong(nullptr, 0);
            const qint64 want = rest.mid(opAt + 2).trimmed().toLongLong(nullptr, 0);
            const qint64 got  = vals.value(lhs, 0) & mask;
            return (eqAt >= 0) ? (got == want) : (got != want);
        }
    }

    auto rangeAfter = [&](const QString &kw) -> int {
        return when.indexOf(kw);
    };
    const int notInAt = rangeAfter(QLatin1String(" not in "));
    const int inAt    = rangeAfter(QLatin1String(" in "));
    if (notInAt >= 0 || inAt >= 0) {
        const bool neg = (notInAt >= 0);
        const int at = neg ? notInAt : inAt;
        const int kwLen = neg ? 8 : 4;
        const QString a = when.left(at).trimmed();
        const QString rng = when.mid(at + kwLen).trimmed();
        const int dd = rng.indexOf(QLatin1String(".."));
        const qint64 lo = rng.left(dd).toLongLong();
        const qint64 hi = rng.mid(dd + 2).toLongLong();
        const qint64 x = vals.value(a, 0);
        const bool in = (x >= lo && x <= hi);
        return neg ? !in : in;
    }

    static const char *ops[] = { "==", "!=", ">=", "<=", ">", "<" };
    for (const char *op : ops) {
        const int k = when.indexOf(QLatin1String(op));
        if (k < 0) { continue; }
        const QString a = when.left(k).trimmed();
        const qint64  b = when.mid(k + int(qstrlen(op))).trimmed().toLongLong();
        const qint64  x = vals.value(a, 0);
        const QString o = QString::fromLatin1(op);
        if (o == "==") { return x == b; }  if (o == "!=") { return x != b; }
        if (o == ">=") { return x >= b; }  if (o == "<=") { return x <= b; }
        if (o == ">")  { return x >  b; }  return x < b;
    }
    return true;
}

}  // namespace

namespace Schema {

// Walk a struct's DOM children in spec order, emitting fields, pads and
// <repeat> blocks. The exact inverse of Decoder::walkStruct / walkEntry.
static bool emitStruct(BitWriter &bw, const QDomElement &st,
                       const Schema::SubEntry &se, QString *err)
{
    // Count fields are driven by how many rows the operator supplied; set them
    // up front so the <field> that carries the count (emitted before its
    // <repeat>) writes the right number.
    QHash<QString, qint64> vals = se.values;
    for (QDomElement e = st.firstChildElement("repeat"); !e.isNull();
         e = e.nextSiblingElement("repeat")) {
        const QString cattr = e.attribute("count");
        bool isNum = false; cattr.toLongLong(&isNum);
        if (!isNum) { vals.insert(cattr, se.repeats.value(e.attribute("name")).size()); }
    }

    for (QDomElement e = st.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        const QString tag = e.tagName();
        if (tag == "pad")   { bw.put(0, e.attribute("bits").toInt()); continue; }
        if (tag == "align") { bw.alignByte(); continue; }
        if (tag == "field") {
            if (!condOk(e.attribute("when"), vals)) { continue; }
            const int bits = e.attribute("bits").toInt();
            const quint64 m = (bits >= 64)
                ? quint64(vals.value(e.attribute("name"), 0))
                : (quint64(vals.value(e.attribute("name"), 0)) & ((quint64(1) << bits) - 1));
            bw.put(m, bits);
            continue;
        }
        if (tag == "repeat") {
            const QString rn  = e.attribute("name");
            const qint64 cmin = e.attribute("cmin", "0").toLongLong();
            const qint64 cmax = e.attribute("cmax", "63").toLongLong();
            const auto &rows = se.repeats.value(rn);
            if (rows.size() < cmin || rows.size() > cmax) {
                if (err) { *err = QStringLiteral("repeat '%1' has %2 rows, outside %3..%4")
                                      .arg(rn).arg(rows.size()).arg(cmin).arg(cmax); }
                return false;
            }
            for (const QHash<QString, qint64> &row : rows) {
                QHash<QString, qint64> rc = vals;   // entry-local scope, seeded from struct
                for (QDomElement k = e.firstChildElement(); !k.isNull();
                     k = k.nextSiblingElement()) {
                    const QString ktag = k.tagName();
                    if (ktag == "pad") { bw.put(0, k.attribute("bits").toInt()); continue; }
                    if (ktag != "field") { continue; }
                    if (!condOk(k.attribute("when"), rc)) { continue; }
                    const int kb = k.attribute("bits").toInt();
                    const qint64 kv = row.value(k.attribute("name"), 0);
                    const quint64 m = (kb >= 64) ? quint64(kv)
                                                 : (quint64(kv) & ((quint64(1) << kb) - 1));
                    bw.put(m, kb);
                    rc.insert(k.attribute("name"), kv);
                }
            }
            continue;
        }
        // <group>/<flags> inside a struct: not reached for SLRP structs; refuse.
        if (tag == "group" || tag == "flags") {
            if (err) { *err = QStringLiteral("struct uses <%1>, not encodable yet").arg(tag); }
            return false;
        }
    }
    return true;
}


// Read-side mirror of emitStruct(). Same child order, same `when` gating, same
// count-driven repeats — except the counts come off the wire instead of being
// derived from the row vectors, and the row values are collected into the
// SubEntry the Packet Maker edits.
static bool parseStruct(BitReader &br, const QDomElement &st,
                        Schema::SubEntry &se, QString *err)
{
    QHash<QString, qint64> vals;    // struct-scope values, for `when` and counts

    for (QDomElement e = st.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        const QString tag = e.tagName();
        if (tag == "pad")   { br.take(e.attribute("bits").toInt()); continue; }
        if (tag == "align") { br.alignByte(); continue; }
        if (tag == "field") {
            if (!condOk(e.attribute("when"), vals)) { continue; }
            const int bits = e.attribute("bits").toInt();
            const bool sgn = (e.attribute("signed") == QLatin1String("true"));
            const qint64 v = sgn ? br.takeSigned(bits) : qint64(br.take(bits));
            const QString nm = e.attribute("name");
            vals.insert(nm, v);
            se.values.insert(nm, v);
            continue;
        }
        if (tag == "repeat") {
            const QString rn   = e.attribute("name");
            const QString cnt  = e.attribute("count");
            bool isNum = false;
            qint64 n = cnt.toLongLong(&isNum);
            if (!isNum) { n = vals.value(cnt, 0); }
            const qint64 cmin = e.attribute("cmin", "0").toLongLong();
            const qint64 cmax = e.attribute("cmax", "63").toLongLong();
            if (n < cmin || n > cmax) {
                if (err) { *err = QStringLiteral("repeat '%1' count %2 outside %3..%4")
                                      .arg(rn).arg(n).arg(cmin).arg(cmax); }
                return false;
            }
            QVector<QHash<QString, qint64>> rows;
            for (qint64 i = 0; i < n; ++i) {
                QHash<QString, qint64> rc = vals;    // entry-local scope, as in emitStruct
                QHash<QString, qint64> row;
                for (QDomElement k = e.firstChildElement(); !k.isNull();
                     k = k.nextSiblingElement()) {
                    const QString ktag = k.tagName();
                    if (ktag == "pad") { br.take(k.attribute("bits").toInt()); continue; }
                    if (ktag != "field") { continue; }
                    if (!condOk(k.attribute("when"), rc)) { continue; }
                    const int kb = k.attribute("bits").toInt();
                    const bool sgn = (k.attribute("signed") == QLatin1String("true"));
                    const qint64 kv = sgn ? br.takeSigned(kb) : qint64(br.take(kb));
                    const QString knm = k.attribute("name");
                    row.insert(knm, kv);
                    rc.insert(knm, kv);
                }
                rows.push_back(row);
            }
            se.repeats.insert(rn, rows);
            continue;
        }
        if (tag == "group" || tag == "flags") {
            if (err) { *err = QStringLiteral("struct uses <%1>, not parseable yet").arg(tag); }
            return false;
        }
    }
    return true;
}


bool Encoder::load(const QString &xmlPath, QString *err)
{
    m_loaded = false;
    m_structs.clear(); m_enums.clear(); m_packets.clear();

    QFile f(xmlPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) { *err = QStringLiteral("cannot open %1").arg(xmlPath); }
        return false;
    }
    QString parseErr; int line = 0, col = 0;
    if (!m_doc.setContent(&f, &parseErr, &line, &col)) {
        if (err) { *err = QStringLiteral("%1:%2: %3").arg(line).arg(col).arg(parseErr); }
        return false;
    }

    const QDomElement root = m_doc.documentElement();
    for (QDomElement e = root.firstChildElement(); !e.isNull();
         e = e.nextSiblingElement()) {
        const QString t = e.tagName();
        if (t == "packet")      { m_packets.push_back(e); }
        else if (t == "struct") { m_structs.insert(e.attribute("name"), e); }
        else if (t == "enum")   { m_enums.insert(e.attribute("name"), e); }
    }
    m_loaded = !m_packets.isEmpty();
    if (!m_loaded && err) { *err = QStringLiteral("schema has no <packet> definitions"); }
    return m_loaded;
}

QStringList Encoder::packetNames() const
{
    QStringList out;
    for (const QDomElement &p : m_packets) {
        // A packet may answer to several tokens; every one of them is offered,
        // so the Packet Maker lists arprecv alongside arp rather than hiding a
        // buildable packet because it shares an element.
        const QStringList toks = captypeTokens(p.attribute("match"));
        if (toks.isEmpty()) { out << p.attribute("name").toLower(); }
        else                { out += toks; }
    }
    return out;
}

QDomElement Encoder::packetElem(const QString &captype) const
{
    for (const QDomElement &p : m_packets) {
        for (const QString &tok : captypeTokens(p.attribute("match"))) {
            if (tok.compare(captype, Qt::CaseInsensitive) == 0) { return p; }
        }
    }
    return QDomElement();
}

QVector<FieldInfo> Encoder::fieldsOf(const QDomElement &parent,
                                     QStringList *unsupported)
{
    QVector<FieldInfo> out;
    collectFields(parent, QString(), out, unsupported);
    return out;
}

void Encoder::collectFields(const QDomElement &parent, const QString &whenCtx,
                            QVector<FieldInfo> &out, QStringList *unsupported)
{
    auto combine = [](const QString &a, const QString &b) -> QString {
        if (a.isEmpty()) { return b; }
        if (b.isEmpty()) { return a; }
        return a + QStringLiteral(" && ") + b;   // condOk splits on &&
    };

    for (QDomElement e = parent.firstChildElement(); !e.isNull();
         e = e.nextSiblingElement()) {
        const QString t = e.tagName();
        if (t == "field") {
            FieldInfo fi;
            fi.name     = e.attribute("name");
            fi.bits     = e.attribute("bits").toInt();
            fi.isSigned = (e.attribute("signed") == QLatin1String("true"));
            fi.enumName = e.attribute("enum");
            fi.when     = combine(whenCtx, e.attribute("when"));
            out.push_back(fi);
        } else if (t == "flags") {
            // A flags block is N raw bits; the operator supplies the bitmap.
            // order="lsb" only affects how the decoder labels individual bits.
            FieldInfo fi;
            fi.name = e.attribute("name");
            fi.bits = e.attribute("bits").toInt();
            fi.when = whenCtx;
            out.push_back(fi);
        } else if (t == "pad") {
            FieldInfo fi; fi.isPad = true; fi.bits = e.attribute("bits").toInt();
            out.push_back(fi);
        } else if (t == "align") {
            FieldInfo fi; fi.isPad = true; fi.name = QStringLiteral("<align>");
            fi.bits = -1;                       // sentinel: align to next byte
            out.push_back(fi);
        } else if (t == "group") {
            collectFields(e, whenCtx, out, unsupported);          // inline the group's fields
        } else if (t == "when") {
            collectFields(e, combine(whenCtx, e.attribute("test")), out, unsupported);
        } else if (t == "note" || t == "row") {
            // display-only; emits no bits. <row> renders other fields'
            // captured ids through a template — the decoder consumes
            // nothing for it, so neither does the writer.
        } else if (t == "subpackets") {
            break;                              // handled by the caller
        } else if (unsupported) {
            // Anything else emits bits this encoder does not know how to
            // write — `repeat` (DOP, NMSFLT) and `eventstream` (NMSHLTH)
            // today, whatever is added tomorrow. Skipping it silently is the
            // dangerous option: the field walk would simply be short, and
            // every later field would be written at the wrong bit offset
            // while still producing a frame that decodes and CRCs. Name it
            // so the caller can refuse the whole packet.
            if (!unsupported->contains(t)) { *unsupported << t; }
        }
    }
}

PacketInfo Encoder::packet(const QString &captype) const
{
    PacketInfo pi;
    const QDomElement p = packetElem(captype);
    if (p.isNull()) { return pi; }

    pi.ok   = true;
    pi.name = p.attribute("name");
    pi.wire = p.attribute("wire");
    pi.crc  = p.attribute("crc");
    pi.bodyOffsetBits = p.attribute("body_offset").toInt();
    pi.header = fieldsOf(p, &pi.unsupported);

    // The writer is msb-first only. An lsb-first packet (DMI, LINFO, RFID,
    // CCSYS, DLSYS) would be packed in the wrong bit order and the result
    // still looks fine — it decodes back to the values that produced it, and
    // its CRC is valid over its own wrong bytes. The round-trip validator
    // shows what that costs: not one @dmi or @linfo frame in the corpus can
    // be reproduced. Refuse instead, so the failure is visible before
    // anything is transmitted rather than after.
    if (!pi.wire.isEmpty() && pi.wire != QLatin1String("msb-first")) {
        pi.unsupported << QStringLiteral("%1 bit order").arg(pi.wire);
    }

    const QDomElement sub = p.firstChildElement("subpackets");
    if (!sub.isNull()) {
        pi.hasSub      = true;
        pi.subTypeBits = sub.attribute("type_bits").toInt();
        pi.subLenBits  = sub.attribute("len_bits").toInt();
        pi.reserveTail = sub.attribute("reserve_tail", "8").toInt();
        for (QDomElement cs = sub.firstChildElement("case"); !cs.isNull();
             cs = cs.nextSiblingElement("case")) {
            pi.cases.push_back({ cs.attribute("type").toInt(),
                                 cs.attribute("struct") });
        }
    }
    return pi;
}

QVector<FieldInfo> Encoder::structFields(const QString &structName) const
{
    if (!m_structs.contains(structName)) { return {}; }
    return fieldsOf(m_structs.value(structName), nullptr);
}

StructLayout Encoder::structLayout(const QString &structName) const
{
    StructLayout sl;
    if (!m_structs.contains(structName)) { return sl; }
    const QDomElement st = m_structs.value(structName);
    sl.ok = true;

    // First pass: collect repeats and note which fields are their counts.
    for (QDomElement e = st.firstChildElement("repeat"); !e.isNull();
         e = e.nextSiblingElement("repeat")) {
        RepeatInfo ri;
        ri.name       = e.attribute("name");
        ri.countField = e.attribute("count");
        ri.cmin       = e.attribute("cmin", "0").toInt();
        ri.cmax       = e.attribute("cmax", "63").toInt();
        bool isNum = false; ri.countField.toLongLong(&isNum);
        if (!isNum) { sl.countFields << ri.countField; }
        else        { ri.countField.clear(); }              // literal count
        for (QDomElement k = e.firstChildElement(); !k.isNull(); k = k.nextSiblingElement()) {
            if (k.tagName() != "field") { continue; }
            FieldInfo fi;
            fi.name = k.attribute("name"); fi.bits = k.attribute("bits").toInt();
            fi.isSigned = (k.attribute("signed") == "true");
            fi.enumName = k.attribute("enum"); fi.when = k.attribute("when");
            ri.fields.push_back(fi);
        }
        sl.repeats.push_back(ri);
    }

    // Second pass: scalar fields (skip pads, skip count fields).
    for (QDomElement e = st.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        if (e.tagName() != "field") { continue; }
        const QString nm = e.attribute("name");
        if (sl.countFields.contains(nm)) { continue; }
        FieldInfo fi;
        fi.name = nm; fi.bits = e.attribute("bits").toInt();
        fi.isSigned = (e.attribute("signed") == "true");
        fi.enumName = e.attribute("enum"); fi.when = e.attribute("when");
        sl.scalarFields.push_back(fi);
    }
    return sl;
}

QVector<QPair<qint64, QString>> Encoder::enumChoices(const QString &enumName) const
{
    QVector<QPair<qint64, QString>> out;
    if (!m_enums.contains(enumName)) { return out; }
    const QDomElement en = m_enums.value(enumName);
    for (QDomElement m = en.firstChildElement("map"); !m.isNull();
         m = m.nextSiblingElement("map")) {
        const QString vs = m.attribute("v");
        bool ok = false; const qint64 v = vs.toLongLong(&ok);
        if (ok) { out.push_back({ v, m.attribute("label", m.attribute("name")) }); }
    }
    return out;
}

QByteArray Encoder::encodeBody(const QString &captype,
                               const QHash<QString, qint64> &header,
                               const QVector<SubEntry> &subs,
                               QString *err) const
{
    const PacketInfo pi = packet(captype);
    if (!pi.ok) { if (err) { *err = QStringLiteral("no packet for '%1'").arg(captype); } return {}; }
    if (!pi.unsupported.isEmpty()) {
        if (err) { *err = QStringLiteral("packet uses grammar the encoder can't emit yet: %1")
                              .arg(pi.unsupported.join(", ")); }
        return {};
    }

    BitWriter bw;

    // Flat header. `align` sentinel (bits < 0) rounds to the next byte.
    for (const FieldInfo &f : pi.header) {
        if (f.isPad && f.bits < 0) { bw.alignByte(); continue; }
        if (f.isPad) { bw.put(0, f.bits); continue; }
        if (!condOk(f.when, header)) { continue; }
        const quint64 masked = (f.bits >= 64)
            ? quint64(header.value(f.name, 0))
            : (quint64(header.value(f.name, 0)) & ((quint64(1) << f.bits) - 1));
        bw.put(masked, f.bits);
    }

    // Sub-packets: each starts byte-aligned, [type][len][struct...], padded to
    // a whole number of bytes; len is the total sub-packet byte count and is
    // back-patched once the struct body length is known.
    if (pi.hasSub) {
        for (const SubEntry &se : subs) {
            bw.alignByte();
            const int startBit  = bw.nbits;
            const int startByte = startBit / 8;
            bw.put(quint64(se.type), pi.subTypeBits);
            const int lenBitPos = bw.nbits;
            bw.put(0, pi.subLenBits);                    // len placeholder

            QString sname;
            for (const CaseInfo &c : pi.cases) {
                if (c.type == se.type) { sname = c.structName; break; }
            }
            if (sname.isEmpty() || !m_structs.contains(sname)) {
                if (err) { *err = QStringLiteral("sub-packet type %1 has no struct").arg(se.type); }
                return {};
            }
            if (!emitStruct(bw, m_structs.value(sname), se, err)) { return {}; }

            bw.alignByte();
            const int L = (bw.nbits / 8) - startByte;    // whole sub-packet, bytes
            if (L >= (1 << pi.subLenBits)) {
                if (err) { *err = QStringLiteral("sub-packet %1 is %2 B, too big for a %3-bit length")
                                      .arg(sname).arg(L).arg(pi.subLenBits); }
                return {};
            }
            bw.poke(lenBitPos, quint64(L), pi.subLenBits);
        }
    }

    bw.alignByte();
    return bw.buf;
}

ParsedPacket Encoder::parseBody(const QString &captype, const QByteArray &frame) const
{
    ParsedPacket pp;
    pp.captype = captype;

    const PacketInfo pi = packet(captype);
    if (!pi.ok) { pp.error = QStringLiteral("no packet for '%1'").arg(captype); return pp; }
    if (!pi.unsupported.isEmpty()) {
        pp.error = QStringLiteral("packet uses grammar the encoder can't emit yet: %1")
                       .arg(pi.unsupported.join(", "));
        return pp;
    }
    if (frame.isEmpty()) { pp.error = QStringLiteral("buffer is empty"); return pp; }

    BitReader br(frame, 0, frame.size() * 8);

    // Flat header, in spec order, with the same `when` gating the writer uses.
    for (const FieldInfo &f : pi.header) {
        if (f.isPad && f.bits < 0) { br.alignByte(); continue; }
        if (f.isPad)               { br.take(f.bits); continue; }
        if (!condOk(f.when, pp.header)) { continue; }
        pp.header.insert(f.name, f.isSigned ? br.takeSigned(f.bits)
                                            : qint64(br.take(f.bits)));
    }
    if (br.over) {
        pp.error = QStringLiteral("buffer is too short for the %1 header (%2 B given)")
                       .arg(pi.name).arg(frame.size());
        return pp;
    }

    // Sub-packets. Each starts byte-aligned as [type][len][struct...]; `len` is
    // the whole sub-packet in bytes, so the cursor jumps to the next one rather
    // than trusting the struct walk to land exactly on the boundary. That is
    // also what makes a stale struct definition visible instead of silently
    // corrupting everything after it — see `leftover` below.
    if (pi.hasSub) {
        const int tailBytes = pi.reserveTail;
        const int stopByte  = frame.size() - tailBytes;
        pp.tailBytes = qMax(0, qMin(tailBytes, frame.size()));
        if (stopByte <= 0) {
            pp.error = QStringLiteral("buffer holds only the %1-byte reserved tail")
                           .arg(tailBytes);
            return pp;
        }
        while (br.pos / 8 < stopByte) {
            const int startByte = br.pos / 8;
            const int type = int(br.take(pi.subTypeBits));
            const int len  = int(br.take(pi.subLenBits));
            if (len == 0) {
                pp.notes << QStringLiteral("stopped at byte %1: sub-packet length 0 "
                                           "(padding, or the buffer ends here)").arg(startByte);
                break;
            }
            if (startByte + len > stopByte) {
                pp.notes << QStringLiteral("stopped at byte %1: sub-packet type %2 claims "
                                           "%3 B, past the reserved tail")
                                .arg(startByte).arg(type).arg(len);
                break;
            }

            QString sname;
            for (const CaseInfo &c : pi.cases) {
                if (c.type == type) { sname = c.structName; break; }
            }
            if (sname.isEmpty() || !m_structs.contains(sname)) {
                pp.error = QStringLiteral("sub-packet type %1 at byte %2 has no struct in the schema")
                               .arg(type).arg(startByte);
                return pp;
            }

            SubEntry se;
            se.type = type;
            const int subEnd = (startByte + len) * 8;
            BitReader sb(frame, startByte * 8 + pi.subTypeBits + pi.subLenBits, subEnd);
            QString serr;
            if (!parseStruct(sb, m_structs.value(sname), se, &serr)) {
                pp.error = QStringLiteral("%1 at byte %2: %3").arg(sname).arg(startByte).arg(serr);
                return pp;
            }
            if (sb.over) {
                pp.error = QStringLiteral("%1 at byte %2 overruns its declared %3 B")
                               .arg(sname).arg(startByte).arg(len);
                return pp;
            }

            // Anything past byte alignment means the struct and the wire
            // disagree — the symptom that the missing TagLinking TIN produced.
            const int leftover = subEnd - sb.pos;
            if (leftover >= 8) {
                pp.notes << QStringLiteral("%1: %2 unread bits before the next sub-packet "
                                           "— the struct is missing a field")
                                .arg(sname).arg(leftover);
            }

            pp.subs.push_back(se);
            br.pos = subEnd;
        }
        pp.bodyBytes = br.pos / 8;
    } else {
        br.alignByte();
        pp.bodyBytes = br.pos / 8;
        pp.tailBytes = qMax(0, frame.size() - pp.bodyBytes);
    }

    // PKT_LENGTH is rewritten by PacketBuilder, but a mismatch here almost
    // always means the buffer was pasted with a message header still attached
    // or with bytes missing, so say so rather than quietly building a stub.
    if (pp.header.contains(QStringLiteral("PKT_LENGTH"))) {
        const qint64 declared = pp.header.value(QStringLiteral("PKT_LENGTH"));
        if (declared != frame.size()) {
            pp.notes << QStringLiteral("PKT_LENGTH says %1 B but the buffer is %2 B")
                            .arg(declared).arg(frame.size());
        }
    }

    pp.ok = true;
    return pp;
}

QString Encoder::detectCaptype(const QByteArray &frame) const
{
    QString best;
    int bestSubs = -1;
    for (const QString &name : packetNames()) {
        const ParsedPacket pp = parseBody(name, frame);
        if (!pp.ok) { continue; }
        // A declared PKT_LENGTH that matches the buffer is decisive.
        if (pp.header.contains(QStringLiteral("PKT_LENGTH"))
            && pp.header.value(QStringLiteral("PKT_LENGTH")) == frame.size()) {
            return name;
        }
        // Otherwise prefer whichever packet made the most sense of the bytes.
        if (pp.notes.isEmpty() && pp.subs.size() > bestSubs) {
            bestSubs = pp.subs.size();
            best = name;
        }
    }
    return best;
}

}  // namespace Schema
