#ifndef PACKETPRESET_H
#define PACKETPRESET_H

// =============================================================================
//  PacketPreset
//  -----------------------------------------------------------------------------
//  Everything the Packet Maker composed, as data: the packet type, the header
//  values, the sub-packets, the per-send variation rules, and where to send.
//
//  It exists as its own type rather than as save/load methods on the dialog
//  because two things now read this format — the dialog, and the sequence
//  runner, which has no widgets to load into. Two writers of one file format
//  is how a format drifts, so the definition lives here and both go through
//  it.
//
//  The SESSION KEY is deliberately not part of a preset. It is a shared
//  secret, and a preset is a file that gets mailed around, dropped in a
//  ticket, and committed. The key is supplied at build time by whoever is
//  doing the sending.
// =============================================================================

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "messageheader.h"
#include "packetbuilder.h"
#include "packetvariation.h"
#include "schema/schemaencoder.h"

struct PacketPreset {
    QString captype;
    QString dest       = QStringLiteral("127.0.0.1");
    int     port       = 0;
    int     intervalMs = 1000;

    // STRUCT_MESSAGE_HEADER envelope, for the packet types that carry one.
    int msgSrc  = 0;
    int msgDest = 0;
    int msgSeq  = 0;

    // Fields between the header and the packet (see MessageHeader::Extra).
    // Only enabled rows go on the wire. Absent in presets written before
    // session 64, which therefore load with none enabled — the same bytes
    // they always produced.
    QVector<MessageHeader::Extra> extras;

    QHash<QString, qint64>    header;
    QVector<Schema::SubEntry> subs;
    QVector<PacketVary::Rule> vary;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &o, QString *err = nullptr,
                  QStringList *unknownFields = nullptr);

    static bool load(const QString &path, PacketPreset *out, QString *err = nullptr,
                     QStringList *unknownFields = nullptr);
    bool save(const QString &path, QString *err = nullptr) const;

    // Header values with the variation rules for `sendIndex` applied.
    // sendIndex < 0 returns the base values untouched.
    QHash<QString, qint64> headerFor(int sendIndex, const Schema::Encoder &enc) const;

    // Build the frame for one send. Returns PacketBuilder's result verbatim,
    // self-verification included, so a sequence step is held to exactly the
    // same standard as pressing Build in the dialog.
    PacketBuilder::Result build(PacketBuilder &builder, int sendIndex,
                                const QByteArray &sessionKey) const;
};

#endif  // PACKETPRESET_H
