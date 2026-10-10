#include "rfidtag.h"

#include "capturedecoder.h"
#include "packetbuilder.h"
#include "schema/schemadecoder.h"

#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QXmlStreamWriter>

namespace RfidTag {

namespace {

const QString kRfid = QStringLiteral("rfid");

// The encoder the Packet Maker builds with: the external kavach.xml when
// Settings names a loadable one, else the built-in copy.
// shortcut: loaded once per process, so a schema changed in Settings applies
// after a restart; reload it here if that ever matters while the window is open.
const Schema::Encoder &encoder()
{
    static const PacketBuilder builder;
    return builder.encoder();
}

quint64 leWord(const QByteArray &b, int off)
{
    quint64 v = 0;
    for (int i = 0; i < 8; ++i) v |= quint64(quint8(b.at(off + i))) << (8 * i);
    return v;
}

bool parseHex64(const QString &s, quint64 *v)
{
    const QString t = s.trimmed();
    if (t.isEmpty() || t.size() > 16) return false;
    bool ok = false;
    *v = t.toULongLong(&ok, 16);
    return ok;
}

QString hex16(quint64 v) { return QStringLiteral("%1").arg(v, 16, 16, QLatin1Char('0')); }

QByteArray frameOf(const QByteArray &tag) { return QByteArray(1, char(1)) + tag; }

}  // namespace

// --- page_x / page_y ---------------------------------------------------------

QByteArray fromPages(const QString &px, const QString &py, QString *err)
{
    quint64 x = 0, y = 0;
    if (!parseHex64(px, &x) || !parseHex64(py, &y)) {
        if (err) *err = QStringLiteral("page_x and page_y must each be 1 to 16 hex digits");
        return {};
    }
    QByteArray tag(TagBytes, '\0');
    for (int i = 0; i < 8; ++i) {
        tag[i] = char((x >> (8 * i)) & 0xFF);
        tag[8 + i] = char((y >> (8 * i)) & 0xFF);
    }
    return tag;
}

QString pageX(const QByteArray &tag) { return tag.size() == TagBytes ? hex16(leWord(tag, 0)) : QString(); }
QString pageY(const QByteArray &tag) { return tag.size() == TagBytes ? hex16(leWord(tag, 8)) : QString(); }

// --- fields ------------------------------------------------------------------

QHash<QString, qint64> values(const QByteArray &tag, QString *err)
{
    if (tag.size() != TagBytes) {
        if (err) *err = QStringLiteral("a tag is 16 bytes");
        return {};
    }
    Schema::ParsedPacket pp = encoder().parseBody(kRfid, frameOf(tag));
    if (!pp.ok) {
        if (err) *err = pp.error;
        return {};
    }
    pp.header.remove(QStringLiteral("reader_id"));
    return pp.header;
}

QByteArray build(const QHash<QString, qint64> &v, QString *err)
{
    if (!encoder().isLoaded()) {
        if (err) *err = QStringLiteral("the schema is not loaded");
        return {};
    }
    // The encoder masks a value to its field width; refuse instead, so a
    // location typed one digit too long is not silently cut short.
    const int type = int(v.value(QStringLiteral("type")));
    QHash<QString, qint64> in;
    for (const Field &f : fieldsOf(type)) {
        const qint64 x = v.value(f.name);
        if (x < 0 || (f.bits < 63 && x >= (qint64(1) << f.bits))) {
            if (err) *err = QStringLiteral("%1 = %2 does not fit its %3 bits (0 to %4)")
                                .arg(f.name).arg(x).arg(f.bits).arg((qint64(1) << f.bits) - 1);
            return {};
        }
        in.insert(f.name, x);
    }
    in.insert(QStringLiteral("type"), type);
    in.insert(QStringLiteral("reader_id"), 1);
    QString e;
    const QByteArray frame = encoder().encodeBody(kRfid, in, {}, &e);
    if (frame.size() != 1 + TagBytes) {
        if (err) *err = e.isEmpty() ? QStringLiteral("the schema built %1 bytes, not 17").arg(frame.size()) : e;
        return {};
    }
    return frame.mid(1);
}

QVector<Field> fieldsOf(int type)
{
    QVector<Field> out;
    if (type < 0 || type > 15) return out;
    // Which fields a type carries is the schema's `when` gating: read it off
    // a tag of that type rather than evaluating the conditions a second time.
    QHash<QString, qint64> in{ { QStringLiteral("type"), type }, { QStringLiteral("reader_id"), 1 } };
    const QByteArray frame = encoder().encodeBody(kRfid, in, {});
    const QHash<QString, qint64> present = encoder().parseBody(kRfid, frame).header;
    for (const Schema::FieldInfo &f : encoder().packet(kRfid).header) {
        if (f.isPad || f.isCrc || f.name == QLatin1String("reader_id") || f.name == QLatin1String("type")) continue;
        if (!present.contains(f.name)) continue;
        out.append({ f.name, f.bits, f.enumName });
    }
    return out;
}

QString enumLabel(const QString &enumName, qint64 v)
{
    // The decoder's labels, not the encoder's: Schema::Encoder::enumChoices
    // finds no enum in a schema that nests them in <enums> (kavach.xml does).
    return kavachSchema().enumLabel(enumName, v);
}

QVector<QPair<QString, QString>> describe(const QByteArray &tag)
{
    QVector<QPair<QString, QString>> rows;
    if (tag.size() != TagBytes) return rows;
    QString hex;
    for (char b : frameOf(tag)) hex += QStringLiteral(" %1").arg(uchar(b), 2, 16, QLatin1Char('0')).toUpper();
    const CaptureLine c = CaptureDecoder::parseLine(QStringLiteral("@rfid_0_0 2000-01-01T00:00:00 1") + hex);
    for (const FieldRow &r : CaptureDecoder::describe(c))
        if (r.field != QLatin1String("reader_id")) rows.append({ r.field, r.value });
    return rows;
}

Summary summary(const QByteArray &tag)
{
    Summary s;
    if (tag.size() != TagBytes) return s;
    const RfidInfo t = CaptureDecoder::decodeRfid(frameOf(tag));
    s.ok = true;                // decodeRfid reads any 17 bytes
    s.type = t.type;
    s.unique = t.unique;
    s.duplicate = t.dup == 1;
    s.absLoc = t.absLoc;
    s.tinNom = t.tinNom;
    s.tinRev = t.tinRev;
    s.placement = t.place;
    s.crcOk = t.crcOk;
    s.crcStored = t.crcStored;
    s.crcCalc = t.crcCalc;
    return s;
}

QByteArray fixCrc(const QByteArray &tag)
{
    if (tag.size() != TagBytes) return tag;
    const Summary s = summary(tag);
    if (s.crcOk) return tag;
    quint64 y = leWord(tag, 8);
    y = (y & ((quint64(1) << 34) - 1)) | (quint64(s.crcCalc) << 34);
    QByteArray out = tag;
    for (int i = 0; i < 8; ++i) out[8 + i] = char((y >> (8 * i)) & 0xFF);
    return out;
}

QString nameOf(const QByteArray &tag)
{
    const Summary s = summary(tag);
    return s.ok ? QString::number(s.unique) + (s.duplicate ? QStringLiteral("D") : QString()) : QString();
}

QString typeName(int type)
{
    switch (type) {
    case 9:  return QStringLiteral("Normal");
    case 10: return QStringLiteral("LC gate");
    case 11: return QStringLiteral("Adjacent line");
    case 12: return QStringLiteral("Adjustment/Junction");
    default: return QStringLiteral("type %1 (not a tag type)").arg(type);
    }
}

// --- routes ------------------------------------------------------------------

namespace {

Tag tagFrom(const QDomElement &e, const QString &nameAttr, QStringList *notes)
{
    QString err;
    Tag t;
    t.name = e.attribute(nameAttr);
    t.bytes = fromPages(e.attribute(QStringLiteral("page_x")), e.attribute(QStringLiteral("page_y")), &err);
    if (t.bytes.isEmpty() && notes)
        *notes << QStringLiteral("line %1 (%2): %3").arg(e.lineNumber()).arg(t.name, err);
    return t;
}

}  // namespace

QVector<Route> readXml(const QByteArray &xml, QString *err, QStringList *notes)
{
    QVector<Route> out;
    QDomDocument doc;
    QString msg;
    int line = 0;
    if (!doc.setContent(xml, &msg, &line)) {
        if (err) *err = QStringLiteral("not XML (line %1: %2)").arg(line).arg(msg);
        return out;
    }
    const QDomElement root = doc.documentElement();
    if (root.tagName() == QLatin1String("tag_route")) {
        Route r;
        r.name = root.attribute(QStringLiteral("name"));
        r.dir = root.attribute(QStringLiteral("dir")).toInt();
        for (QDomElement e = root.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (e.tagName() == QLatin1String("tag")) {
                const Tag t = tagFrom(e, QStringLiteral("name"), notes);
                if (!t.bytes.isEmpty()) r.tags.append(t);
            } else if (e.tagName() == QLatin1String("signal")) {
                r.signalList.append({ e.attribute(QStringLiteral("foot_tag")), e.attribute(QStringLiteral("name")),
                                      e.attribute(QStringLiteral("sig_id")) });
            }
        }
        out.append(r);
        return out;
    }
    // route.xml (flat <route_data>, or <route> holding two) and
    // Configuration1.xml (<LOCO> holding many): every <route_data> is a route.
    const QDomNodeList blocks = root.tagName() == QLatin1String("route_data")
                                    ? QDomNodeList() : doc.elementsByTagName(QStringLiteral("route_data"));
    QVector<QDomElement> els;
    if (root.tagName() == QLatin1String("route_data")) els << root;
    for (int i = 0; i < blocks.size(); ++i) els << blocks.at(i).toElement();
    for (const QDomElement &b : els) {
        Route r;
        r.name = b.attribute(QStringLiteral("route_name"));
        r.dir = b.attribute(QStringLiteral("dir")).toInt();
        for (QDomElement e = b.firstChildElement(QStringLiteral("rfid_data")); !e.isNull();
             e = e.nextSiblingElement(QStringLiteral("rfid_data"))) {
            const Tag t = tagFrom(e, QStringLiteral("rfid_id"), notes);
            if (!t.bytes.isEmpty()) r.tags.append(t);
        }
        out.append(r);
    }
    if (out.isEmpty() && err) *err = QStringLiteral("no <tag_route> or <route_data> in it");
    return out;
}

QVector<Route> readFile(const QString &path, QString *err, QStringList *notes)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return {};
    }
    QVector<Route> routes = readXml(f.readAll(), err, notes);
    // A flat route.xml has no name: the file's is the best there is.
    for (Route &r : routes)
        if (r.name.isEmpty()) r.name = QFileInfo(path).completeBaseName().remove(QStringLiteral(".tagroute"));
    return routes;
}

QByteArray toTagRouteXml(const Route &r)
{
    // QXmlStreamWriter keeps attributes in the order written (QDom does not),
    // so the same route always makes the same file.
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(4);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("tag_route"));
    w.writeAttribute(QStringLiteral("name"), r.name);
    w.writeAttribute(QStringLiteral("dir"), QString::number(r.dir));
    for (const Tag &t : r.tags) {
        w.writeEmptyElement(QStringLiteral("tag"));
        w.writeAttribute(QStringLiteral("name"), t.name);
        w.writeAttribute(QStringLiteral("page_x"), pageX(t.bytes));
        w.writeAttribute(QStringLiteral("page_y"), pageY(t.bytes));
    }
    for (const Signal &sg : r.signalList) {
        w.writeEmptyElement(QStringLiteral("signal"));
        w.writeAttribute(QStringLiteral("foot_tag"), sg.footTag);
        w.writeAttribute(QStringLiteral("name"), sg.name);
        w.writeAttribute(QStringLiteral("sig_id"), sg.sigId);
    }
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

namespace {

struct Adjust { qint64 from = -1, to = -1; bool mirrored = false; bool ruled = false; };

// tags_sim's get_nom_dir_adjustment / get_rev_dir_adjustment.
Adjust adjustOf(const QHash<QString, qint64> &v, bool nominal)
{
    const qint64 l1 = v.value(QStringLiteral("abs_loc_1")), l2 = v.value(QStringLiteral("abs_loc_2"));
    const int d = int(v.value(nominal ? QStringLiteral("dir_corr_1") : QStringLiteral("dir_corr_2")));
    Adjust a;
    if (nominal) {
        if (d == 1) a = { l1, l2, false, true };
        else if (d == 4) a = { l2, l1, false, true };
        else if (d == 2) a = { l1, l2, true, true };
    } else {
        if (d == 4) a = { l2, l1, false, true };
        else if (d == 1) a = { l1, l2, false, true };
        else if (d == 3) a = { l1, l2, true, true };
    }
    return a;
}

}  // namespace

QVector<RouteRow> routeRows(const QVector<Tag> &tags, int dir)
{
    const bool nominal = dir != DirReverse;
    QVector<RouteRow> rows;
    QVector<Summary> sum;
    for (const Tag &t : tags) {
        const Summary s = summary(t.bytes);
        sum << s;
        RouteRow r;
        r.rfidId = nameOf(t.bytes);
        r.tagName = s.duplicate ? QStringLiteral("duplicate") : QStringLiteral("main");
        r.tagType = s.type;
        r.absLoc = r.ownLoc = s.absLoc;
        r.trackId = s.tinRev;
        r.pageX = pageX(t.bytes);
        r.pageY = pageY(t.bytes);
        rows.append(r);
    }

    // Main tags in first-seen order, as tags_sim's dictionary keeps them.
    QVector<int> order;
    QHash<int, qint64> own;
    QHash<int, Adjust> adjustAt;
    for (int i = 0; i < tags.size(); ++i) {
        if (sum.at(i).type == 12) {
            adjustAt.insert(sum.at(i).unique, adjustOf(values(tags.at(i).bytes), nominal));
            if (!adjustAt.value(sum.at(i).unique).ruled && !sum.at(i).duplicate)
                rows[i].adjustNote = QStringLiteral("dir_corr_%1 = %2: no correction for this direction; kept at its own location")
                                         .arg(nominal ? 1 : 2)
                                         .arg(values(tags.at(i).bytes).value(nominal ? QStringLiteral("dir_corr_1")
                                                                                     : QStringLiteral("dir_corr_2")));
        }
        if (sum.at(i).duplicate) continue;
        if (!own.contains(sum.at(i).unique)) order << sum.at(i).unique;
        own.insert(sum.at(i).unique, sum.at(i).absLoc);
    }
    QHash<int, qint64> adjusted;
    qint64 distance = 0, to = 0;
    bool mirrored = false;
    for (int u : order) {
        const qint64 l = own.value(u);
        if (distance != 0) {
            const qint64 diff = qAbs(to - l);
            adjusted.insert(u, distance + l + (mirrored ? (nominal ? 2 : -2) * diff : 0));
        }
        if (adjustAt.contains(u)) {
            const Adjust a = adjustAt.value(u);
            if (!a.ruled) {
                distance = 0;           // tags_sim: (-1) - (-1): the tags after it are not corrected
                continue;
            }
            to = a.to;
            distance = a.from - a.to;
            mirrored = a.mirrored;
            adjusted.insert(u, a.from);
        }
    }

    for (int i = 0; i < rows.size(); ++i) {
        const bool mine = !sum.at(i).duplicate && adjusted.contains(sum.at(i).unique);
        if (mine) rows[i].absLoc = adjusted.value(sum.at(i).unique);
        if (sum.at(i).type == 12 && mine && rows.at(i).absLoc != rows.at(i).ownLoc)
            rows[i].adjustNote = QStringLiteral("written at %1 m (its own location %2 m)").arg(rows.at(i).absLoc).arg(rows.at(i).ownLoc);
        if (i + 1 < rows.size()) {
            const Summary &n = sum.at(i + 1);
            rows[i].nextAbsLoc = mine && !n.duplicate ? adjusted.value(n.unique, n.absLoc) : n.absLoc;
        } else {
            rows[i].nextAbsLoc = rows.at(i).absLoc;
        }
    }
    return rows;
}

QByteArray toRouteXml(const Route &r, QString *err)
{
    if (r.dir != DirNominal && r.dir != DirReverse) {
        if (err) *err = QStringLiteral("set the route's direction first: the REV route and the names depend on it");
        return {};
    }
    if (r.tags.isEmpty()) {
        if (err) *err = QStringLiteral("the route has no tags");
        return {};
    }
    // tags_sim names a route by its first and last main tags.
    QString first, last;
    for (const Tag &t : r.tags) {
        if (summary(t.bytes).duplicate) continue;
        if (first.isEmpty()) first = nameOf(t.bytes);
        last = nameOf(t.bytes);
    }
    const QString side = r.dir == DirNominal ? QStringLiteral("DN_") : QStringLiteral("UP_");
    const QString ends = first + QLatin1Char('_') + last;

    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(4);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("route"));
    auto block = [&](const QString &name, int dir, const QVector<RouteRow> &rows) {
        w.writeStartElement(QStringLiteral("route_data"));
        w.writeAttribute(QStringLiteral("route_name"), name);
        w.writeAttribute(QStringLiteral("dir"), QString::number(dir));
        for (const RouteRow &row : rows) {
            w.writeEmptyElement(QStringLiteral("rfid_data"));
            w.writeAttribute(QStringLiteral("rfid_id"), row.rfidId);
            w.writeAttribute(QStringLiteral("tag_name"), row.tagName);
            w.writeAttribute(QStringLiteral("tag_type"), QString::number(row.tagType));
            w.writeAttribute(QStringLiteral("abs_loc"), QString::number(row.absLoc));
            w.writeAttribute(QStringLiteral("next_rfid_abs_loc"), QString::number(row.nextAbsLoc));
            w.writeAttribute(QStringLiteral("track_id"), QString::number(row.trackId));
            w.writeAttribute(QStringLiteral("page_x"), row.pageX);
            w.writeAttribute(QStringLiteral("page_y"), row.pageY);
        }
        w.writeEndElement();
    };
    // The REV route is the same rows backwards, locations as written for the
    // route (tags_sim reverses its finished rows; it does not recompute them).
    const QVector<RouteRow> rows = routeRows(r.tags, r.dir);
    QVector<RouteRow> rev(rows.crbegin(), rows.crend());
    for (int i = 0; i < rev.size(); ++i) rev[i].nextAbsLoc = i + 1 < rev.size() ? rev.at(i + 1).absLoc : rev.at(i).absLoc;
    block(side + ends, r.dir, rows);
    block(QStringLiteral("REV_") + side + ends, r.dir == DirNominal ? DirReverse : DirNominal, rev);
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

bool writeFile(const QString &path, const QByteArray &data, QString *err)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (err) *err = f.errorString();
        return false;
    }
    return true;
}

}  // namespace RfidTag
