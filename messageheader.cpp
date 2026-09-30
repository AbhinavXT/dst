#include "messageheader.h"

#include <QJsonObject>

namespace MessageHeader {

int messageId(const QString &captype)
{
    const QString t = captype.toLower();
    if (t == "slrp") { return 9;  }
    if (t == "lsrp") { return 10; }
    if (t == "aap")  { return 11; }
    if (t == "aep")  { return 12; }
    if (t == "arp")  { return 13; }
    // Same packet, same message id — what differs between the two forms is the
    // station id on the end of the header, not the id of the message.
    if (t == "arprecv") { return 13; }
    return -1;
}

QString shapeNote(const QString &captype)
{
    const QString t = captype.toLower();
    if (t == "arp" || t == "arprecv" || t == "lsrp") {
        return QStringLiteral(
            "this is the RECEIVED form: 8-byte header, no station id. A loco's "
            "own transmission carries a 2-byte station id and a 10-byte header, "
            "which DLConsole does not send.");
    }
    return QString();
}

bool applies(const QString &captype) { return messageId(captype) >= 0; }

QByteArray build(const QString &captype, int packetLen, quint16 seqNum,
                 quint8 srcId, quint8 destId)
{
    const int mid = messageId(captype);
    if (mid < 0) { return QByteArray(); }

    const quint16 msgLen = quint16(packetLen + SIZE);

    QByteArray h;
    h.reserve(SIZE);
    h.append(char(srcId));
    h.append(char(destId));
    // uint16 fields, little-endian (low byte first)
    auto put16 = [&h](quint16 v) {
        h.append(char(v & 0xFF));
        h.append(char((v >> 8) & 0xFF));
    };
    put16(quint16(mid));
    put16(msgLen);
    put16(seqNum);
    return h;
}

// ---- extra header fields -------------------------------------------------

namespace {

bool widthOk(int bytes) { return bytes == 1 || bytes == 2 || bytes == 4; }

qint64 maxFor(int bytes) { return (qint64(1) << (8 * bytes)) - 1; }

}  // namespace

QVector<Extra> defaultExtras()
{
    Extra st; st.name = QStringLiteral("station_id"); st.bytes = 2;
    Extra lo; lo.name = QStringLiteral("loco_id");    lo.bytes = 4;
    return { st, lo };
}

bool parseExtraValue(const QString &text, int bytes, qint64 *out, QString *err)
{
    auto fail = [err](const QString &m) { if (err) { *err = m; } return false; };
    if (!widthOk(bytes)) { return fail(QStringLiteral("width must be 1, 2 or 4 bytes")); }

    const QString t = text.trimmed();
    if (t.isEmpty()) { return fail(QStringLiteral("no value")); }

    bool ok = false;
    qulonglong v = 0;
    if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        v = t.mid(2).toULongLong(&ok, 16);
    } else {
        // toULongLong accepts a leading '-' on some Qt versions and wraps it;
        // refuse it outright so the answer does not depend on the Qt build.
        if (t.startsWith(QLatin1Char('-'))) {
            return fail(QStringLiteral("'%1' is negative; extra fields are unsigned").arg(t));
        }
        v = t.toULongLong(&ok, 10);
    }
    if (!ok) { return fail(QStringLiteral("'%1' is not a number (decimal, or 0x-prefixed hex)").arg(t)); }
    if (v > qulonglong(maxFor(bytes))) {
        return fail(QStringLiteral("%1 does not fit in uint%2 (max %3)")
                        .arg(t).arg(bytes * 8).arg(maxFor(bytes)));
    }
    if (out) { *out = qint64(v); }
    return true;
}

bool validateExtras(const QVector<Extra> &extras, QString *err)
{
    for (int i = 0; i < extras.size(); ++i) {
        const Extra &e = extras.at(i);
        if (!e.enabled) { continue; }
        const QString who = e.name.trimmed().isEmpty()
            ? QStringLiteral("extra field %1").arg(i + 1)
            : QStringLiteral("extra field '%1'").arg(e.name.trimmed());
        if (!widthOk(e.bytes)) {
            if (err) { *err = QStringLiteral("%1: width must be 1, 2 or 4 bytes").arg(who); }
            return false;
        }
        if (e.value < 0 || e.value > maxFor(e.bytes)) {
            if (err) { *err = QStringLiteral("%1: %2 does not fit in uint%3")
                                  .arg(who).arg(e.value).arg(e.bytes * 8); }
            return false;
        }
    }
    return true;
}

QByteArray encodeExtras(const QVector<Extra> &extras)
{
    if (!validateExtras(extras)) { return QByteArray(); }
    QByteArray out;
    for (const Extra &e : extras) {
        if (!e.enabled) { continue; }
        const quint64 v = quint64(e.value);
        for (int i = 0; i < e.bytes; ++i) {
            const int shift = e.bigEndian ? 8 * (e.bytes - 1 - i) : 8 * i;
            out.append(char((v >> shift) & 0xFF));
        }
    }
    return out;
}

int extrasSize(const QVector<Extra> &extras)
{
    int n = 0;
    for (const Extra &e : extras) { if (e.enabled) { n += e.bytes; } }
    return n;
}

QByteArray buildWithExtras(const QString &captype, int packetLen, quint16 seqNum,
                           quint8 srcId, quint8 destId, const QVector<Extra> &extras)
{
    if (!applies(captype) || !validateExtras(extras)) { return QByteArray(); }
    const QByteArray ex = encodeExtras(extras);
    // message_length counts the extras: build() adds SIZE, we add the rest.
    return build(captype, packetLen + ex.size(), seqNum, srcId, destId) + ex;
}

QString describeExtras(const QVector<Extra> &extras)
{
    QStringList parts;
    for (const Extra &e : extras) {
        if (!e.enabled) { continue; }
        parts << QStringLiteral("%1=%2 (0x%3, u%4 %5)")
                     .arg(e.name.trimmed().isEmpty() ? QStringLiteral("?") : e.name.trimmed())
                     .arg(e.value)
                     .arg(e.value, e.bytes * 2, 16, QLatin1Char('0'))
                     .arg(e.bytes * 8)
                     .arg(e.bigEndian ? QStringLiteral("BE") : QStringLiteral("LE"));
    }
    return parts.join(QStringLiteral("  "));
}

QJsonArray extrasToJson(const QVector<Extra> &extras)
{
    QJsonArray a;
    for (const Extra &e : extras) {
        QJsonObject o;
        o["name"]    = e.name;
        o["bytes"]   = e.bytes;
        o["value"]   = double(e.value);
        o["endian"]  = e.bigEndian ? QStringLiteral("be") : QStringLiteral("le");
        o["enabled"] = e.enabled;
        a.append(o);
    }
    return a;
}

QVector<Extra> extrasFromJson(const QJsonArray &a)
{
    QVector<Extra> out;
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        Extra e;
        e.name      = o.value("name").toString();
        e.bytes     = o.value("bytes").toInt(2);
        e.value     = qint64(o.value("value").toDouble(0));
        e.bigEndian = o.value("endian").toString() == QLatin1String("be");
        e.enabled   = o.value("enabled").toBool(false);
        out << e;
    }
    return out;
}

}  // namespace MessageHeader
