// =====================================================================
//  schemadecoder.cpp -- see header. MIRRORS schema/engine.py exactly.
//  engine.py + validate_slrp.py are the gate (validated row-for-row vs the
//  kschema oracle over 2278 real SLRP frames). Keep this in lockstep.
// =====================================================================
#include "schemadecoder.h"

#include <QFile>
#include <QSet>
#include <QStringList>
#include <functional>
#include <cstring>

namespace Schema {

// ---- bit cursor ------------------------------------------------------
quint32 Decoder::Cursor::take(int n)
{
    quint32 v = 0;
    if (n <= 0) { return 0; }

    // NOTE: n > 32 is legitimate and must keep working. take() doubles as
    // the cursor-advance primitive — <pad bits="71"> appears in kavach.xml
    // and discards the return value. So we advance for all n and only
    // constrain the value accumulation below.
    for (int i = 0; i < n; ++i) {
        // Three separate ways to be out of bounds, and they are NOT the
        // same check. `pos >= limit` is the ordinary past-end-of-body case.
        // `pos < 0` and `(pos>>3) >= nbytes` are the ones that matter for
        // safety: every input that feeds `pos` and `limit` — body_offset,
        // trailer_bytes, a crc's at=, subpacket lengths — comes from
        // toInt() on an XML attribute with no validation, so a negative or
        // oversized value used to index straight off either end of the
        // buffer. Release builds compile out Q_ASSERT, so that read was
        // silent.
        if (pos < 0 || pos >= limit || (pos >> 3) >= nbytes) {
            ++pos;
            continue;                                      // zero-fill
        }
        const int byte = pos >> 3, off = pos & 7;
        const int bit  = msb ? ((b[byte] >> (7 - off)) & 1)
                             : ((b[byte] >> off) & 1);

        // The msb branch shifts left by one each iteration, which is always
        // defined and naturally keeps the last 32 bits. The lsb branch
        // shifts by i, and `quint32(bit) << i` is undefined behaviour once
        // i >= 32 — reachable today via the 71-bit pad. Bits past 32 can't
        // be represented in the return type anyway, so drop them explicitly
        // rather than leaving it to the compiler.
        if (msb)          { v = (v << 1) | bit; }
        else if (i < 32)  { v |= quint32(bit) << i; }
        ++pos;
    }
    return v;
}

static qint64 toSigned(quint32 v, int n)
{
    if (n < 32 && (v & (1u << (n - 1)))) { return qint64(v) - (qint64(1) << n); }
    return qint64(v);
}

// Float/double counterparts of composite() below, referenced the same way
// (format="name") from a field whose type= is float or double. A separate
// function because the integer formatters index and divide their argument and
// would silently misbehave on a real. MIRRORS engine.FLOAT_FORMATTERS.
static QString compositeFloat(const QString &name, double v)
{
    if (name == QLatin1String("mps")) {              // m/s, plus the km/h an
        return QStringLiteral("%1 m/s  (%2 km/h)")   // operator actually reads
            .arg(v, 0, 'f', 4).arg(v * 3.6, 0, 'f', 1);
    }
    if (name == QLatin1String("curveA")) {
        // UBA braking curves are stored as x = A*v^2 + C, so A is not itself a
        // deceleration: a = -1/(2A). Show both — A is what is on the wire, a is
        // what the curve means. A == 0 is an empty array slot, not a zero decel.
        if (v == 0.0) { return QString::number(v, 'f', 6); }
        return QStringLiteral("%1  (decel %2 m/s\u00B2)")
            .arg(v, 0, 'f', 6).arg(-1.0 / (2.0 * v), 0, 'f', 3);
    }
    return QString::number(v, 'g');
}

// mirrors engine._fmt_float: format='name' -> float formatter owns the whole
// token (units included); else prec='N' -> fixed N decimals, else %g, + unit
static QString fmtFloatVal(const QDomElement &fld, double v)
{
    if (fld.hasAttribute("format")) {
        return compositeFloat(fld.attribute("format"), v);
    }
    const QString s = fld.hasAttribute("prec")
        ? QString::number(v, 'f', fld.attribute("prec").toInt())
        : QString::number(v, 'g');
    return fld.hasAttribute("unit") ? (s + QChar(' ') + fld.attribute("unit")) : s;
}

static QString hexRange(const uchar *b, int from, int len)
{
    QString s;
    for (int i = 0; i < len; ++i) {
        if (i) s += ' ';
        s += QStringLiteral("%1").arg(b[from + i], 2, 16, QChar('0')).toUpper();
    }
    return s;
}

// ---- composite formatters (the one escape hatch; mirrors engine.FORMATTERS)
static QString sigDir(qint64 v)
{
    switch (v) {
        case 0:  return "UP";       case 1:  return "DN";       case 2:  return "UP FAST";
        case 3:  return "DN FAST";  case 8:  return "UP SLOW";  case 9:  return "DN SLOW";
        case 10: return "UP MAIN";  case 11: return "DN MAIN";  case 12: return "UP SUB";
        case 13: return "DN SUB";   case 14: return "UP BI-DIR";case 15: return "DN BI-DIR";
        default: return "undefined";
    }
}
static QString sigKind(qint64 v)
{
    switch (v) {
        case 0x10: return "Dist";            case 0x11: return "Inr-Dist";
        case 0x12: return "Gate-Dist";       case 0x13: return "Gate-Inr-Dist";
        case 0x14: return "IB-Dist";         case 0x15: return "IB-Inr-Dist";
        case 0x16: return "Auto";            case 0x17: return "Semi-Auto Lit";
        case 0x18: return "Home";            case 0x19: return "Home L-X";
        case 0x1A: return "R-Home";          case 0x1B: return "R-Home L-X";
        case 0x1C: return "M/L-Str L-X";     case 0x1D: return "L/L-Str L-X";
        case 0x1E: return "Int-Str";         case 0x01: return "Adv-Str";
        case 0x02: return "IB-Stop";         case 0x03: return "Gate-Stop";
        case 0x04: return "Calling-On";      case 0x05: return "Adv-Str-cum-Gate";
        case 0x06: return "Gate-cum-Dist";   case 0x07: return "Adv-Str-cum-Dist";
        case 0x23: return "Auto-Gate";       case 0x24: return "Semi-Auto";
        case 0x25: return "Adv-Str-cum-G-ID";case 0x26: return "Gate-cum-ID";
        case 0x27: return "Gate-ID-cum-Dist";case 0x28: return "IB-cum-Gate-Dist";
        case 0x29: return "IB-cum-Gate-ID";  case 0x2A: return "IB-cum-Dist";
        case 0x2B: return "Adv-Str-cum-IB-D";case 0x2C: return "Str-cum-IB-Dist";
        case 0x2D: return "Stop Board";      case 0x2E: return "Gate-cum-IB-Dist";
        case 0x2F: return "Gate-cum-IB-ID";  case 0x30: return "Adv-Str-cum-G-D";
        default: return "undefined";
    }
}
// ---- typed field read (mirrors engine._read) -------------------------
int Decoder::fieldBits(const QDomElement &fld)
{
    const QString t = fld.attribute("type");
    if (t == QLatin1String("float"))  { return 32; }
    if (t == QLatin1String("double")) { return 64; }
    if (t == QLatin1String("char"))   { return 8 * fld.attribute("count").toInt(); }
    return fld.attribute("bits").toInt();
}

Decoder::Kind Decoder::readTyped(const QDomElement &fld, Cursor &c,
                                 qint64 *iv, double *dv, QString *sv)
{
    const QString t = fld.attribute("type");
    if (t == QLatin1String("float")) {            // 4-byte IEEE-754, le
        const quint32 raw = c.take(32);
        float f; std::memcpy(&f, &raw, sizeof(f));
        *dv = double(f);
        return Kind::Real;
    }
    if (t == QLatin1String("double")) {           // 8-byte IEEE-754, le
        // take() returns at most 32 bits, so read the two halves in wire
        // order: lsb-first means the LOW word arrives first.
        const quint32 lo = c.take(32);
        const quint32 hi = c.take(32);
        const quint64 bits = (quint64(hi) << 32) | quint64(lo);
        double d; std::memcpy(&d, &bits, sizeof(d));
        *dv = d;
        return Kind::Real;
    }
    if (t == QLatin1String("char")) {             // fixed ascii array, NUL-trimmed
        const int cn = fld.attribute("count").toInt();
        QByteArray ba; ba.reserve(cn);
        for (int i = 0; i < cn; ++i) { ba.append(char(c.take(8) & 0xFF)); }
        const int z = ba.indexOf('\0');
        if (z >= 0) { ba.truncate(z); }
        *sv = QString::fromLatin1(ba);
        return Kind::Text;
    }
    const int n = fld.attribute("bits").toInt();
    const quint32 raw = c.take(n);
    *iv = (fld.attribute("signed") == "true") ? toSigned(raw, n) : qint64(raw);
    return Kind::Int;
}

QString Decoder::composite(const QString &name, qint64 v)
{
    if (name == "sigInfo") {
        const qint64 line = v & 0x1F, dr = (v >> 5) & 0xF, kind = (v >> 9) & 0x3F,
                     ovr = (v >> 15) & 1, stop = (v >> 16) & 1;
        return QStringLiteral("0x%1  [type=%2  dir=%3  line=%4  override=%5  stop_sig=%6]")
            .arg(v, 0, 16).arg(sigKind(kind)).arg(sigDir(dr))
            .arg(line == 0 ? QStringLiteral("n/a") : QString::number(line))
            .arg(ovr ? QStringLiteral("running") : QStringLiteral("standstill"))
            .arg(stop ? QStringLiteral("Y") : QStringLiteral("N"));
    }
    if (name == "sigAspect") {
        switch (v) {
            case 0:  return "0 (Unidentified)";
            case 1:  return "1 (Red)";
            case 2:  return "2 (Yellow, no route indication)";
            case 3:  return "3 (Yellow + Pos1 junction route, left)";
            case 4:  return "4 (Yellow + Pos2 junction route, left)";
            case 5:  return "5 (Yellow + Pos3 junction route, left)";
            case 6:  return "6 (Yellow + Pos4 junction route, right)";
            case 7:  return "7 (Yellow + Pos5 junction route, right)";
            case 8:  return "8 (Yellow + Pos6 junction route, right)";
            case 10: return "10 (Double Yellow)";
            case 11: return "11 (Green)";
            case 12: return "12 (Double Yellow + Pos1 junction route, left)";
            case 13: return "13 (Double Yellow + Pos4 junction route, right)";
            case 14: return "14 (AG Marker OFF)";
            case 15: return "15 (Red with Calling-on at OFF)";
            case 24: return "24 (Stop Board / Buffer Stop)";
            default: break;
        }
        if (v >= 32 && v <= 63)
            return QStringLiteral("%1 (Yellow + Stencil route %2)").arg(v).arg(v - 31);
        return QStringLiteral("%1 (Spare)").arg(v);
    }
    if (name == "absKm")                              // DMI abs_loco_loc
        return QStringLiteral("%1 m (%2 km)").arg(v).arg(v / 1000.0, 0, 'f', 3);
    if (name == "decel")                              // DMI deceleration_constant
        return QStringLiteral("DC %1.%2").arg(v / 100).arg(v % 100, 2, 10, QChar('0'));
    return QString::number(v);
}

// computed-row template: "{id}" -> value, "{id:0Nd}" -> zero-padded width-N int.
// Mirrors engine._row_tmpl; substitutes stored ids, consumes no bits.
QString Decoder::rowTmpl(const QString &tmpl, const Ctx &ctx)
{
    QString out;
    int i = 0;
    while (i < tmpl.size()) {
        const QChar ch = tmpl.at(i);
        if (ch != QLatin1Char('{')) { out += ch; ++i; continue; }
        const int close = tmpl.indexOf(QLatin1Char('}'), i);
        if (close < 0) { out += ch; ++i; continue; }
        const QString tok = tmpl.mid(i + 1, close - i - 1);
        const int colon = tok.indexOf(QLatin1Char(':'));
        const QString key  = colon < 0 ? tok : tok.left(colon);
        const QString spec = colon < 0 ? QString() : tok.mid(colon + 1);
        const qint64 v = ctx.vals.value(key, 0);
        if (!spec.isEmpty() && spec.startsWith(QLatin1Char('0')) && spec.endsWith(QLatin1Char('d'))) {
            const int width = spec.mid(1, spec.size() - 2).toInt();   // "02d" -> 2
            out += QStringLiteral("%1").arg(v, width, 10, QChar('0'));
        } else {
            out += QString::number(v);
        }
        i = close + 1;
    }
    return out;
}

// ---- load + index ----------------------------------------------------
QStringList Decoder::packetNames() const
{
    QStringList out;
    out.reserve(m_packets.size());
    for (const QDomElement &p : m_packets) out << p.attribute("name");
    return out;
}

QStringList Decoder::allFieldNames() const
{
    QSet<QString> seen;

    // Walks <field> elements at any depth, so fields inside <repeat> and
    // <group> are found as well as the top-level ones — a repeated entry's
    // fields are exactly the ones worth watching.
    std::function<void(const QDomElement &)> walk = [&](const QDomElement &e) {
        for (QDomElement c = e.firstChildElement(); !c.isNull();
             c = c.nextSiblingElement()) {
            if (c.tagName() == QLatin1String("field")) {
                const QString n = c.attribute(QStringLiteral("name")).trimmed();
                if (!n.isEmpty()) { seen.insert(n); }
            }
            walk(c);
        }
    };

    for (const QDomElement &p : m_packets) { walk(p); }
    for (auto it = m_structs.cbegin(); it != m_structs.cend(); ++it) {
        walk(it.value());
    }

    QStringList out = seen.values();
    out.sort(Qt::CaseInsensitive);
    return out;
}

QVector<Decoder::PacketFields> Decoder::fieldsByCaptype() const
{
    QVector<PacketFields> out;

    // Same <field> walk as allFieldNames, plus one thing it does not need to
    // do: follow <subpackets><case struct="…"> into the struct it names.
    // allFieldNames sweeps every struct anyway, so it never had to know
    // which packet a struct belonged to. Here that IS the question — the
    // eight SLRP sub-packets carry most of the fields worth pinning, and a
    // walk that stopped at the <case> element would offer an empty SLRP.
    //
    // `chased` is per packet and guards against a schema whose structs name
    // each other in a cycle: without it the walk would recurse until the
    // stack is gone, which a hand-edited schema can cause and should not.
    std::function<void(const QDomElement &, QSet<QString> &, QSet<QString> &, int)>
        walk = [&](const QDomElement &e, QSet<QString> &seen,
                   QSet<QString> &chased, int depth) {
        if (depth > kMaxStructDepth) { return; }
        for (QDomElement c = e.firstChildElement(); !c.isNull();
             c = c.nextSiblingElement()) {
            if (c.tagName() == QLatin1String("field")) {
                const QString n = c.attribute(QStringLiteral("name")).trimmed();
                if (!n.isEmpty()) { seen.insert(n); }
            } else if (c.tagName() == QLatin1String("case")) {
                const QString s = c.attribute(QStringLiteral("struct")).trimmed();
                if (!s.isEmpty() && !chased.contains(s)
                    && m_structs.contains(s)) {
                    chased.insert(s);
                    walk(m_structs.value(s), seen, chased, depth + 1);
                }
            }
            walk(c, seen, chased, depth + 1);
        }
    };

    for (const QDomElement &p : m_packets) {
        QSet<QString> seen;
        QSet<QString> chased;
        walk(p, seen, chased, 0);

        QStringList fields = seen.values();
        fields.sort(Qt::CaseInsensitive);

        const QString name = p.attribute(QStringLiteral("name"));
        const QStringList toks = captypeTokens(p.attribute(QStringLiteral("match")));
        for (const QString &t : toks) {
            out.push_back({ name, t, fields });
        }
        // A packet whose match= names no captype still has fields, and
        // dropping it would make them unreachable in a chooser built from
        // this. It gets an entry with no token, which narrows to nothing.
        if (toks.isEmpty()) { out.push_back({ name, QString(), fields }); }
    }
    return out;
}

bool Decoder::load(const QString &xmlPath, QString *err)
{
    m_loaded = false; m_structs.clear(); m_enums.clear(); m_packets.clear();
    m_eventtables.clear(); m_flagtables.clear();
    QFile f(xmlPath);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = "cannot open " + xmlPath; return false; }
    QString perr; int line = 0, col = 0;
    if (!m_doc.setContent(&f, &perr, &line, &col)) {
        if (err) *err = QStringLiteral("XML error at %1:%2 - %3").arg(line).arg(col).arg(perr);
        return false;
    }
    const QDomElement root = m_doc.documentElement();
    const QDomElement enums = root.firstChildElement("enums");
    for (QDomElement e = enums.firstChildElement("enum"); !e.isNull();
         e = e.nextSiblingElement("enum")) {
        m_enums.insert(e.attribute("name"), e);
    }
    for (QDomElement s = root.firstChildElement("struct"); !s.isNull();
         s = s.nextSiblingElement("struct")) {
        m_structs.insert(s.attribute("name"), s);
    }
    for (QDomElement p = root.firstChildElement("packet"); !p.isNull();
         p = p.nextSiblingElement("packet")) {
        m_packets.push_back(p);
    }
    const QDomElement ets = root.firstChildElement("eventtables");
    for (QDomElement et = ets.firstChildElement("eventtable"); !et.isNull();
         et = et.nextSiblingElement("eventtable")) {
        QHash<int, EventDef> tbl;
        for (QDomElement e = et.firstChildElement("event"); !e.isNull();
             e = e.nextSiblingElement("event")) {
            EventDef d;
            d.bytes    = e.attribute("bytes", "1").toInt();
            d.isSigned = e.attribute("signed") == "true";
            d.name     = e.attribute("name");
            tbl.insert(e.attribute("id").toInt(), d);
        }
        m_eventtables.insert(et.attribute("name"), tbl);
    }
    const QDomElement fts = root.firstChildElement("flagtables");
    for (QDomElement ft = fts.firstChildElement("flagtable"); !ft.isNull();
         ft = ft.nextSiblingElement("flagtable")) {
        QStringList names;
        for (QDomElement f = ft.firstChildElement("flag"); !f.isNull();
             f = f.nextSiblingElement("flag")) {
            names << f.attribute("name");
        }
        m_flagtables.insert(ft.attribute("name"), names);
    }
    if (m_packets.isEmpty()) {
        if (err) *err = "schema has no <packet> elements";
        return false;
    }
    // Refuse, don't guess: every when= / test= must parse, and must name a
    // field some packet or struct declares. A typo would otherwise decode
    // as "that field is 0" on every frame, silently, forever.
    QString condErr;
    if (!validateConditions(root, &condErr)) {
        // A refused schema leaves nothing behind, same as a malformed one:
        // half a schema must not be able to decode anything.
        m_structs.clear(); m_enums.clear(); m_packets.clear();
        m_eventtables.clear(); m_flagtables.clear();
        if (err) *err = condErr;
        return false;
    }
    m_loaded = true;
    return m_loaded;
}

bool Decoder::validateConditions(const QDomElement &root, QString *err)
{
    // Names a condition may refer to: whatever the walkers put in ctx.vals,
    // which is the name= and id= of elements inside <packet> and <struct>.
    QSet<QString> declared;
    std::function<void(const QDomElement &)> collect =
        [&](const QDomElement &e) {
            if (e.hasAttribute("name")) { declared.insert(e.attribute("name")); }
            if (e.hasAttribute("id"))   { declared.insert(e.attribute("id")); }
            for (QDomElement k = e.firstChildElement(); !k.isNull();
                 k = k.nextSiblingElement()) {
                collect(k);
            }
        };
    for (QDomElement e = root.firstChildElement(); !e.isNull();
         e = e.nextSiblingElement()) {
        if (e.tagName() == QLatin1String("packet")
            || e.tagName() == QLatin1String("struct")) {
            collect(e);
        }
    }

    QString failure;
    std::function<bool(const QDomElement &, const QString &)> check =
        [&](const QDomElement &e, const QString &owner) -> bool {
            for (const char *attr : { "when", "test" }) {
                if (!e.hasAttribute(attr)) { continue; }
                const QString cond = e.attribute(attr);
                QString nm;
                const QString why = checkCondition(cond, &nm);
                const QString where = QStringLiteral("<%1%2> in %3")
                    .arg(e.tagName(),
                         e.hasAttribute("name")
                             ? QStringLiteral(" name=\"%1\"").arg(e.attribute("name"))
                             : QString(),
                         owner);
                if (!why.isEmpty()) {
                    failure = QStringLiteral("bad condition %1=\"%2\" on %3: %4")
                                  .arg(QLatin1String(attr), cond, where, why);
                    return false;
                }
                if (!nm.isEmpty() && !declared.contains(nm)) {
                    failure = QStringLiteral("condition %1=\"%2\" on %3 names '%4', "
                                             "which no packet or struct declares")
                                  .arg(QLatin1String(attr), cond, where, nm);
                    return false;
                }
            }
            for (QDomElement k = e.firstChildElement(); !k.isNull();
                 k = k.nextSiblingElement()) {
                if (!check(k, owner)) { return false; }
            }
            return true;
        };
    for (QDomElement e = root.firstChildElement(); !e.isNull();
         e = e.nextSiblingElement()) {
        if (e.tagName() != QLatin1String("packet")
            && e.tagName() != QLatin1String("struct")) {
            continue;
        }
        const QString owner = QStringLiteral("%1 %2")
                                  .arg(e.tagName(), e.attribute("name"));
        if (!check(e, owner)) {
            if (err) { *err = failure; }
            return false;
        }
    }
    return true;
}

QStringList captypeTokens(const QString &match)
{
    const int eq = match.indexOf(QLatin1String("=="));
    if (eq < 0 || !match.startsWith(QLatin1String("captype"))) { return {}; }
    QStringList out;
    const QStringList parts = match.mid(eq + 2).split(QLatin1Char(','));
    for (const QString &t : parts) {
        const QString tok = t.trimmed();
        if (!tok.isEmpty()) { out << tok; }
    }
    return out;
}

const QDomElement *Decoder::packetFor(const QByteArray &frame, const QString &captype) const
{
    if (frame.isEmpty()) { return nullptr; }
    if (!captype.isEmpty()) {                          // primary: select by @token
        for (const QDomElement &p : m_packets) {
            const QStringList toks = captypeTokens(p.attribute("match"));
            for (const QString &t : toks) {
                if (t.compare(captype, Qt::CaseInsensitive) == 0) { return &p; }
            }
        }
    }
    const int top = (uchar(frame[0]) >> 4) & 0xF;      // fallback: top-nibble pkt_type
    for (const QDomElement &p : m_packets) {
        const QString m = p.attribute("match");
        if (m.startsWith("pkt_type==") && m.mid(10).toInt() == top) { return &p; }
    }
    return nullptr;
}

bool Decoder::handles(const QByteArray &frame, const QString &captype) const
{
    return m_loaded && packetFor(frame, captype) != nullptr;
}

// ---- enum resolution (mirrors engine.Schema.enum_label) --------------
qint64 Decoder::formula(const QString &expr, qint64 v)
{
    if (expr.isEmpty()) return v;
    if (expr.startsWith("v*")) return v * expr.mid(2).toLongLong();
    if (expr.startsWith("v+")) return v + expr.mid(2).toLongLong();
    if (expr.startsWith("v-")) return v - expr.mid(2).toLongLong();
    return v;
}
QString Decoder::fmtMap(const QDomElement &m, qint64 v, qint64 f)
{
    if (!m.hasAttribute("label")) {
        const QString unit = m.attribute("unit");
        return unit.isEmpty() ? QString::number(f)
                              : QStringLiteral("%1 %2").arg(f).arg(unit);
    }
    QString lbl = m.attribute("label");
    lbl.replace("%v", QString::number(v)).replace("%f", QString::number(f));
    const QString unit = m.attribute("unit");
    return unit.isEmpty() ? lbl : QStringLiteral("%1 %2").arg(lbl).arg(unit);
}
QString Decoder::enumLabel(const QString &enumName, qint64 v) const
{
    const QDomElement e = m_enums.value(enumName);
    if (e.isNull()) { return QString::number(v); }
    QDomElement def;
    for (QDomElement m = e.firstChildElement("map"); !m.isNull();
         m = m.nextSiblingElement("map")) {
        if (m.attribute("default") == "true") { def = m; continue; }
        if (m.hasAttribute("v") && m.attribute("v").toLongLong() == v) {
            return fmtMap(m, v, v);
        }
        if (m.hasAttribute("from")) {
            const qint64 lo = m.attribute("from").toLongLong();
            const qint64 hi = m.attribute("to").toLongLong();
            if (v >= lo && v <= hi) {
                return fmtMap(m, v, formula(m.attribute("formula"), v));
            }
        }
    }
    if (!def.isNull()) { return fmtMap(def, v, v); }
    return QString::number(v);
}

// ---- value rendering (mirrors engine._bare / _full) ------------------
QString Decoder::bareVal(const QDomElement &fld, qint64 v) const
{
    if (fld.hasAttribute("format")) return composite(fld.attribute("format"), v);
    if (fld.hasAttribute("enum"))   return enumLabel(fld.attribute("enum"), v);
    return QString::number(v);
}
QString Decoder::fullVal(const QDomElement &fld, qint64 v) const
{
    if (fld.hasAttribute("meaning")) {                 // per-eid meaning, shown alone
        const auto fn = m_meanings.value(fld.attribute("meaning"));
        return fn ? fn(fld.attribute("eid").toInt(), v) : QString::number(v);
    }
    if (fld.hasAttribute("format")) return composite(fld.attribute("format"), v);
    if (fld.hasAttribute("enum"))   return enumLabel(fld.attribute("enum"), v);
    if (fld.hasAttribute("hex")) {
        return QStringLiteral("0x%1").arg(v, fld.attribute("hex").toInt(), 16, QChar('0'));
    }
    if (fld.hasAttribute("hexup")) {
        return QStringLiteral("0x%1").arg(v, fld.attribute("hexup").toInt(), 16, QChar('0')).toUpper();
    }
    if (fld.hasAttribute("unit"))   return QStringLiteral("%1 %2").arg(v).arg(fld.attribute("unit"));
    return QString::number(v);
}

// ---- conditions (mirrors engine.cond_ok) -----------------------------
//
// Grammar — the whole of it, and checkCondition() below enforces it at load:
//
//     NAME OP LIT               OP is one of  ==  !=  >=  <=  >  <
//     NAME & LIT (==|!=) LIT    mask test (LSRP health rotates on FRAME_NUM & 7)
//     NAME in LIT..LIT          inclusive range
//     NAME not in LIT..LIT      negated inclusive range
//
// LIT is decimal or 0x-hex, optionally negative. Never octal: a leading zero
// is decimal, because "010" meaning 8 is a trap nobody writing a schema
// expects. Every path uses the same parser — before session 65 the mask path
// accepted hex and the plain path silently read "0x0F" as 0.
//
// At RUN time a field the frame does not carry evaluates as 0. That is
// deliberate — sub-packet fields are absent from most frames, and the reject
// rules rely on it. What must not happen is a NAME that no frame could ever
// carry (a typo), and that is caught at LOAD time, where it is refused.

namespace {

bool parseCondLiteral(const QString &text, qint64 *out)
{
    QString t = text.trimmed();
    bool neg = false;
    if (t.startsWith(QLatin1Char('-'))) { neg = true; t = t.mid(1).trimmed(); }
    if (t.isEmpty()) { return false; }
    bool ok = false;
    qint64 v = 0;
    if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        v = t.mid(2).toLongLong(&ok, 16);
        if (t.size() == 2) { ok = false; }
    } else {
        v = t.toLongLong(&ok, 10);
    }
    if (ok && out) { *out = neg ? -v : v; }
    return ok;
}

qint64 condLiteral(const QString &text)
{
    qint64 v = 0;
    parseCondLiteral(text, &v);      // well-formedness was proven at load
    return v;
}

bool isCondName(const QString &n)
{
    if (n.isEmpty()) { return false; }
    const QChar c0 = n.at(0);
    if (!(c0.isLetter() || c0 == QLatin1Char('_'))) { return false; }
    for (const QChar ch : n) {
        if (!(ch.isLetterOrNumber() || ch == QLatin1Char('_')
              || ch == QLatin1Char('.'))) {
            return false;
        }
    }
    return true;
}

// Splits "LO..HI". Both halves must be literals and LO <= HI.
bool parseRange(const QString &rng, qint64 *lo, qint64 *hi, QString *why)
{
    const int dd = rng.indexOf(QLatin1String(".."));
    if (dd < 0) { *why = QStringLiteral("range needs LO..HI"); return false; }
    if (!parseCondLiteral(rng.left(dd), lo) || !parseCondLiteral(rng.mid(dd + 2), hi)) {
        *why = QStringLiteral("range bounds must be numbers");
        return false;
    }
    if (*lo > *hi) { *why = QStringLiteral("range LO is above HI"); return false; }
    return true;
}

} // namespace

bool Decoder::condOk(const QString &when, const Ctx &ctx)
{
    return conditionHolds(when, ctx.vals);
}

QString Decoder::checkCondition(const QString &when, QString *name)
{
    if (name) { name->clear(); }
    const QString w = when.trimmed();
    if (w.isEmpty()) { return QString(); }
    QString why;
    qint64 lo = 0, hi = 0;

    auto takeName = [&](const QString &raw) -> bool {
        const QString n = raw.trimmed();
        if (!isCondName(n)) {
            why = QStringLiteral("'%1' is not a field name").arg(n);
            return false;
        }
        if (name) { *name = n; }
        return true;
    };

    const int notInAt = w.indexOf(QLatin1String(" not in "));
    if (notInAt >= 0) {
        if (!takeName(w.left(notInAt))) { return why; }
        return parseRange(w.mid(notInAt + 8).trimmed(), &lo, &hi, &why) ? QString() : why;
    }
    const int inAt = w.indexOf(QLatin1String(" in "));
    if (inAt >= 0) {
        if (!takeName(w.left(inAt))) { return why; }
        return parseRange(w.mid(inAt + 4).trimmed(), &lo, &hi, &why) ? QString() : why;
    }
    const int ampAt = w.indexOf(QLatin1Char('&'));
    if (ampAt >= 0) {
        if (!takeName(w.left(ampAt))) { return why; }
        const QString rest = w.mid(ampAt + 1);
        if (rest.contains(QLatin1Char('&'))) {
            return QStringLiteral("only one '&' mask is supported (no &&)");
        }
        const int eqAt = rest.indexOf(QLatin1String("=="));
        const int neAt = rest.indexOf(QLatin1String("!="));
        const int opAt = (eqAt >= 0) ? eqAt : neAt;
        if (opAt < 0) { return QStringLiteral("mask test needs == or !="); }
        if (!parseCondLiteral(rest.left(opAt), &lo)) {
            return QStringLiteral("mask must be a number");
        }
        if (!parseCondLiteral(rest.mid(opAt + 2), &hi)) {
            return QStringLiteral("mask comparand must be a number");
        }
        return QString();
    }
    static const char *ops[] = {"==","!=",">=","<=",">","<"};
    for (const char *op : ops) {
        const int k = w.indexOf(QLatin1String(op));
        if (k < 0) { continue; }
        if (!takeName(w.left(k))) { return why; }
        if (!parseCondLiteral(w.mid(k + int(qstrlen(op))), &lo)) {
            return QStringLiteral("right-hand side must be a number");
        }
        return QString();
    }
    return QStringLiteral("no operator (expected ==, !=, >=, <=, >, <, &, in, not in)");
}

bool Decoder::conditionHolds(const QString &when,
                             const QHash<QString, qint64> &vals)
{
    struct { const QHash<QString, qint64> &vals; } ctx{ vals };
    if (when.isEmpty()) { return true; }
    const int notInAt = when.indexOf(QLatin1String(" not in "));
    if (notInAt >= 0) {                              // "name not in LO..HI"
        const QString a = when.left(notInAt).trimmed();
        const QString rng = when.mid(notInAt + 8).trimmed();
        const int dd = rng.indexOf(QLatin1String(".."));
        const qint64 lo = condLiteral(rng.left(dd));
        const qint64 hi = condLiteral(rng.mid(dd + 2));
        const qint64 x = ctx.vals.value(a, 0);
        return !(x >= lo && x <= hi);
    }
    const int inAt = when.indexOf(QLatin1String(" in "));
    if (inAt >= 0) {                                  // "name in LO..HI"
        const QString a = when.left(inAt).trimmed();
        const QString rng = when.mid(inAt + 4).trimmed();
        const int dd = rng.indexOf(QLatin1String(".."));
        const qint64 lo = condLiteral(rng.left(dd));
        const qint64 hi = condLiteral(rng.mid(dd + 2));
        const qint64 x = ctx.vals.value(a, 0);
        return x >= lo && x <= hi;
    }
    // "name & MASK == VALUE" — needed for fields whose meaning is
    // multiplexed by low bits of another field. The LSRP health status is
    // the case in point: which six faults its six bits describe depends on
    // FRAME_NUM & 7, so without a mask test the field cannot be decoded at
    // all. Checked before the plain operators because the expression also
    // contains "==".
    const int ampAt = when.indexOf(QLatin1Char('&'));
    if (ampAt >= 0) {
        const QString lhs = when.left(ampAt).trimmed();
        const QString rest = when.mid(ampAt + 1).trimmed();
        const int eqAt = rest.indexOf(QLatin1String("=="));
        const int neAt = rest.indexOf(QLatin1String("!="));
        const int opAt = (eqAt >= 0) ? eqAt : neAt;
        if (opAt >= 0) {
            const qint64 mask = condLiteral(rest.left(opAt));
            const qint64 want = condLiteral(rest.mid(opAt + 2));
            const qint64 got  = ctx.vals.value(lhs, 0) & mask;
            return (eqAt >= 0) ? (got == want) : (got != want);
        }
    }

    static const char *ops[] = {"==","!=",">=","<=",">","<"};
    for (const char *op : ops) {
        const int k = when.indexOf(QLatin1String(op));
        if (k < 0) { continue; }
        const QString a = when.left(k).trimmed();
        const qint64  b = condLiteral(when.mid(k + int(qstrlen(op))));
        const qint64  x = ctx.vals.value(a, 0);
        const QString o = QString::fromLatin1(op);
        if (o == "==") { return x == b; }
        if (o == "!=") { return x != b; }
        if (o == ">=") { return x >= b; }
        if (o == "<=") { return x <= b; }
        if (o == ">")  { return x >  b; }
        return x < b;
    }
    // Unreachable for a schema that loaded: checkCondition() refuses any
    // condition without an operator. Kept as "holds" only so a caller that
    // skipped validation sees the pre-65 behaviour rather than a new one.
    return true;
}

// ---- flat walker: field / pad / repeat (mirrors engine.walk_flat) ----
void Decoder::walkFlat(QDomElement node, Cursor &c, Ctx &ctx,
                       QVector<FieldRow> &rows, const Geo &geo) const
{
    // Stamp every row this call produces with the bit range the cursor
    // consumed while producing it.
    //
    // Done once here rather than at each of the fifteen rows.push_back()
    // sites below: those are spread across every tag type, several are
    // inside loops, and the function has a dozen early returns — so a
    // per-site approach would be both invasive and easy to get subtly
    // wrong as tags are added. A scope guard captures the cursor delta
    // for whatever the body did, including tags added later.
    struct SpanStamper {
        Cursor &c; QVector<FieldRow> &rows;
        int startPos; int startRow;
        ~SpanStamper() {
            const int len = c.pos - startPos;
            if (len <= 0) return;                 // consumed nothing
            for (int i = startRow; i < rows.size(); ++i) {
                // Don't overwrite a narrower span already set by a nested
                // walkStruct/subpacket call — the inner one is more precise.
                if (rows[i].bitOffset < 0) {
                    rows[i].bitOffset = startPos;
                    rows[i].bitLength = len;
                }
            }
        }
    } stamper{ c, rows, c.pos, rows.size() };

    const QString tag = node.tagName();
    if (tag == "pad") { c.take(node.attribute("bits").toInt()); return; }

    if (tag == "align") {                              // round cursor to next byte
        if (c.pos & 7) { c.pos += 8 - (c.pos & 7); }
        return;
    }

    if (tag == "flags") {                              // N single-bit flags -> names
        const QStringList names = m_flagtables.value(node.attribute("table"));
        const int nbits = node.attribute("bits").toInt();

        // order="lsb": the table is indexed by BIT POSITION (bit 0 = LSB),
        // not by read order.
        //
        // In an msb-first packet the first bit read is the most significant
        // one, so read order runs backwards through bit positions. Firmware
        // that builds a field with SET_BIT(x, v, n) is numbering from the
        // LSB, and a table written to match that source would silently come
        // out reversed — every fault reported as its mirror-image
        // neighbour, which decodes cleanly and means something completely
        // different. This lets the XML list flags in the same order as the
        // #defines they came from.
        const bool lsbTable = (node.attribute("order") == QLatin1String("lsb"));

        // expand="all" emits a row per bit, not just the set ones.
        //
        // A summary that lists only faults is ambiguous in exactly the way
        // an acceptance test cannot tolerate: a subsystem missing from the
        // list might be healthy, or might be a bit the decoder never read.
        // The SIF cases ask you to confirm a SPECIFIC bit is 0 or 1, and
        // "it wasn't mentioned" is not evidence of either. With expand,
        // every bit states its own condition.
        const bool expandAll = (node.attribute("expand") == QLatin1String("all"));

        QVector<bool> bitSet(nbits, false);
        QStringList on;
        for (int i = 0; i < nbits; ++i) {
            const int idx = lsbTable ? (nbits - 1 - i) : i;
            const bool set = (c.take(1) != 0);
            if (idx >= 0 && idx < nbits) bitSet[idx] = set;
            if (set && idx < names.size() && idx >= 0) {
                on << names.at(idx);
            }
        }
        if (lsbTable) {
            // Restore ascending bit order for display; the loop above
            // collects them highest-bit-first.
            std::reverse(on.begin(), on.end());
        }
        c.take(node.attribute("pad", "0").toInt());
        rows.push_back({ QStringLiteral("  ") + node.attribute("name"),
                         on.isEmpty() ? QStringLiteral("(none)") : on.join(QStringLiteral("; ")) });

        if (expandAll) {
            for (int b = 0; b < nbits && b < names.size(); ++b) {
                // "1 (FAULT)" / "0 (ok)" so the row reads plainly AND
                // compares numerically: field:SPEED_SENSOR2_FAULT=1 works,
                // and so does ~FAULT.
                rows.push_back({ QStringLiteral("    ") + names.at(b),
                                 bitSet.at(b) ? QStringLiteral("1 (FAULT)")
                                              : QStringLiteral("0 (ok)") });
            }
        }
        return;
    }

    if (tag == "row") {                                // computed row from stored ids
        rows.push_back({ QStringLiteral("  ") + node.attribute("name"),
                         rowTmpl(node.attribute("tmpl"), ctx) });
        return;
    }

    if (tag == "when") {                               // conditional block of flat children
        if (condOk(node.attribute("test"), ctx)) {
            for (QDomElement k = node.firstChildElement(); !k.isNull();
                 k = k.nextSiblingElement()) {
                walkFlat(k, c, ctx, rows, geo);
            }
        }
        return;
    }

    if (tag == "note") {                               // literal annotation row
        if (!condOk(node.attribute("when"), ctx)) { return; }
        rows.push_back({ QStringLiteral("  ") + node.attribute("name"),
                         node.attribute("text") });
        return;
    }

    if (tag == "crc") {                                // stored value + computed check
        // Every one of at/bits/from/len is an unvalidated XML integer that
        // ends up as a buffer offset or length. Clamp each against the real
        // frame before it is used.
        if (node.hasAttribute("at")) {
            c.pos = qBound(0, node.attribute("at").toInt(), c.nbytes * 8);
        }
        const int nbits = qBound(1, node.attribute("bits", "32").toInt(), 32);
        const quint32 stored = c.take(nbits);          // lsb-first => LE stored value
        const auto fn = m_crcs.value(node.attribute("algo"));

        // Was: QByteArray(c.b, (c.limit + 7) / 8) — a deep copy of the frame
        // on EVERY crc field, sized from the schema-derived limit rather
        // than the real buffer. Use the true size, and build it once.
        const QByteArray frame(reinterpret_cast<const char *>(c.b), c.nbytes);
        const int frm = qBound(0, node.attribute("from").toInt(), c.nbytes);
        const int ln  = qBound(0, node.attribute("len").toInt(),  c.nbytes - frm);
        const bool ok = fn && (fn(frame, frm, ln) == stored);
        rows.push_back({ QStringLiteral("  ") + node.attribute("name"),
                         QStringLiteral("0x%1").arg(stored, 8, 16, QChar('0')).toUpper()
                             + (ok ? QStringLiteral("  PASS") : QStringLiteral("  FAIL")) });
        return;
    }

    if (tag == "group") {                              // sub-fields -> one row
        const QString sep = node.hasAttribute("sep") ? node.attribute("sep")
                                                     : QStringLiteral("  ");
        Ctx ev = ctx;
        Grp grp;
        const QString line = walkEntry(node, c, ev, grp, geo, sep);
        for (auto it = ev.vals.constBegin(); it != ev.vals.constEnd(); ++it) {
            ctx.vals.insert(it.key(), it.value());
        }
        rows.push_back({ QStringLiteral("  ") + node.attribute("name"), line });
        return;
    }

    if (tag == "eventstream") {                        // count + (id -> size/name) records
        const QHash<int, EventDef> tbl = m_eventtables.value(node.attribute("table"));
        const auto meaning = m_meanings.value(node.attribute("meaning"));
        const int cbits  = node.attribute("count_bits", "8").toInt();
        const int idbits = node.attribute("id_bits", "16").toInt();
        const int count  = int(c.take(cbits));
        for (int i = 0; i < count; ++i) {
            if (c.pos + idbits > c.limit) { break; }
            const int eid = int(c.take(idbits));
            const bool known = tbl.contains(eid);
            const EventDef ed = tbl.value(eid);
            const int sz = known ? ed.bytes : 1;
            if (c.pos + sz * 8 > c.limit) { break; }
            const quint32 raw = c.take(sz * 8);
            const qint64 v = (known && ed.isSigned) ? toSigned(raw, sz * 8) : qint64(raw);
            const QString name = known ? ed.name : QStringLiteral("EVENT_%1").arg(eid);
            QString val;
            if (meaning) {
                const QString m = meaning(eid, v);
                val = m.isEmpty() ? QString::number(v)
                                  : QStringLiteral("%1  (%2)").arg(v).arg(m);
            } else {
                val = QString::number(v);
            }
            rows.push_back({ QStringLiteral("  ") + name, val });
        }
        return;
    }

    if (tag == "field") {
        if (!condOk(node.attribute("when"), ctx)) { return; }
        qint64 iv = 0; double dv = 0.0; QString sv;
        const Kind k = readTyped(node, c, &iv, &dv, &sv);
        const bool hidden = (node.attribute("hide") == "true");
        if (k == Kind::Real) {
            if (hidden) { return; }
            rows.push_back({ QStringLiteral("  ") + node.attribute("name"),
                             fmtFloatVal(node, dv) });
            return;
        }
        if (k == Kind::Text) {
            if (hidden) { return; }
            rows.push_back({ QStringLiteral("  ") + node.attribute("name"),
                             sv.isEmpty() ? QStringLiteral("(empty)") : sv });
            return;
        }
        ctx.vals.insert(node.attribute("name"), iv);
        if (node.hasAttribute("id")) { ctx.vals.insert(node.attribute("id"), iv); }
        if (hidden) { return; }
        rows.push_back({ QStringLiteral("  ") + node.attribute("name"),
                         fullVal(node, iv) });
        return;
    }

    if (tag == "repeat") {
        const QString cattr = node.attribute("count");
        bool isNum = false;
        const qint64 lit = cattr.toLongLong(&isNum);
        qint64 cnt = isNum ? lit : ctx.vals.value(cattr, 0);
        const qint64 cmin = node.attribute("cmin", "0").toLongLong();
        const qint64 cmax = node.attribute("cmax", "63").toLongLong();
        if (cnt < cmin || cnt > cmax) { cnt = 0; }
        const QString nm = node.attribute("name");
        const int base = (node.attribute("index0") == "true") ? 0 : 1;
        const QString sep = node.hasAttribute("sep") ? node.attribute("sep")
                                                     : QStringLiteral("  ");
        if (node.attribute("expand") == "true") {  // each child field -> its own row
            QVector<QDomElement> kids;
            int ebits = 0;
            for (QDomElement k = node.firstChildElement("field"); !k.isNull();
                 k = k.nextSiblingElement("field")) {
                kids.push_back(k); ebits += fieldBits(k);
            }
            for (qint64 i = 0; i < cnt; ++i) {
                if (c.pos + ebits > c.limit) { break; }
                for (const QDomElement &k : kids) {
                    qint64 iv = 0; double dv = 0.0; QString sv;
                    const Kind kind = readTyped(k, c, &iv, &dv, &sv);
                    const QString label = QStringLiteral("  %1[%2] %3")
                                              .arg(nm).arg(i + base).arg(k.attribute("name"));
                    if (kind == Kind::Real) {
                        rows.push_back({ label, fmtFloatVal(k, dv) });
                    } else if (kind == Kind::Text) {
                        rows.push_back({ label, sv.isEmpty() ? QStringLiteral("(empty)") : sv });
                    } else {
                        ctx.vals.insert(k.attribute("name"), iv);
                        rows.push_back({ label, fullVal(k, iv) });
                    }
                }
            }
            return;
        }
        Grp grp;
        for (qint64 i = 0; i < cnt && c.pos < c.limit; ++i) {
            Ctx ev = ctx;                              // entry-local scope
            const QString line = walkEntry(node, c, ev, grp, geo, sep);
            rows.push_back({ QStringLiteral("  %1[%2]").arg(nm).arg(i + base), line });
        }
        return;
    }
}

// ---- entry walker (mirrors engine.walk_entry) ------------------------
QString Decoder::walkEntry(QDomElement rep, Cursor &c, Ctx ctx,
                           Grp &grp, const Geo &geo, const QString &sep) const
{
    QStringList toks;
    bool hasSeg=false, hasStart=false, hasLen=false, hasTag=false;
    qint64 segv=0, startv=0, lenv=0, tagv=0;

    for (QDomElement sub = rep.firstChildElement(); !sub.isNull();
         sub = sub.nextSiblingElement()) {
        const QString stag = sub.tagName();
        if (stag == "pad") { c.take(sub.attribute("bits").toInt()); continue; }
        if (stag != "field") { continue; }
        if (!condOk(sub.attribute("when"), ctx)) { continue; }
        qint64 iv = 0; double dv = 0.0; QString sv;
        const Kind kind = readTyped(sub, c, &iv, &dv, &sv);
        QString bare;
        if (kind == Kind::Real) {
            bare = fmtFloatVal(sub, dv);          // roles are integer-only; a real
        } else if (kind == Kind::Text) {          // carries no abs-location meaning
            bare = sv.isEmpty() ? QStringLiteral("(empty)") : sv;
        } else {
            const qint64 v = iv;
            ctx.vals.insert(sub.attribute("name"), v);
            if (sub.hasAttribute("id")) { ctx.vals.insert(sub.attribute("id"), v); }
            const QString role = sub.attribute("role");
            if      (role == "seglen") { hasSeg=true;   segv=v; }
            else if (role == "start")  { hasStart=true; startv=v; }
            else if (role == "len")    { hasLen=true;   lenv=v; }
            else if (role == "taghop") { hasTag=true;   tagv=v; }
            bare = bareVal(sub, v);
        }
        if (sub.attribute("hide") == "true") { continue; }
        if (sub.hasAttribute("tmpl")) {
            QString t = sub.attribute("tmpl"); t.replace("%s", bare); toks << t;
        } else {
            toks << QStringLiteral("%1=%2").arg(sub.attribute("name"), bare);
        }
    }
    QString suf;
    if (geo.known) {
        if (hasSeg) {
            const qint64 a = grp.seg, b = a + segv; grp.seg = b;
            suf = QStringLiteral("   [%1 \u2192 %2 m]")
                  .arg(geo.blockAbs + qint64(geo.travel)*a)
                  .arg(geo.blockAbs + qint64(geo.travel)*b);
        } else if (hasStart && hasLen) {
            suf = QStringLiteral("   [%1 \u2192 %2 m]")
                  .arg(geo.blockAbs + qint64(geo.travel)*startv)
                  .arg(geo.blockAbs + qint64(geo.travel)*(startv+lenv));
        } else if (hasStart) {
            suf = QStringLiteral("   abs=%1 m").arg(geo.blockAbs + qint64(geo.travel)*startv);
        } else if (hasTag) {
            grp.tag += tagv;
            suf = QStringLiteral("   abs=%1 m").arg(geo.blockAbs + qint64(geo.travel)*grp.tag);
        }
    }
    return toks.join(sep) + suf;
}

void Decoder::walkStruct(const QString &name, Cursor &c, Ctx ctx,
                         QVector<FieldRow> &rows, const Geo &geo,
                         int depth) const
{
    if (depth > kMaxStructDepth) {
        rows.push_back({ QStringLiteral("  <schema error>"),
                         QStringLiteral("struct nesting past %1 levels at '%2' "
                                        "— cyclic schema?")
                             .arg(kMaxStructDepth).arg(name) });
        return;
    }
    const QDomElement st = m_structs.value(name);
    if (st.isNull()) { return; }
    for (QDomElement fld = st.firstChildElement(); !fld.isNull();
         fld = fld.nextSiblingElement()) {
        walkFlat(fld, c, ctx, rows, geo);
    }
}

// ---- top-level decode (mirrors engine.decode) ------------------------
QVector<FieldRow> Decoder::decode(const QByteArray &frame,
                                  const QHash<int, qint64> &tagLoc,
                                  const QString &captype,
                                  int bodyOffsetOverrideBits,
                                  QHash<QString, qint64> *rawValues) const
{
    QVector<FieldRow> rows;
    if (rawValues) { rawValues->clear(); }
    const QDomElement *pkt = packetFor(frame, captype);
    if (!pkt) { return rows; }

    const int n = frame.size();
    const bool msb = pkt->attribute("wire", "msb-first") != "lsb-first";

    // Clamp both schema-supplied offsets into the frame. A negative
    // trailer_bytes used to push `limit` past the end of the buffer; a
    // negative body_offset made the very first take() index before it.
    const int trailer = qBound(0, pkt->attribute("trailer_bytes", "4").toInt(), n);
    const int schemaOff = (bodyOffsetOverrideBits >= 0)
                              ? bodyOffsetOverrideBits
                              : pkt->attribute("body_offset", "0").toInt();
    const int boff    = qBound(0, schemaOff, n * 8);

    Cursor c { reinterpret_cast<const uchar *>(frame.constData()),
               boff, (n - trailer) * 8, msb, n };
    Ctx ctx;
    Geo nogeo;

    for (QDomElement node = pkt->firstChildElement(); !node.isNull();
         node = node.nextSiblingElement()) {
        if (node.tagName() == "subpackets") {
            // compose abs-location geometry from header fields (Annexure-C)
            Geo geo;
            const int  ref  = int(ctx.vals.value("LAST_REF_RFID", 0));
            const qint64 dps  = ctx.vals.value("DIST_PKT_START", 0);
            const qint64 pdir = ctx.vals.value("PKT_DIR", 0);
            if (tagLoc.contains(ref)) {
                geo.known  = true;
                geo.refAbs = tagLoc.value(ref);
                geo.travel = (pdir == 2) ? -1 : 1;
                // Single spatial origin for the whole look-ahead profile: the
                // START SIGNAL the MA/profile is issued from. DIST_PKT_START is
                // the travel-signed gap from LAST_REF_RFID to that signal, so
                // blockAbs = refAbs + travel*dps. Every overlay AND the tag list
                // are look-ahead distances measured from this single origin.
                geo.blockAbs = geo.refAbs + qint64(geo.travel) * dps;
            }

            const int tb = node.attribute("type_bits").toInt();
            const int lb = node.attribute("len_bits").toInt();
            const int reserve = node.attribute("reserve_tail", "8").toInt();
            QHash<int, QString> cases;
            for (QDomElement cs = node.firstChildElement("case"); !cs.isNull();
                 cs = cs.nextSiblingElement("case")) {
                cases.insert(cs.attribute("type").toInt(), cs.attribute("struct"));
            }
            int idx = 1;
            while (c.pos / 8 < n - reserve) {
                const int sb = c.pos / 8;
                const int t  = int(c.take(tb));
                const int L  = int(c.take(lb));
                if (L == 0 || sb + L > n - 4) { break; }
                const QString sname = cases.value(t);
                rows.push_back({ QStringLiteral("subpkt[%1] %2 @byte %3")
                                 .arg(idx)
                                 .arg(sname.isEmpty() ? QStringLiteral("reserved(%1)").arg(t) : sname)
                                 .arg(sb),
                                 QStringLiteral("%1 B").arg(L) });
                if (!sname.isEmpty() && m_structs.contains(sname)) {
                    Cursor sc { c.b, sb * 8 + tb + lb, sb * 8 + L * 8, msb, n };
                    walkStruct(sname, sc, ctx, rows, geo, 1);
                }
                c.pos = sb * 8 + L * 8;
                ++idx;
            }
            const int endByte = c.pos / 8;
            const int rem = (n - 4) - endByte;
            if (rem == 4) {
                rows.push_back({ QStringLiteral("  MAC_CODE"), hexRange(c.b, endByte, 4) });
            } else if (rem > 0) {
                rows.push_back({ QStringLiteral("  unparsed (raw)"), hexRange(c.b, endByte, rem) });
            }
        } else {
            walkFlat(node, c, ctx, rows, nogeo);
        }
    }

    // Hand back the numeric context the walk built. Published only on
    // request: every existing caller wants rows and nothing else.
    if (rawValues) { *rawValues = ctx.vals; }
    return rows;
}

} // namespace Schema
