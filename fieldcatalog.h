#ifndef FIELDCATALOG_H
#define FIELDCATALOG_H

// =============================================================================
//  FieldCatalog
//  -----------------------------------------------------------------------------
//  A configuration layer between the language a test case is written in and
//  the raw names the schema decoder produces.
//
//  WHY
//    Without it, a test case has to be written as `field:LOCO_MODE=2`. Three
//    things are wrong with that:
//
//      – The author must already know the decoder's spelling of the field.
//      – `2` is meaningless on the page. Whether it is Staff Responsible or
//        Limited Supervision is a fact stored only in kavach.xml, so a
//        mistyped digit produces a case that runs happily and checks the
//        wrong thing.
//      – Nothing says WHICH packet carries the field, so a name that exists
//        in two packets matches in both, and a query intended for one
//        message type silently evaluates against another.
//
//    The catalogue fixes all three: `loco_mode == staff_responsible` names
//    the field, the value and the packet in one readable term, and the
//    mapping lives in one editable file rather than being retyped into
//    every case.
//
//  IT IS ADDITIVE
//    An unknown name falls through unchanged, so `field:LOCO_MODE=2` keeps
//    working exactly as before. The catalogue is a convenience and a safety
//    net, never a gate: a field the catalogue has not been told about is
//    still queryable by its raw name.
//
//  FILE FORMAT — fieldmap.json
//    {
//      "packets": { "lsrp": "Loco → Stationary regular packet", ... },
//      "fields": [
//        { "name":   "loco_mode",
//          "packet": "lsrp",
//          "field":  "LOCO_MODE",
//          "desc":   "Operating mode reported by the loco",
//          "values": { "stand_by": 1, "staff_responsible": 2, ... } }
//      ]
//    }
// =============================================================================

#include <QHash>
#include <QString>
#include <QStringList>

class FieldCatalog
{
public:
    struct Entry {
        QString alias;     // the name a test case uses
        QString field;     // the decoder's name
        QString packet;    // captype it belongs to; empty = any
        QString desc;
        QHash<QString, qint64> values;   // symbolic value -> number
    };

    // Process-wide instance. Loaded once at start-up from fieldmap.json
    // beside the binary; absent file is not an error.
    static FieldCatalog &instance();

    bool load(const QString &path, QString *err = nullptr);
    bool loadFromJson(const QByteArray &json, QString *err = nullptr);
    void clear();

    bool isLoaded() const { return !m_byAlias.isEmpty(); }
    int  fieldCount() const { return m_byAlias.size(); }

    // Resolve an alias to its catalogue entry, or null if unknown.
    // Case-insensitive.
    const Entry *find(const QString &alias) const;

    // The decoder-level field name for an alias; returns `alias` unchanged
    // when the catalogue does not know it.
    QString resolveField(const QString &alias) const;

    // The packet an alias belongs to, or empty for "any".
    QString resolvePacket(const QString &alias) const;

    // Numeric value for a symbolic name within a field. `ok` is false when
    // the field or the symbol is unknown, which lets the caller fall back
    // to parsing the text as a number.
    qint64 resolveValue(const QString &alias, const QString &symbol,
                        bool *ok) const;

    // Reverse lookup, for display: the symbol for a numeric value.
    QString symbolFor(const QString &alias, qint64 value) const;

    QStringList aliases() const;
    // Aliases and their symbolic values, formatted for query completion.
    QStringList completionsFor(const QString &partial) const;

private:
    QHash<QString, Entry> m_byAlias;   // key is lower-cased alias
};

#endif // FIELDCATALOG_H
