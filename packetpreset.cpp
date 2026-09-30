#include "packetpreset.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace {

// Numbers go through double on the way to JSON, so keep the conversion in one
// place rather than scattering casts that could quietly disagree.
inline double toJ(qint64 v)          { return double(v); }
inline qint64 fromJ(const QJsonValue &v, qint64 dflt = 0)
{
    return v.isDouble() ? qint64(v.toDouble()) : dflt;
}

}  // namespace

QJsonObject PacketPreset::toJson() const
{
    QJsonObject o;
    o["format"]      = QStringLiteral("dlconsole.packet");
    o["version"]     = 1;
    o["captype"]     = captype;
    o["dest"]        = dest;
    o["port"]        = port;
    o["interval_ms"] = intervalMs;
    o["msg_src"]     = msgSrc;
    o["msg_dest"]    = msgDest;
    o["msg_seq"]     = msgSeq;
    o["msg_extras"]  = MessageHeader::extrasToJson(extras);

    QJsonObject h;
    for (auto it = header.constBegin(); it != header.constEnd(); ++it) {
        h[it.key()] = toJ(it.value());
    }
    o["header"] = h;

    QJsonArray sa;
    for (const Schema::SubEntry &se : subs) {
        QJsonObject so;
        so["type"] = se.type;

        QJsonObject vals;
        for (auto vt = se.values.constBegin(); vt != se.values.constEnd(); ++vt) {
            vals[vt.key()] = toJ(vt.value());
        }
        so["values"] = vals;

        QJsonObject reps;
        for (auto rt = se.repeats.constBegin(); rt != se.repeats.constEnd(); ++rt) {
            QJsonArray rows;
            for (const QHash<QString, qint64> &row : rt.value()) {
                QJsonObject ro;
                for (auto ft = row.constBegin(); ft != row.constEnd(); ++ft) {
                    ro[ft.key()] = toJ(ft.value());
                }
                rows.append(ro);
            }
            reps[rt.key()] = rows;
        }
        so["repeats"] = reps;
        sa.append(so);
    }
    o["subs"] = sa;
    o["vary"] = PacketVary::toJson(vary);
    return o;
}

bool PacketPreset::fromJson(const QJsonObject &o, QString *err, QStringList *unknownFields)
{
    Q_UNUSED(unknownFields);   // filled by the caller, which knows the schema

    if (o.value("format").toString() != QLatin1String("dlconsole.packet")) {
        if (err) { *err = QStringLiteral("not a Packet Maker preset"); }
        return false;
    }
    captype = o.value("captype").toString();
    if (captype.isEmpty()) {
        if (err) { *err = QStringLiteral("preset names no packet type"); }
        return false;
    }

    dest       = o.value("dest").toString(dest);
    port       = o.value("port").toInt(port);
    intervalMs = o.value("interval_ms").toInt(intervalMs);
    msgSrc     = o.value("msg_src").toInt(msgSrc);
    msgDest    = o.value("msg_dest").toInt(msgDest);
    msgSeq     = o.value("msg_seq").toInt(msgSeq);
    extras     = MessageHeader::extrasFromJson(o.value("msg_extras").toArray());

    header.clear();
    const QJsonObject h = o.value("header").toObject();
    for (auto it = h.constBegin(); it != h.constEnd(); ++it) {
        header.insert(it.key(), fromJ(it.value()));
    }

    subs.clear();
    for (const QJsonValue &sv : o.value("subs").toArray()) {
        const QJsonObject so = sv.toObject();
        Schema::SubEntry se;
        se.type = so.value("type").toInt();

        const QJsonObject vals = so.value("values").toObject();
        for (auto vt = vals.constBegin(); vt != vals.constEnd(); ++vt) {
            se.values.insert(vt.key(), fromJ(vt.value()));
        }

        const QJsonObject reps = so.value("repeats").toObject();
        for (auto rt = reps.constBegin(); rt != reps.constEnd(); ++rt) {
            QVector<QHash<QString, qint64>> rows;
            for (const QJsonValue &rv : rt.value().toArray()) {
                const QJsonObject ro = rv.toObject();
                QHash<QString, qint64> row;
                for (auto ft = ro.constBegin(); ft != ro.constEnd(); ++ft) {
                    row.insert(ft.key(), fromJ(ft.value()));
                }
                rows.push_back(row);
            }
            se.repeats.insert(rt.key(), rows);
        }
        subs.push_back(se);
    }

    vary = PacketVary::fromJson(o.value("vary").toArray());
    return true;
}

bool PacketPreset::load(const QString &path, PacketPreset *out, QString *err,
                        QStringList *unknownFields)
{
    if (!out) { return false; }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) { *err = QStringLiteral("could not read %1").arg(path); }
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    f.close();

    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (err) { *err = QStringLiteral("%1 is not valid JSON: %2")
                              .arg(path, pe.errorString()); }
        return false;
    }
    return out->fromJson(doc.object(), err, unknownFields);
}

bool PacketPreset::save(const QString &path, QString *err) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) { *err = QStringLiteral("could not write %1").arg(path); }
        return false;
    }
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

QHash<QString, qint64> PacketPreset::headerFor(int sendIndex,
                                               const Schema::Encoder &enc) const
{
    QHash<QString, qint64> out = header;
    if (sendIndex < 0 || vary.isEmpty()) { return out; }

    const Schema::PacketInfo pi = enc.packet(captype);
    for (const PacketVary::Rule &r : vary) {
        if (!r.enabled || r.field.isEmpty() || !out.contains(r.field)) { continue; }
        int bits = 0;
        if (pi.ok) {
            for (const Schema::FieldInfo &f : pi.header) {
                if (!f.isPad && f.name == r.field) { bits = f.bits; break; }
            }
        }
        out.insert(r.field, r.valueFor(sendIndex, bits));
    }
    return out;
}

PacketBuilder::Result PacketPreset::build(PacketBuilder &builder, int sendIndex,
                                          const QByteArray &sessionKey) const
{
    return builder.build(captype, headerFor(sendIndex, builder.encoder()),
                         subs, sessionKey);
}
