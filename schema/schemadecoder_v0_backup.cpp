// =====================================================================
//  schemadecoder.cpp -- see header. Mirrors the validated reference
//  engine (engine.py): same cursor, field/pad/repeat/subpackets handling,
//  same enum resolution. Validated row-for-row against real SLRP captures.
// =====================================================================
#include "schemadecoder.h"

#include <QFile>

namespace Schema {

// ---- bit cursor ------------------------------------------------------
quint32 Decoder::Cursor::take(int n)
{
    quint32 v = 0;
    for (int i = 0; i < n; ++i) {
        if (pos > limit - 1) { ++pos; continue; }          // past end -> zero-fill
        const int byte = pos >> 3, off = pos & 7;
        const int bit  = msb ? ((b[byte] >> (7 - off)) & 1)
                             : ((b[byte] >> off) & 1);
        v = msb ? ((v << 1) | bit) : (v | (quint32(bit) << i));
        ++pos;
    }
    return v;
}

static qint64 toSigned(quint32 v, int n)
{
    if (n < 32 && (v & (1u << (n - 1)))) { return qint64(v) - (qint64(1) << n); }
    return qint64(v);
}

// ---- load + index ----------------------------------------------------
bool Decoder::load(const QString &xmlPath, QString *err)
{
    m_loaded = false; m_structs.clear(); m_enums.clear(); m_packets.clear();
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
    m_loaded = !m_packets.isEmpty();
    if (!m_loaded && err) *err = "schema has no <packet> elements";
    return m_loaded;
}

// ---- packet selection (match="pkt_type==N", N on the top nibble) -----
const QDomElement *Decoder::packetFor(const QByteArray &frame) const
{
    if (frame.isEmpty()) { return nullptr; }
    const int top = (uchar(frame[0]) >> 4) & 0xF;
    for (const QDomElement &p : m_packets) {
        const QString m = p.attribute("match");
        if (m.startsWith("pkt_type==") && m.mid(10).toInt() == top) { return &p; }
    }
    return nullptr;
}

bool Decoder::handles(const QByteArray &frame) const
{
    return m_loaded && packetFor(frame) != nullptr;
}

// ---- enum resolution -------------------------------------------------
QString Decoder::enumLabel(const QString &enumName, qint64 v) const
{
    const QDomElement e = m_enums.value(enumName);
    if (e.isNull()) { return QString::number(v); }
    for (QDomElement m = e.firstChildElement("map"); !m.isNull();
         m = m.nextSiblingElement("map")) {
        if (m.hasAttribute("v") && m.attribute("v").toLongLong() == v) {
            return m.attribute("label", QString::number(v));
        }
        if (m.hasAttribute("from")) {
            const qint64 lo = m.attribute("from").toLongLong();
            const qint64 hi = m.attribute("to").toLongLong();
            if (v >= lo && v <= hi) {
                if (m.hasAttribute("formula")) {
                    // only "v*K" / "v+K" supported; keep the schema simple
                    const QString f = m.attribute("formula");
                    qint64 out = v;
                    if (f.startsWith("v*")) { out = v * f.mid(2).toLongLong(); }
                    else if (f.startsWith("v+")) { out = v + f.mid(2).toLongLong(); }
                    const QString u = m.attribute("unit");
                    return u.isEmpty() ? QString::number(out)
                                       : QStringLiteral("%1 %2").arg(out).arg(u);
                }
                return m.attribute("label", QString::number(v));
            }
        }
    }
    return QString::number(v);
}

// ---- when="id OP literal" -------------------------------------------
bool Decoder::condOk(const QString &when, const Ctx &ctx)
{
    if (when.isEmpty()) { return true; }
    static const char *ops[] = {"==","!=",">=","<=",">","<"};
    for (const char *op : ops) {
        const int k = when.indexOf(QLatin1String(op));
        if (k < 0) { continue; }
        const QString a = when.left(k).trimmed();
        const qint64  b = when.mid(k + int(qstrlen(op))).trimmed().toLongLong();
        const qint64  x = ctx.ids.value(a, 0);
        const QString o = QString::fromLatin1(op);
        if (o == "==") return x == b;  if (o == "!=") return x != b;
        if (o == ">=") return x >= b;  if (o == "<=") return x <= b;
        if (o == ">")  return x >  b;  return x < b;
    }
    return true;
}

// ---- field / pad / repeat walker ------------------------------------
void Decoder::walk(QDomElement node, Cursor &c, Ctx &ctx,
                   QVector<FieldRow> &rows, const QString &prefix) const
{
    const QString tag = node.tagName();
    if (tag == "pad") { c.take(node.attribute("bits").toInt()); return; }

    if (tag == "field") {
        if (!condOk(node.attribute("when"), ctx)) { return; }
        const int n = node.attribute("bits").toInt();
        const quint32 raw = c.take(n);
        const qint64 val = (node.attribute("signed") == "true") ? toSigned(raw, n)
                                                                 : qint64(raw);
        if (node.hasAttribute("id")) { ctx.ids.insert(node.attribute("id"), val); }
        QString disp;
        if (node.hasAttribute("enum"))      disp = enumLabel(node.attribute("enum"), val);
        else if (node.hasAttribute("unit")) disp = QStringLiteral("%1 %2").arg(val).arg(node.attribute("unit"));
        else                                disp = QString::number(val);
        rows.push_back({ prefix + node.attribute("name"), disp });
        return;
    }

    if (tag == "repeat") {
        const int cnt = int(ctx.ids.value(node.attribute("count"), 0));
        const QString nm = node.attribute("name");
        for (int i = 0; i < cnt && c.pos < c.limit; ++i) {
            Ctx local = ctx;                 // entry-local ids (e.g. class)
            QStringList parts;
            for (QDomElement sub = node.firstChildElement(); !sub.isNull();
                 sub = sub.nextSiblingElement()) {
                QVector<FieldRow> tmp;
                walk(sub, c, local, tmp, QString());
                for (const FieldRow &fr : tmp) {
                    parts << QStringLiteral("%1=%2").arg(fr.field.trimmed(), fr.value);
                }
            }
            rows.push_back({ QStringLiteral("  %1[%2]").arg(nm).arg(i + 1),
                             parts.join("  ") });
        }
        return;
    }
}

void Decoder::walkStruct(const QString &name, Cursor &c, Ctx ctx,
                         QVector<FieldRow> &rows) const
{
    const QDomElement st = m_structs.value(name);
    if (st.isNull()) { return; }
    for (QDomElement fld = st.firstChildElement(); !fld.isNull();
         fld = fld.nextSiblingElement()) {
        walk(fld, c, ctx, rows, QStringLiteral("  "));
    }
}

// ---- top-level decode ------------------------------------------------
QVector<FieldRow> Decoder::decode(const QByteArray &frame) const
{
    QVector<FieldRow> rows;
    const QDomElement *pkt = packetFor(frame);
    if (!pkt) { return rows; }

    const int n = frame.size();
    const bool msb = pkt->attribute("wire", "msb-first") != "lsb-first";
    Cursor c { reinterpret_cast<const uchar *>(frame.constData()), 0, (n - 4) * 8, msb };
    Ctx ctx;

    for (QDomElement node = pkt->firstChildElement(); !node.isNull();
         node = node.nextSiblingElement()) {
        if (node.tagName() == "subpackets") {
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
                                 .arg(idx).arg(sname.isEmpty() ? QStringLiteral("reserved(%1)").arg(t) : sname).arg(sb),
                                 QStringLiteral("%1 B").arg(L) });
                if (!sname.isEmpty() && m_structs.contains(sname)) {
                    Cursor sc { c.b, sb * 8 + tb + lb, sb * 8 + L * 8, msb };
                    walkStruct(sname, sc, ctx, rows);
                }
                c.pos = sb * 8 + L * 8;
                ++idx;
            }
        } else {
            walk(node, c, ctx, rows, QString());
        }
    }
    return rows;
}

} // namespace Schema
