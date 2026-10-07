#include "lococonfigcore.h"

#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>

#include <cmath>
#include <cstring>

namespace LocoInfo {

// =============================================================================
//  Field / Layout
// =============================================================================

QString Field::typeText() const
{
    switch (kind) {
    case Kind::U8:    return QStringLiteral("u8");
    case Kind::U16:   return QStringLiteral("u16");
    case Kind::U32:   return QStringLiteral("u32");
    case Kind::Float: return QStringLiteral("f32");
    case Kind::Char:  return QStringLiteral("char[%1]").arg(size);
    }
    return QString();
}

bool Layout::load(const QString &schemaPath, QString *error)
{
    m_fields.clear();
    m_index.clear();
    m_bodySize = 0;
    m_loaded = false;

    auto fail = [error](const QString &why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };

    QFile file(schemaPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("Cannot open %1: %2").arg(schemaPath, file.errorString()));
    }
    QDomDocument document;
    QString parseError;
    int errorLine = 0;
    if (!document.setContent(&file, &parseError, &errorLine)) {
        return fail(QStringLiteral("%1 line %2: %3").arg(schemaPath).arg(errorLine).arg(parseError));
    }

    // Find <packet name="LINFO"> anywhere in the document.
    QDomElement packet;
    const QDomNodeList packets = document.elementsByTagName(QStringLiteral("packet"));
    for (int index = 0; index < packets.count(); ++index) {
        const QDomElement candidate = packets.at(index).toElement();
        if (candidate.attribute(QStringLiteral("name")) == QLatin1String("LINFO")) {
            packet = candidate;
        }
    }
    if (packet.isNull()) {
        return fail(QStringLiteral("The schema has no <packet name=\"LINFO\">"));
    }
    if (packet.attribute(QStringLiteral("wire")) != QLatin1String("lsb-first")) {
        // LOCO_INFO is a little-endian C struct. A schema that says otherwise
        // has changed meaning, and packing it as LE would be wrong silently.
        return fail(QStringLiteral("LINFO is not wire=\"lsb-first\"; the editor only packs a little-endian struct"));
    }

    QString section;
    int offset = 0;
    for (QDomElement child = packet.firstChildElement(); !child.isNull();
         child = child.nextSiblingElement()) {
        const QString tag = child.tagName();
        if (tag == QLatin1String("note")) {
            // "static_brake_params  (STATIC_BRAKING_CONFIG)" -> "static_brake_params"
            section = child.attribute(QStringLiteral("text")).section(QLatin1Char(' '), 0, 0).trimmed();
            if (section == QLatin1String(kTopLevelSection)) {
                section.clear();
            }
            continue;
        }
        if (tag != QLatin1String("field")) {
            return fail(QStringLiteral("LINFO uses <%1>, which the editor does not pack").arg(tag));
        }

        Field field;
        field.name = child.attribute(QStringLiteral("name"));
        field.section = section;
        if (section.isEmpty()) {
            field.key = field.name;
        } else {
            field.key = section + QLatin1Char('.') + field.name;
        }
        field.hex = child.hasAttribute(QStringLiteral("hexup"));
        field.group = child.attribute(QStringLiteral("ui-group"));
        field.note = child.attribute(QStringLiteral("ui-note"));
        field.format = child.attribute(QStringLiteral("ui-format"));
        field.cName = child.attribute(QStringLiteral("c-name"), field.name);
        field.offset = offset;

        const QString type = child.attribute(QStringLiteral("type"));
        if (type == QLatin1String("float")) {
            field.kind = Kind::Float;
            field.size = 4;
        } else if (type == QLatin1String("char")) {
            field.kind = Kind::Char;
            field.size = child.attribute(QStringLiteral("count")).toInt();
            if (field.size <= 1) {
                return fail(QStringLiteral("LINFO field %1: char count must be at least 2").arg(field.name));
            }
        } else if (type.isEmpty()) {
            const int bits = child.attribute(QStringLiteral("bits")).toInt();
            if (bits == 8) {
                field.kind = Kind::U8;
            } else if (bits == 16) {
                field.kind = Kind::U16;
            } else if (bits == 32) {
                field.kind = Kind::U32;
            } else {
                return fail(QStringLiteral("LINFO field %1: %2 bits is not a whole C integer")
                                .arg(field.name).arg(bits));
            }
            field.size = bits / 8;
            if (child.hasAttribute(QStringLiteral("signed"))) {
                return fail(QStringLiteral("LINFO field %1 is signed; the editor packs unsigned only")
                                .arg(field.name));
            }
        } else {
            return fail(QStringLiteral("LINFO field %1: type \"%2\" is not packed by the editor")
                            .arg(field.name, type));
        }

        if (field.name == QLatin1String(kCrcFieldName)) {
            field.isCrc = true;
        }
        if (m_index.contains(field.key)) {
            return fail(QStringLiteral("LINFO has two fields keyed %1").arg(field.key));
        }
        m_index.insert(field.key, m_fields.size());
        m_fields.append(field);
        offset += field.size;
    }

    // The CRC must be the last member and a u32: it covers everything before it.
    if (m_fields.isEmpty() || !m_fields.last().isCrc || m_fields.last().kind != Kind::U32) {
        return fail(QStringLiteral("LINFO must end with a 32-bit %1").arg(QLatin1String(kCrcFieldName)));
    }
    m_bodySize = offset;
    m_loaded = true;
    return true;
}

int Layout::indexOf(const QString &key) const
{
    return m_index.value(key, -1);
}

const Field *Layout::field(const QString &key) const
{
    const int index = indexOf(key);
    if (index < 0) {
        return nullptr;
    }
    return &m_fields[index];
}

const Field *Layout::crcField() const
{
    if (m_fields.isEmpty()) {
        return nullptr;
    }
    return &m_fields.last();
}

// =============================================================================
//  CRC
// =============================================================================

quint32 crc32(const QByteArray &data)
{
    // Reflected form of polynomial 0x04C11DB7 (0xEDB88320), remainder
    // starting at 0 and no final XOR: bit-for-bit what v12's
    // REFLECT_DATA / REFLECT_REMAINDER table version computes.
    quint32 remainder = 0;
    for (int index = 0; index < data.size(); ++index) {
        remainder ^= static_cast<quint8>(data.at(index));
        for (int bit = 0; bit < 8; ++bit) {
            if (remainder & 1u) {
                remainder = (remainder >> 1) ^ 0xEDB88320u;
            } else {
                remainder >>= 1;
            }
        }
    }
    return remainder;
}

QString crcText(quint32 crc)
{
    return QStringLiteral("0x") + QStringLiteral("%1").arg(crc, 8, 16, QLatin1Char('0')).toUpper();
}

// =============================================================================
//  Packing
// =============================================================================

namespace {

quint64 maxFor(Kind kind)
{
    switch (kind) {
    case Kind::U8:  return 0xFFull;
    case Kind::U16: return 0xFFFFull;
    case Kind::U32: return 0xFFFFFFFFull;
    default:        return 0;
    }
}

bool isInteger(Kind kind)
{
    return kind == Kind::U8 || kind == Kind::U16 || kind == Kind::U32;
}

// Why a value cannot be packed into a field, or empty.
QString problemWith(const Field &field, const QVariant &value)
{
    if (isInteger(field.kind)) {
        bool ok = false;
        const qint64 number = value.toLongLong(&ok);
        if (!ok) {
            return QStringLiteral("%1 is not a number").arg(field.key);
        }
        if (number < 0 || static_cast<quint64>(number) > maxFor(field.kind)) {
            return QStringLiteral("%1 = %2 is outside 0–%3 (%4)")
                .arg(field.key).arg(number).arg(maxFor(field.kind)).arg(field.typeText());
        }
        return QString();
    }
    if (field.kind == Kind::Float) {
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok || !std::isfinite(number) || std::fabs(number) > 3.4e38) {
            return QStringLiteral("%1 is not a finite 32-bit float").arg(field.key);
        }
        return QString();
    }
    // Char: ASCII, and room for the terminating NUL (v12 uses strcpy).
    const QString text = value.toString();
    for (const QChar character : text) {
        if (character.unicode() < 0x20 || character.unicode() > 0x7E) {
            return QStringLiteral("%1 has a character that is not printable ASCII").arg(field.key);
        }
    }
    if (text.size() > field.size - 1) {
        return QStringLiteral("%1 is %2 characters; at most %3 fit")
            .arg(field.key).arg(text.size()).arg(field.size - 1);
    }
    return QString();
}

}  // namespace

QByteArray fieldBytes(const Field &field, const QVariant &value)
{
    QByteArray bytes(field.size, '\0');
    if (isInteger(field.kind)) {
        const quint32 number = static_cast<quint32>(value.toLongLong());
        if (field.kind == Kind::U8) {
            bytes[0] = static_cast<char>(number & 0xFFu);
        } else if (field.kind == Kind::U16) {
            qToLittleEndian<quint16>(static_cast<quint16>(number), bytes.data());
        } else {
            qToLittleEndian<quint32>(number, bytes.data());
        }
    } else if (field.kind == Kind::Float) {
        const float number = static_cast<float>(value.toDouble());
        quint32 raw = 0;
        std::memcpy(&raw, &number, sizeof(raw));
        qToLittleEndian<quint32>(raw, bytes.data());
    } else {
        const QByteArray text = value.toString().toLatin1();
        const int length = qMin(text.size(), field.size - 1);
        std::memcpy(bytes.data(), text.constData(), static_cast<size_t>(length));
    }
    return bytes;
}

bool sameValue(const Field &field, const QVariant &a, const QVariant &b)
{
    return fieldBytes(field, a) == fieldBytes(field, b);
}

QByteArray encodeBody(const Layout &layout, const Values &values, QString *error)
{
    if (!layout.isLoaded()) {
        if (error != nullptr) {
            *error = QStringLiteral("The LOCO_INFO layout is not loaded");
        }
        return QByteArray();
    }
    QByteArray body(layout.bodySize(), '\0');
    for (const Field &field : layout.fields()) {
        if (field.isCrc) {
            continue;
        }
        const QVariant value = values.value(field.key, QVariant());
        if (!value.isValid()) {
            continue;   // zero / empty, as memset() leaves it in v12
        }
        const QString problem = problemWith(field, value);
        if (!problem.isEmpty()) {
            if (error != nullptr) {
                *error = problem;
            }
            return QByteArray();
        }
        const QByteArray bytes = fieldBytes(field, value);
        std::memcpy(body.data() + field.offset, bytes.constData(), static_cast<size_t>(bytes.size()));
    }
    const Field *crcField = layout.crcField();
    const quint32 crc = crc32(body.left(crcField->offset));
    qToLittleEndian<quint32>(crc, body.data() + crcField->offset);
    return body;
}

Parsed parseBody(const Layout &layout, const QByteArray &bytes)
{
    Parsed parsed;
    if (!layout.isLoaded()) {
        parsed.error = QStringLiteral("The LOCO_INFO layout is not loaded");
        return parsed;
    }
    QByteArray body = bytes;
    const int withHeader = layout.bodySize() + kHeaderBytes;
    if (body.size() == withHeader
        && static_cast<quint8>(body.at(0)) == kSourceId
        && static_cast<quint8>(body.at(1)) == kDestId
        && static_cast<quint8>(body.at(2)) == kMessageId) {
        body = body.mid(kHeaderBytes);   // the datagram form: drop the header
    }
    if (body.size() != layout.bodySize()) {
        // Most often a file from a different LOCO_INFO version: members were
        // added or removed since it was written. Say so, rather than guess.
        parsed.error = QStringLiteral("%1 bytes; this LOCO_INFO layout is %2 bytes (or %3 with its message "
                                      "header). It was probably written for a different LOCO_INFO version.")
                           .arg(bytes.size()).arg(layout.bodySize()).arg(withHeader);
        return parsed;
    }

    for (const Field &field : layout.fields()) {
        const char *at = body.constData() + field.offset;
        if (field.isCrc) {
            parsed.storedCrc = qFromLittleEndian<quint32>(at);
            continue;
        }
        if (field.kind == Kind::U8) {
            parsed.values.insert(field.key, static_cast<qint64>(static_cast<quint8>(*at)));
        } else if (field.kind == Kind::U16) {
            parsed.values.insert(field.key, static_cast<qint64>(qFromLittleEndian<quint16>(at)));
        } else if (field.kind == Kind::U32) {
            parsed.values.insert(field.key, static_cast<qint64>(qFromLittleEndian<quint32>(at)));
        } else if (field.kind == Kind::Float) {
            const quint32 raw = qFromLittleEndian<quint32>(at);
            float number = 0.0f;
            std::memcpy(&number, &raw, sizeof(number));
            parsed.values.insert(field.key, static_cast<double>(number));
        } else {
            // Up to the first NUL; anything after it is not part of the string.
            const QByteArray raw(at, field.size);
            int end = raw.indexOf('\0');
            if (end < 0) {
                end = raw.size();
            }
            parsed.values.insert(field.key, QString::fromLatin1(raw.left(end)));
        }
    }
    parsed.computedCrc = crc32(body.left(layout.crcField()->offset));
    parsed.crcOk = (parsed.computedCrc == parsed.storedCrc);
    parsed.ok = true;
    return parsed;
}

QByteArray message(const QByteArray &body)
{
    // STRUCT_MESSAGE_HEADER, packed: src, dest, message id, then the length
    // of the WHOLE message (header included), little-endian -- v12 sets
    // message_length = sizeof(LOCO_INFO).
    QByteArray datagram(kHeaderBytes, '\0');
    datagram[0] = static_cast<char>(kSourceId);
    datagram[1] = static_cast<char>(kDestId);
    datagram[2] = static_cast<char>(kMessageId);
    qToLittleEndian<quint16>(static_cast<quint16>(body.size() + kHeaderBytes), datagram.data() + 3);
    datagram.append(body);
    return datagram;
}

// =============================================================================
//  Text
// =============================================================================

QString formatValue(const Field &field, const QVariant &value, const QString &format)
{
    if (!value.isValid()) {
        return QString();
    }
    if (isInteger(field.kind)) {
        const quint32 number = static_cast<quint32>(value.toLongLong());
        if (format == QLatin1String("ipv4")) {
            // v12 stores these as the number whose big-endian bytes are the
            // dotted quad: 127.0.0.1 is 2130706433.
            return QHostAddress(number).toString();
        }
        if (field.hex) {
            return crcText(number);
        }
        return QString::number(number);
    }
    if (field.kind == Kind::Float) {
        // The shortest text that packs back to the same float: 4.2f shows
        // as "4.2", not "4.19999981".
        const float number = static_cast<float>(value.toDouble());
        for (int precision = 6; precision <= 9; ++precision) {
            const QString text = QString::number(static_cast<double>(number), 'g', precision);
            if (static_cast<float>(text.toDouble()) == number) {
                return text;
            }
        }
        return QString::number(static_cast<double>(number), 'g', 9);
    }
    return value.toString();
}

bool parseValueText(const Field &field, const QString &text, const QString &format,
                    QVariant *out, QString *error)
{
    const QString trimmed = text.trimmed();
    auto fail = [error](const QString &why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };

    QVariant value;
    if (isInteger(field.kind)) {
        if (format == QLatin1String("ipv4")) {
            QHostAddress address;
            if (!address.setAddress(trimmed) || address.protocol() != QAbstractSocket::IPv4Protocol) {
                return fail(QStringLiteral("Enter a dotted IPv4 address, e.g. 127.0.0.1"));
            }
            value = static_cast<qint64>(address.toIPv4Address());
        } else {
            bool ok = false;
            qulonglong number = 0;
            if (trimmed.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
                number = trimmed.mid(2).toULongLong(&ok, 16);
            } else {
                number = trimmed.toULongLong(&ok, 10);
            }
            if (!ok) {
                return fail(QStringLiteral("Enter a whole number (or 0x… hex)"));
            }
            // Anything past 32 bits is clamped to one past the u32 range, so
            // the range check below rejects it with the field's own limits.
            if (number > 0xFFFFFFFFull) {
                number = 0x100000000ull;
            }
            value = static_cast<qint64>(number);
        }
    } else if (field.kind == Kind::Float) {
        bool ok = false;
        const double number = trimmed.toDouble(&ok);
        if (!ok) {
            return fail(QStringLiteral("Enter a number, e.g. 4.2"));
        }
        value = number;
    } else {
        value = text;   // not trimmed: spaces in an APN are the operator's to decide
    }

    const QString problem = problemWith(field, value);
    if (!problem.isEmpty()) {
        return fail(problem);
    }
    *out = value;
    return true;
}

QStringList validate(const Layout &layout, const Values &values)
{
    QStringList problems;
    for (const Field &field : layout.fields()) {
        if (field.isCrc || !values.contains(field.key)) {
            continue;
        }
        const QString problem = problemWith(field, values.value(field.key));
        if (!problem.isEmpty()) {
            problems.append(problem);
        }
    }
    return problems;
}

QStringList changedKeys(const Layout &layout, const Values &before, const Values &after)
{
    QStringList changed;
    for (const Field &field : layout.fields()) {
        if (field.isCrc) {
            continue;
        }
        if (!sameValue(field, before.value(field.key), after.value(field.key))) {
            changed.append(field.key);
        }
    }
    return changed;
}

QStringList completeValues(const Layout &layout, const Values &defaults, Values *values)
{
    QStringList dropped;
    const QList<QString> keys = values->keys();
    for (const QString &key : keys) {
        const Field *field = layout.field(key);
        if (field == nullptr || field->isCrc) {
            values->remove(key);
            if (field == nullptr) {
                dropped.append(key);
            }
        }
    }
    for (const Field &field : layout.fields()) {
        if (field.isCrc || values->contains(field.key)) {
            continue;
        }
        if (defaults.contains(field.key)) {
            values->insert(field.key, defaults.value(field.key));
        } else if (field.kind == Kind::Char) {
            values->insert(field.key, QString());
        } else if (field.kind == Kind::Float) {
            values->insert(field.key, 0.0);
        } else {
            values->insert(field.key, static_cast<qint64>(0));
        }
    }
    dropped.sort();
    return dropped;
}

// =============================================================================
//  JSON
// =============================================================================

QJsonObject valuesToJson(const Layout &layout, const Values &values)
{
    QJsonObject object;
    for (const Field &field : layout.fields()) {
        if (field.isCrc || !values.contains(field.key)) {
            continue;
        }
        const QVariant value = values.value(field.key);
        if (field.kind == Kind::Char) {
            object.insert(field.key, value.toString());
        } else if (field.kind == Kind::Float) {
            // Stored as the float's own shortest text value, so the file
            // reads 4.2 rather than 4.199999809265137.
            object.insert(field.key, formatValue(field, value).toDouble());
        } else {
            object.insert(field.key, static_cast<double>(value.toLongLong()));
        }
    }
    return object;
}

Values valuesFromJson(const Layout &layout, const QJsonObject &object)
{
    Values values;
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        const Field *field = layout.field(iterator.key());
        if (field == nullptr) {
            values.insert(iterator.key(), iterator.value().toVariant());   // reported by completeValues
            continue;
        }
        if (field->kind == Kind::Char) {
            values.insert(field->key, iterator.value().toString());
        } else if (field->kind == Kind::Float) {
            values.insert(field->key, iterator.value().toDouble());
        } else {
            values.insert(field->key, static_cast<qint64>(std::llround(iterator.value().toDouble())));
        }
    }
    return values;
}

bool loadDefaults(const QString &path, const Layout &layout, Values *out, QString *error, QString *source)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot open %1").arg(path);
        }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("%1: %2").arg(path, parseError.errorString());
        }
        return false;
    }
    *out = valuesFromJson(layout, document.object().value(QStringLiteral("values")).toObject());
    if (source != nullptr) {
        *source = document.object().value(QStringLiteral("source")).toString();
    }
    return true;
}

Presentation Presentation::fromLayout(const Layout &layout)
{
    Presentation presentation;
    QHash<QString, int> groupIndex;
    for (const Field &field : layout.fields()) {
        if (field.isCrc) {
            continue;
        }
        if (!field.note.isEmpty()) {
            presentation.notes.insert(field.key, field.note);
        }
        if (!field.format.isEmpty()) {
            presentation.formats.insert(field.key, field.format);
        }
        if (field.group.isEmpty()) {
            continue;   // shown under "Other"
        }
        if (!groupIndex.contains(field.group)) {
            groupIndex.insert(field.group, presentation.groups.size());
            Group group;
            group.title = field.group;
            presentation.groups.append(group);
        }
        presentation.groups[groupIndex.value(field.group)].keys.append(field.key);
    }
    return presentation;
}

QString Presentation::groupOf(const QString &key) const
{
    for (const Group &group : groups) {
        if (group.keys.contains(key)) {
            return group.title;
        }
    }
    return QStringLiteral("Other");
}

// =============================================================================
//  ConfigStore
// =============================================================================

namespace {

QJsonObject configToJson(const Layout &layout, const LocoConfig &config)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), config.name);
    QJsonArray targets;
    for (const SendTarget &target : config.targets) {
        QJsonObject row;
        row.insert(QStringLiteral("ip"), target.ip.trimmed());
        row.insert(QStringLiteral("port"), static_cast<int>(target.port));
        row.insert(QStringLiteral("enabled"), target.enabled);
        targets.append(row);
    }
    object.insert(QStringLiteral("targets"), targets);
    // Row 1 also under the pre-bulk-send keys, so an older DLConsole still
    // reads a target from this file.
    if (!config.targets.isEmpty()) {
        object.insert(QStringLiteral("target_ip"), config.targets.first().ip.trimmed());
        object.insert(QStringLiteral("port"), static_cast<int>(config.targets.first().port));
    }
    object.insert(QStringLiteral("values"), valuesToJson(layout, config.values));
    if (!config.locked.isEmpty()) {
        object.insert(QStringLiteral("locked_fields"), QJsonArray::fromStringList(config.locked));
    }
    if (!config.lastSentBody.isEmpty()) {
        object.insert(QStringLiteral("last_sent_body_hex"), QString::fromLatin1(config.lastSentBody.toHex()));
        object.insert(QStringLiteral("last_sent_at"), config.lastSentAt.toUTC().toString(Qt::ISODateWithMs));
        object.insert(QStringLiteral("last_sent_target"), config.lastSentTarget);
    }
    return object;
}

// One configuration from its JSON object: targets (bulk list, or the single
// target of an older file), values completed from the defaults, and what was
// last sent. Shared by load() and importConfigs() (session 94).
LocoConfig configFromJson(const Layout &layout, const Values &defaults, const QJsonObject &object,
                          QSet<QString> *dropped)
{
    LocoConfig config;
    config.name = object.value(QStringLiteral("name")).toString().trimmed();
    // Targets: the bulk-send list if present, else the single target of
    // a file written before bulk send (it becomes row 1).
    const QJsonArray targetRows = object.value(QStringLiteral("targets")).toArray();
    if (!targetRows.isEmpty()) {
        for (int row = 0; row < kMaxTargets; ++row) {
            SendTarget target;
            target.ip.clear();
            if (row < targetRows.size()) {
                const QJsonObject rowObject = targetRows.at(row).toObject();
                target.ip = rowObject.value(QStringLiteral("ip")).toString().trimmed();
                const int port = rowObject.value(QStringLiteral("port")).toInt(kDefaultPort);
                if (port > 0 && port <= 65535) {
                    target.port = static_cast<quint16>(port);
                }
                target.enabled = rowObject.value(QStringLiteral("enabled")).toBool(true);
            }
            config.targets[row] = target;
        }
    } else {
        const QString ip = object.value(QStringLiteral("target_ip")).toString();
        if (!ip.isEmpty()) {
            config.targets[0].ip = ip;
        }
        const int port = object.value(QStringLiteral("port")).toInt(kDefaultPort);
        if (port > 0 && port <= 65535) {
            config.targets[0].port = static_cast<quint16>(port);
        }
    }
    config.values = valuesFromJson(layout, object.value(QStringLiteral("values")).toObject());
    for (const QString &key : completeValues(layout, defaults, &config.values)) {
        dropped->insert(key);
    }
    // Locks on fields this schema no longer has (or on the computed CRC)
    // are dropped, as their values are.
    for (const QJsonValue &key : object.value(QStringLiteral("locked_fields")).toArray()) {
        const Field *field = layout.field(key.toString());
        if (field != nullptr && !field->isCrc && !config.locked.contains(field->key)) {
            config.locked.append(field->key);
        }
    }
    config.locked.sort();
    config.lastSentBody = QByteArray::fromHex(object.value(QStringLiteral("last_sent_body_hex")).toString().toLatin1());
    config.lastSentAt = QDateTime::fromString(object.value(QStringLiteral("last_sent_at")).toString(), Qt::ISODateWithMs);
    config.lastSentTarget = object.value(QStringLiteral("last_sent_target")).toString();
    return config;
}

}  // namespace

Values keepLocked(const Layout &layout, const Values &incoming, const Values &current,
                  const QStringList &locked, QStringList *kept)
{
    Values out = incoming;
    for (const QString &key : locked) {
        const Field *field = layout.field(key);
        if (field == nullptr || !current.contains(key)) {
            continue;
        }
        if (kept != nullptr && !sameValue(*field, incoming.value(key), current.value(key))) {
            kept->append(key);
        }
        out.insert(key, current.value(key));
    }
    return out;
}

ConfigStore::ConfigStore(const QString &filePath, const Layout *layout, const Values &defaults)
    : m_filePath(filePath)
    , m_layout(layout)
    , m_defaults(defaults)
{
}

bool ConfigStore::load()
{
    m_configs.clear();
    m_droppedKeys.clear();
    m_lastError.clear();

    auto ensureOne = [this]() {
        if (m_configs.isEmpty()) {
            LocoConfig config;
            config.name = QStringLiteral("Loco (defaults)");
            config.values = m_defaults;
            completeValues(*m_layout, m_defaults, &config.values);
            m_configs.append(config);
        }
        if (!contains(m_activeName)) {
            m_activeName = m_configs.first().name;
        }
    };

    QFile file(m_filePath);
    if (!file.exists()) {
        ensureOne();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("Cannot open %1: %2").arg(m_filePath, file.errorString());
        ensureOne();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        m_lastError = QStringLiteral("%1 is not valid JSON: %2").arg(m_filePath, parseError.errorString());
        ensureOne();
        return false;
    }
    const QJsonObject root = document.object();
    QSet<QString> dropped;
    for (const QJsonValue &entry : root.value(QStringLiteral("configs")).toArray()) {
        const QJsonObject object = entry.toObject();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty() || contains(name)) {
            continue;
        }
        LocoConfig config = configFromJson(*m_layout, m_defaults, object, &dropped);
        m_configs.append(config);
    }
    m_activeName = root.value(QStringLiteral("active")).toString();
    m_droppedKeys = QStringList(dropped.begin(), dropped.end());
    m_droppedKeys.sort();
    ensureOne();
    return true;
}

bool ConfigStore::save() const
{
    QJsonArray configs;
    for (const LocoConfig &config : m_configs) {
        configs.append(configToJson(*m_layout, config));
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), 1);
    root.insert(QStringLiteral("active"), m_activeName);
    root.insert(QStringLiteral("configs"), configs);

    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = QStringLiteral("Cannot write %1: %2").arg(m_filePath, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        m_lastError = QStringLiteral("Cannot write %1: %2").arg(m_filePath, file.errorString());
        return false;
    }
    return true;
}

QStringList ConfigStore::names() const
{
    QStringList list;
    for (const LocoConfig &config : m_configs) {
        list.append(config.name);
    }
    return list;
}

bool ConfigStore::contains(const QString &name) const
{
    for (const LocoConfig &config : m_configs) {
        if (config.name == name) {
            return true;
        }
    }
    return false;
}

LocoConfig ConfigStore::config(const QString &name) const
{
    for (const LocoConfig &config : m_configs) {
        if (config.name == name) {
            return config;
        }
    }
    return m_configs.first();
}

void ConfigStore::upsert(const LocoConfig &config, const QString &previousName)
{
    QString lookFor = previousName;
    if (lookFor.isEmpty()) {
        lookFor = config.name;
    }
    for (LocoConfig &existing : m_configs) {
        if (existing.name == lookFor) {
            existing = config;
            if (m_activeName == lookFor) {
                m_activeName = config.name;
            }
            return;
        }
    }
    m_configs.append(config);
}

bool ConfigStore::insertAt(int index, const LocoConfig &config)
{
    if (config.name.isEmpty() || contains(config.name)) {
        return false;
    }
    m_configs.insert(qBound(0, index, m_configs.size()), config);
    return true;
}

bool ConfigStore::remove(const QString &name)
{
    if (m_configs.size() <= 1) {
        return false;
    }
    for (int index = 0; index < m_configs.size(); ++index) {
        if (m_configs[index].name == name) {
            m_configs.removeAt(index);
            if (m_activeName == name) {
                m_activeName = m_configs.first().name;
            }
            return true;
        }
    }
    return false;
}

QString ConfigStore::activeName() const
{
    return m_activeName;
}

void ConfigStore::setActiveName(const QString &name)
{
    if (contains(name)) {
        m_activeName = name;
    }
}

// =============================================================================
//  SendHistory
// =============================================================================

QJsonObject SendRecord::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("when"), when.toUTC().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("operator"), operatorName);
    object.insert(QStringLiteral("config"), configName);
    object.insert(QStringLiteral("target"), target);
    object.insert(QStringLiteral("crc"), crcText(crc));
    object.insert(QStringLiteral("vcc_crc"), crcText(vccCrc));
    object.insert(QStringLiteral("body_hex"), QString::fromLatin1(body.toHex()));
    return object;
}

SendRecord SendRecord::fromJson(const QJsonObject &object, bool *ok)
{
    SendRecord record;
    record.when = QDateTime::fromString(object.value(QStringLiteral("when")).toString(), Qt::ISODateWithMs);
    record.operatorName = object.value(QStringLiteral("operator")).toString();
    record.configName = object.value(QStringLiteral("config")).toString();
    record.target = object.value(QStringLiteral("target")).toString();
    bool crcOk = false;
    record.crc = object.value(QStringLiteral("crc")).toString().mid(2).toUInt(&crcOk, 16);
    bool vccOk = false;
    record.vccCrc = object.value(QStringLiteral("vcc_crc")).toString().mid(2).toUInt(&vccOk, 16);
    record.body = QByteArray::fromHex(object.value(QStringLiteral("body_hex")).toString().toLatin1());
    *ok = record.when.isValid() && crcOk && !record.body.isEmpty();
    return record;
}

bool SendHistory::append(const SendRecord &record)
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        m_lastError = QStringLiteral("Cannot append to %1: %2").arg(m_filePath, file.errorString());
        return false;
    }
    // One line per send; a line cut short by a power loss costs that line only.
    QByteArray line = QJsonDocument(record.toJson()).toJson(QJsonDocument::Compact);
    line.append('\n');
    if (file.write(line) != line.size() || !file.flush()) {
        m_lastError = QStringLiteral("Cannot append to %1: %2").arg(m_filePath, file.errorString());
        return false;
    }
    return true;
}

QJsonObject Verification::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("kind"), QStringLiteral("verification"));
    o.insert(QStringLiteral("sent"), sentAt.toUTC().toString(Qt::ISODateWithMs));
    o.insert(QStringLiteral("seen"), seenAt.toUTC().toString(Qt::ISODateWithMs));
    o.insert(QStringLiteral("loco"), locoKey);
    o.insert(QStringLiteral("match"), match);
    o.insert(QStringLiteral("crc_ok"), crcOk);
    o.insert(QStringLiteral("differences"), QJsonArray::fromStringList(differences));
    return o;
}

Verification Verification::fromJson(const QJsonObject &o, bool *ok)
{
    Verification v;
    v.sentAt = QDateTime::fromString(o.value(QStringLiteral("sent")).toString(), Qt::ISODateWithMs);
    v.seenAt = QDateTime::fromString(o.value(QStringLiteral("seen")).toString(), Qt::ISODateWithMs);
    v.locoKey = o.value(QStringLiteral("loco")).toString();
    v.match = o.value(QStringLiteral("match")).toBool();
    v.crcOk = o.value(QStringLiteral("crc_ok")).toBool(true);
    for (const QJsonValue &d : o.value(QStringLiteral("differences")).toArray()) v.differences << d.toString();
    if (ok) {
        *ok = o.value(QStringLiteral("kind")).toString() == QLatin1String("verification")
              && v.sentAt.isValid() && v.seenAt.isValid();
    }
    return v;
}

bool SendHistory::appendVerification(const Verification &verification)
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        m_lastError = QStringLiteral("Cannot append to %1: %2").arg(m_filePath, file.errorString());
        return false;
    }
    QByteArray line = QJsonDocument(verification.toJson()).toJson(QJsonDocument::Compact);
    line.append('\n');
    if (file.write(line) != line.size() || !file.flush()) {
        m_lastError = QStringLiteral("Cannot append to %1: %2").arg(m_filePath, file.errorString());
        return false;
    }
    return true;
}

QList<Verification> SendHistory::readVerifications() const
{
    QList<Verification> out;
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly)) return out;
    while (!file.atEnd()) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readLine().trimmed());
        if (!doc.isObject()) continue;
        bool ok = false;
        const Verification v = Verification::fromJson(doc.object(), &ok);
        if (ok) out << v;
    }
    return out;
}

QList<SendRecord> SendHistory::readAll(int *skipped) const
{
    QList<SendRecord> records;
    int bad = 0;
    QFile file(m_filePath);
    if (file.open(QIODevice::ReadOnly)) {
        while (!file.atEnd()) {
            const QByteArray line = file.readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }
            const QJsonDocument document = QJsonDocument::fromJson(line);
            bool ok = false;
            SendRecord record;
            if (document.isObject()
                && document.object().value(QStringLiteral("kind")).toString() == QLatin1String("verification")) {
                continue;   // not a send (session 81)
            }
            if (document.isObject()) {
                record = SendRecord::fromJson(document.object(), &ok);
            }
            if (ok) {
                records.append(record);
            } else {
                ++bad;
            }
        }
    }
    if (skipped != nullptr) {
        *skipped = bad;
    }
    return records;
}

QString targetProblem(const QVector<SendTarget> &targets)
{
    QStringList seen;
    int used = 0;
    for (int row = 0; row < targets.size(); ++row) {
        const SendTarget &target = targets.at(row);
        if (!target.isUsed()) {
            continue;
        }
        ++used;
        QHostAddress address;
        if (!address.setAddress(target.ip.trimmed()) || address.protocol() != QAbstractSocket::IPv4Protocol) {
            return QStringLiteral("Target %1: \"%2\" is not a valid IPv4 address").arg(row + 1).arg(target.ip.trimmed());
        }
        if (seen.contains(target.text())) {
            return QStringLiteral("Target %1: %2 is listed twice").arg(row + 1).arg(target.text());
        }
        seen.append(target.text());
    }
    if (used == 0) {
        return QStringLiteral("No target: tick at least one row and give it an IP address");
    }
    return QString();
}

QString currentOperatorName()
{
    QString name = qEnvironmentVariable("USERNAME");
    if (name.isEmpty()) {
        name = qEnvironmentVariable("USER");
    }
    if (name.isEmpty()) {
        name = QStringLiteral("unknown");
    }
    return name;
}

}  // namespace LocoInfo

namespace LocoInfo {

// =============================================================================
//  Session 94: export / import to another PC
// =============================================================================

const char *kLocoConfigsFormat = "dlconsole-loco-configs";

QByteArray exportConfigs(const Layout &layout, const QList<LocoConfig> &configs)
{
    QJsonArray array;
    for (LocoConfig c : configs) {
        c.lastSentBody.clear();                 // this PC's history, not the loco's
        c.lastSentAt = QDateTime();
        c.lastSentTarget.clear();
        array.append(configToJson(layout, c));
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QLatin1String(kLocoConfigsFormat));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("exported_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("configs"), array);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

ImportedConfigs importConfigs(const Layout &layout, const Values &defaults, const QByteArray &json)
{
    ImportedConfigs out;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject()) {
        out.error = QStringLiteral("not valid JSON: %1").arg(parseError.errorString());
        return out;
    }
    const QJsonObject root = document.object();
    // A flasher-profile file, or anything else, is refused by name rather
    // than half-read. loco_configs.json itself (no "format") is accepted:
    // copying that file across is the obvious thing to try.
    const QString format = root.value(QStringLiteral("format")).toString();
    if (!format.isEmpty() && format != QLatin1String(kLocoConfigsFormat)) {
        out.error = QStringLiteral("this is a \"%1\" file, not loco configurations").arg(format);
        return out;
    }
    if (root.value(QStringLiteral("version")).toInt(1) > 1) {
        out.error = QStringLiteral("written by a newer DLConsole (version %1)").arg(root.value(QStringLiteral("version")).toInt());
        return out;
    }
    QSet<QString> dropped, names;
    for (const QJsonValue &entry : root.value(QStringLiteral("configs")).toArray()) {
        const QJsonObject object = entry.toObject();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty() || names.contains(name)) continue;
        names.insert(name);
        LocoConfig c = configFromJson(layout, defaults, object, &dropped);
        c.lastSentBody.clear();
        c.lastSentAt = QDateTime();
        c.lastSentTarget.clear();
        out.configs.append(c);
    }
    if (out.configs.isEmpty()) {
        out.error = QStringLiteral("the file holds no configurations");
        return out;
    }
    out.droppedKeys = QStringList(dropped.begin(), dropped.end());
    out.droppedKeys.sort();
    out.ok = true;
    return out;
}

QString ConfigStore::importConfig(const LocoConfig &config, ImportClash clash)
{
    if (!contains(config.name)) {
        m_configs.append(config);
        return config.name;
    }
    switch (clash) {
    case ImportClash::Skip:
        return QString();
    case ImportClash::Replace:
        for (LocoConfig &c : m_configs) {
            if (c.name == config.name) { c = config; break; }
        }
        return config.name;
    case ImportClash::KeepBoth: {
        LocoConfig renamed = config;
        renamed.name = uniqueImportName(config.name, [this](const QString &n) { return contains(n); });
        m_configs.append(renamed);
        return renamed.name;
    }
    }
    return QString();
}

}  // namespace LocoInfo
