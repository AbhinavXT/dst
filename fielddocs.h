#ifndef FIELDDOCS_H
#define FIELDDOCS_H
// =============================================================================
//  FieldDocs -- what RDSO's annexures say a field is (session 301)
//  ---------------------------------------------------------------------------
//  schema/fielddocs.json holds, per field DLConsole shows, the description
//  from the annexure that defines it: Annexure C (Amdt-10) for the radio
//  packets (SLRP, LSRP, AAP, AEP, ARP) and Annexure D (Amdt-7) for the RFID
//  tags. The texts are the annexures' own (schema/build_fielddocs.py makes
//  the file); a 'note' is DLConsole's and is shown as such.
//
//  Windows look a field up by the packet's capture token and the name they
//  show it under: a schema field ("CUR_SIG_ASPECT"), a group ("tin"), or a
//  repeat row ("speed[3]"). A row's "[n]" index and " (…)" suffix are
//  ignored, and an RFID field can depend on the tag type (placement differs
//  on an LC tag).
//
//  Read-only reference data, like kavachSchema(): one process-wide instance,
//  loaded from the resource on first use.
// =============================================================================
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

struct FieldDocPart {
    QString field;      // the annexure's name: "SOURCE_STN_ILC_IBS_ID"
    int     bits = 0;
    QString text;       // the annexure's description, lines separated by '\n'
};

struct FieldDoc {
    QStringList packets;          // capture tokens: "slrp", "arp", "arprecv", "rfid"
    QStringList names;            // DLConsole's names for it
    QString     src;              // "C" or "D"
    QString     clause;           // "C.4.2", "D 3. LC Tag"
    int         page = 0;
    QVector<int> tagTypes;        // RFID only: the tag types it describes; empty = any
    QVector<FieldDocPart> parts;  // one, or several for a repeat row ("speed[n]": distance, class, value)
    QString     note;             // DLConsole's remark, never the annexure's text
};

class FieldDocs
{
public:
    static const FieldDocs &instance();   // :/schema/fielddocs.json

    bool loadFromJson(const QByteArray &json, QString *err = nullptr);

    // The description of `name` as `packet` shows it, or nullptr. `tagType`
    // picks among an RFID field's per-type descriptions (0 = unknown: the
    // Normal tag's).
    const FieldDoc *find(const QString &packet, const QString &name, int tagType = 0) const;

    // The hover text: the annexure's field name and size, its description,
    // DLConsole's note if any, and where it comes from. Empty when there is
    // no description.
    QString tooltip(const QString &packet, const QString &name, int tagType = 0) const;
    QString tooltipFor(const FieldDoc &doc) const;

    // "Annexure-C — Multiple Access Scheme & Radio Communication Protocol, C.4.2, page 12"
    QString reference(const FieldDoc &doc) const;
    QString sourceTitle(const QString &src) const;
    QString sourceDocument(const QString &src) const;   // "RDSO/SPN/196/2020 Version 4.0, Amdt-10, …"

    const QVector<FieldDoc> &docs() const { return m_docs; }
    int size() const { return m_docs.size(); }

    // "  speed[3]" -> "speed"; "Loco_Health (faults 0-5)" -> "Loco_Health";
    // "subpkt[2] StaticSpeedProfile @byte 25" -> "subpkt".
    static QString normalizeName(const QString &shown);
    // "arprecv" is an ARP; any case.
    static QString normalizePacket(const QString &packet);
    // Session 302: a decoded row that belongs to the capture line, not the
    // packet: describe() writes those unindented ("type", "rtc", "seq",
    // "frame len", "loco / ctrl"). An RFID frame has both the capture's
    // "type" and, indented, the tag's; only the tag's is Annexure D's.
    static bool isCaptureRow(const QString &shown);

private:
    QVector<FieldDoc>             m_docs;
    QHash<QString, QVector<int>>  m_index;   // "packet|name" -> docs
    QHash<QString, QString>       m_titles;
    QHash<QString, QString>       m_documents;
};

#endif  // FIELDDOCS_H
