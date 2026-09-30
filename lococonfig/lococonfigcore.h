#ifndef LOCOCONFIGCORE_H
#define LOCOCONFIGCORE_H
// =============================================================================
//  lococonfigcore.{h,cpp} -- LOCO_INFO: layout, packing, CRC, storage.
//
//  WHAT LOCO_INFO IS
//    The loco's configuration struct (loco_config_v12.cpp): a packed,
//    little-endian C struct of ~170 members, sent to the VCC's application
//    as one UDP datagram (STRUCT_MESSAGE_HEADER src 28 -> dest 2, message
//    120, length = whole struct) and also written as loco_info.bin (the body
//    without the header) for flashing at 0x60700000. The last member,
//    loco_info_crc, is a CRC over the body before it.
//
//  WHERE THE LAYOUT COMES FROM
//    Not from a second hand-written copy of the struct. DLConsole already
//    decodes this struct from captures as `@linfo`, described by
//    <packet name="LINFO"> in schema/kavach.xml. Layout reads THAT element:
//    field order, sizes and types. When LOCO_INFO gains a member, it is
//    added to kavach.xml once, and the decoder and this editor both follow.
//    Anything in that element this code does not understand makes load()
//    fail with the reason, rather than build a struct of the wrong shape.
//
//    Field keys: the schema's field name, prefixed by the section <note> it
//    follows ("static_brake_params.", "national_values."); LOCO_INFO's own
//    members, including those after the nested structs, have no prefix.
//    The prefix is needed because speed_margin_* appear both at the top
//    level and inside national_values.
//
//  WHAT THIS FILE DOES NOT DO
//    No widgets and no sockets: everything here is testable without a
//    window, and the golden test packs the defaults and compares them
//    byte-for-byte with the loco_info.bin the loco_config tool itself writes.
//
//  ADDING A MEMBER
//    One <field> line in kavach.xml, then lococonfig/sync_linfo.py against
//    the tool: see lococonfig/ADDING_A_FIELD.md. Nothing in this file or the
//    window names a field, except vcc_crc (shown in the send bar) and
//    loco_info_crc (computed).
// =============================================================================
#include "profileio.h"
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace LocoInfo {

// ---- the message envelope (loco_config_v12.cpp) -----------------------------
const quint8  kSourceId     = 28;
const quint8  kDestId       = 2;
const quint8  kMessageId    = 120;
const int     kHeaderBytes  = 5;           // STRUCT_MESSAGE_HEADER, packed
const quint16 kDefaultPort  = 50001;
const quint32 kFlashAddress = 0x60700000u; // where loco_info.bin is flashed

// The trailing CRC member, identified by name wherever its section note puts it.
const char kCrcFieldName[] = "loco_info_crc";

// The section note that means "back to LOCO_INFO's own members".
const char kTopLevelSection[] = "loco_info";

enum class Kind { U8, U16, U32, Float, Char };

struct Field {
    QString key;          // "national_values.speed_margin_eb"
    QString name;         // "speed_margin_eb"
    QString section;      // "national_values", or empty for LOCO_INFO's own
    Kind    kind = Kind::U8;
    int     offset = 0;   // byte offset in the body
    int     size = 0;     // bytes
    bool    hex = false;  // schema asks for hex display (hexup)
    bool    isCrc = false;

    // Editor-only attributes on the schema's <field> (the decoders ignore
    // them): ui-group, ui-note, ui-format. And the C member's name, for
    // sync_linfo.py: c-name, else the schema name.
    QString group;
    QString note;
    QString format;
    QString cName;

    QString typeText() const;   // "u8", "u16", "u32", "f32", "char[40]"
};

class Layout {
public:
    // Reads <packet name="LINFO"> from the schema file (normally the
    // compiled-in ":/schema/kavach.xml").
    bool load(const QString &schemaPath, QString *error);
    bool isLoaded() const { return m_loaded; }

    const QVector<Field> &fields() const { return m_fields; }
    int  bodySize() const { return m_bodySize; }
    int  indexOf(const QString &key) const;
    const Field *field(const QString &key) const;
    const Field *crcField() const;

private:
    QVector<Field>      m_fields;
    QHash<QString, int> m_index;
    int  m_bodySize = 0;
    bool m_loaded = false;
};

// Values by field key: qint64 for integers, double for floats, QString for
// char arrays. The CRC field is never stored; it is computed on encode.
using Values = QHash<QString, QVariant>;

// CRC-32, polynomial 0x04C11DB7, reflected, initial remainder 0, no final
// XOR -- crcFast() in loco_config_v12.cpp, and jamcrc(init 0) in
// capturedecoder.cpp.
quint32 crc32(const QByteArray &data);

// The body (bodySize() bytes), CRC filled in. Missing values encode as 0 /
// empty; invalid ones make it fail (check validate() first for the reasons).
QByteArray encodeBody(const Layout &layout, const Values &values, QString *error = nullptr);

struct Parsed {
    bool    ok = false;
    QString error;
    Values  values;
    quint32 storedCrc = 0;
    quint32 computedCrc = 0;
    bool    crcOk = false;
};
// Reads a body back. Accepts the 5-byte message header in front of it too
// (the datagram form) and strips it. A CRC mismatch is reported, not fatal:
// the caller decides whether to load a damaged or hand-edited file.
Parsed parseBody(const Layout &layout, const QByteArray &bytes);

// Header + body: the datagram v12 sends.
QByteArray message(const QByteArray &body);

// One field's bytes for a value; how two values are compared ("changed?"),
// so a float that prints differently but packs the same is not a change.
QByteArray fieldBytes(const Field &field, const QVariant &value);
bool sameValue(const Field &field, const QVariant &a, const QVariant &b);

// Display text, and parsing it back. `format` is from the presentation
// file ("ipv4") or empty. Integers accept decimal or 0x-hex.
QString formatValue(const Field &field, const QVariant &value, const QString &format = QString());
bool    parseValueText(const Field &field, const QString &text, const QString &format,
                       QVariant *out, QString *error);

// Everything that would stop a send: out-of-range integers, non-ASCII or
// over-long text, non-finite floats. Empty = sendable.
QStringList validate(const Layout &layout, const Values &values);

// The keys whose packed bytes differ between two value sets, in layout order.
QStringList changedKeys(const Layout &layout, const Values &before, const Values &after);

// Fill in any key the layout has and `values` lacks, from `defaults` (or
// zero), and drop keys the layout no longer has. Returns the dropped keys.
QStringList completeValues(const Layout &layout, const Values &defaults, Values *values);

// ---- presentation -------------------------------------------------------------
// Built from the schema's ui-* attributes, so a field added to kavach.xml
// needs nothing anywhere else to appear in the editor. Groups are listed in
// the order their first field appears in the struct.
struct Presentation {
    struct Group { QString title; QStringList keys; };
    QVector<Group>          groups;
    QHash<QString, QString> notes;     // key -> ui-note
    QHash<QString, QString> formats;   // key -> ui-format ("ipv4")

    static Presentation fromLayout(const Layout &layout);
    QString groupOf(const QString &key) const;   // "Other" when the field has no ui-group
};

// The defaults (lococonfig/loco_defaults.json), generated by sync_linfo.py
// from the loco_config tool's own values. A field the file lacks is 0/empty.
// `source`, when given, receives the tool's file name ("loco_config_v12.cpp").
bool loadDefaults(const QString &path, const Layout &layout, Values *out, QString *error,
                  QString *source = nullptr);

// Values <-> JSON (the store and the defaults file share this).
QJsonObject valuesToJson(const Layout &layout, const Values &values);
Values      valuesFromJson(const Layout &layout, const QJsonObject &object);

// ---- one saved configuration: one loco --------------------------------------

// Where Send goes. A configuration has up to kMaxTargets of these; Send goes
// to every enabled one with an address (bulk send). Row 1 is the old single
// target, so files written before bulk send load unchanged.
const int kMaxTargets = 4;
struct SendTarget {
    QString ip;
    quint16 port = kDefaultPort;
    bool    enabled = true;

    bool    isUsed() const { return enabled && !ip.trimmed().isEmpty(); }
    QString text() const { return QStringLiteral("%1:%2").arg(ip.trimmed()).arg(port); }
};

// Why these targets cannot be sent to, or empty: at least one used row,
// every used row a valid IPv4 address, no address:port twice.
QString targetProblem(const QVector<SendTarget> &targets);

struct LocoConfig {
    QString    name = QStringLiteral("Loco");
    Values     values;
    // Always kMaxTargets rows; rows 2-4 start empty.
    QVector<SendTarget> targets = defaultTargets();

    // What was last sent from this configuration, exactly as sent, so
    // "changed since last send" is judged against the bytes, not a memory.
    QByteArray lastSentBody;
    QDateTime  lastSentAt;
    QString    lastSentTarget;    // "ip:port" -- or several, comma-separated, after a bulk send

    static QVector<SendTarget> defaultTargets()
    {
        QVector<SendTarget> rows(kMaxTargets);
        rows[0].ip = QStringLiteral("192.168.25.168");
        return rows;
    }
};

// All configurations, in one JSON file beside dlconsole.ini
// (loco_configs.json), written atomically.
class ConfigStore {
public:
    ConfigStore(const QString &filePath, const Layout *layout, const Values &defaults);

    bool    load();                 // a missing file is fine: one default config
    bool    save() const;
    QString lastError() const { return m_lastError; }
    QString filePath() const { return m_filePath; }

    QStringList names() const;
    bool        contains(const QString &name) const;
    LocoConfig  config(const QString &name) const;
    void        upsert(const LocoConfig &config, const QString &previousName = QString());
    bool        remove(const QString &name);    // never the last one
    // Put a removed configuration back at its old place (undo, session 79).
    // Refused (false) if the name is taken.
    bool        insertAt(int index, const LocoConfig &config);
    QString     activeName() const;
    void        setActiveName(const QString &name);

    // Keys dropped on load because the schema no longer has them.
    QStringList droppedKeys() const { return m_droppedKeys; }

    // Session 94: every configuration, in order (for export).
    QList<LocoConfig> all() const { return m_configs; }
    // Add an imported configuration, resolving a name already in use by
    // `clash`. Returns the name it was stored under, or empty when skipped.
    QString importConfig(const LocoConfig &config, ImportClash clash);

private:
    QString            m_filePath;
    const Layout      *m_layout = nullptr;
    Values             m_defaults;
    QList<LocoConfig>  m_configs;
    QString            m_activeName;
    mutable QString    m_lastError;
    QStringList        m_droppedKeys;
};

// ---- export / import to another PC (session 94) ------------------------------
// A .dlloco file: {"format":"dlconsole-loco-configs","version":1,
// "configs":[...]} -- the same per-configuration JSON as loco_configs.json,
// WITHOUT what was last sent (that is this PC's history, not the loco's).
extern const char *kLocoConfigsFormat;
QByteArray exportConfigs(const Layout &layout, const QList<LocoConfig> &configs);

struct ImportedConfigs {
    bool              ok = false;
    QString           error;          // why the file was refused
    QList<LocoConfig> configs;
    QStringList       droppedKeys;    // fields the file had that this schema does not
};
// Values are completed from `defaults` for fields the file lacks, exactly as
// loading loco_configs.json does.
ImportedConfigs importConfigs(const Layout &layout, const Values &defaults, const QByteArray &json);

// ---- the send log (loco_config_history.jsonl, append-only) ------------------
struct SendRecord {
    QDateTime  when;              // UTC
    QString    operatorName;
    QString    configName;
    QString    target;            // "192.168.25.168:50001"
    quint32    crc = 0;
    quint32    vccCrc = 0;
    QByteArray body;              // exactly the body that went out

    QJsonObject toJson() const;
    static SendRecord fromJson(const QJsonObject &object, bool *ok);
};

// What the loco said about a send (session 81): the first @linfo from the
// loco after it, compared field by field. Appended to the same log as the
// sends (a line with "kind":"verification"), so the record of a send and
// the record of its confirmation are kept, and read, together.
struct Verification {
    QDateTime   sentAt;           // UTC, = SendRecord::when of the send it is about
    QDateTime   seenAt;           // UTC, when the @linfo arrived
    QString     locoKey;          // capture key, "7_1"
    bool        match = false;
    bool        crcOk = true;     // the loco's own loco_info_crc over its contents
    QStringList differences;      // "field: expected X, loco has Y"

    QJsonObject toJson() const;
    static Verification fromJson(const QJsonObject &object, bool *ok);
};

class SendHistory {
public:
    explicit SendHistory(const QString &filePath) : m_filePath(filePath) {}
    bool append(const SendRecord &record);
    bool appendVerification(const Verification &verification);
    // Sends only; verification lines are not sends and are not "skipped".
    QList<SendRecord> readAll(int *skipped = nullptr) const;
    QList<Verification> readVerifications() const;
    QString filePath() const { return m_filePath; }
    QString lastError() const { return m_lastError; }
private:
    QString m_filePath;
    QString m_lastError;
};

QString currentOperatorName();
QString crcText(quint32 crc);   // "0x28E5F9E4"

}  // namespace LocoInfo

#endif  // LOCOCONFIGCORE_H
