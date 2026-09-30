// =====================================================================
//  schemadecoder.{h,cpp} -- data-driven packet decoder.
//
//  Decodes a frame purely from an XML schema (kavach.xml), so adding a
//  field or a whole struct is an edit to the XML, not to C++.  Output is
//  the same QVector<FieldRow> the rest of DLConsole uses, so it can feed
//  the existing inspector tree.
//
//  Build note: uses Qt's DOM parser -> add  QT += xml  to DLConsole.pro.
// =====================================================================
#ifndef SCHEMADECODER_H
#define SCHEMADECODER_H

#include <QString>
#include <QVector>
#include <QHash>
#include <QByteArray>
#include <QDomDocument>
#include <QDomElement>

#include "capturedecoder.h"   // FieldRow { QString field; QString value; }

namespace Schema {

class Decoder
{
public:
    Decoder() = default;

    // Load + index a schema file. Returns false (with *err set) on failure.
    bool load(const QString &xmlPath, QString *err = nullptr);
    bool isLoaded() const { return m_loaded; }

    // True if a <packet> exists whose match selects this frame.
    bool handles(const QByteArray &frame) const;

    // Decode a frame into Field|Value rows. Empty if no packet matches.
    QVector<FieldRow> decode(const QByteArray &frame) const;

private:
    // bit cursor over a byte buffer; msb=true -> bit 0 is MSB of byte 0.
    struct Cursor {
        const uchar *b; int pos; int limit; bool msb;
        quint32 take(int n);
    };

    struct Ctx { QHash<QString, qint64> ids; };   // field id -> last value

    const QDomElement *packetFor(const QByteArray &frame) const;
    void walk(QDomElement node, Cursor &c, Ctx &ctx,
              QVector<FieldRow> &rows, const QString &prefix) const;
    void walkStruct(const QString &name, Cursor &c, Ctx ctx,
                    QVector<FieldRow> &rows) const;
    QString enumLabel(const QString &enumName, qint64 v) const;
    static bool condOk(const QString &when, const Ctx &ctx);

    QDomDocument m_doc;
    QHash<QString, QDomElement> m_structs;   // name -> <struct>
    QHash<QString, QDomElement> m_enums;     // name -> <enum>
    QVector<QDomElement>        m_packets;   // <packet> list
    bool m_loaded = false;
};

} // namespace Schema

#endif // SCHEMADECODER_H
