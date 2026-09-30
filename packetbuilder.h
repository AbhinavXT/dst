#ifndef PACKETBUILDER_H
#define PACKETBUILDER_H
// =====================================================================
//  packetbuilder.{h,cpp} -- turn field values into a finished, verified
//  wire frame.
//
//  Wraps Schema::Encoder with the per-regime assembly the encoder stays
//  out of: it sets PKT_LENGTH, appends the CBC-MAC (KavachMac) and the
//  JAMCRC, then RE-DECODES the frame it just built and refuses to hand
//  back anything that doesn't round-trip and whose CRC doesn't verify.
//  Nothing leaves this class unverified — that is the whole point of
//  building on the host instead of on the target.
//
//  Regime handled now: msb-first + anxc_jamcrc, with the SLRP-style
//  sub-packet tail [MAC_CODE 4B][CRC 4B] when reserve_tail>=8. Packets
//  the Schema::Encoder can't emit (groups/repeat) are reported, not
//  guessed.
//
//  UNVERIFIED against the target, and flagged in the result so the UI can
//  surface it: (1) PKT_LENGTH is set to the whole frame's byte count —
//  confirm the field's unit/coverage against firmware; (2) the MAC covers
//  body-before-MAC and the CRC covers body+MAC, matching Check_MAC's
//  effective-length arithmetic, but the exact CRC width firmware subtracts
//  should be confirmed; (3) the UDP transport envelope (if any) is added by
//  the sender, not here.
// =====================================================================
#include <QString>
#include <QByteArray>
#include <QHash>
#include <QVector>

#include "schema/schemaencoder.h"
#include "capturedecoder.h"     // FieldRow

class PacketBuilder {
public:
    // Which kavach.xml this builder encodes against: an external file when
    // Settings names a loadable one, otherwise empty for the built-in copy.
    // Worth showing next to a built frame — "which schema produced this" is
    // the first question when a frame is not what was expected.
    QString schemaPath() const { return m_schemaPath; }

    struct Result {
        bool       ok = false;
        QString    error;
        QByteArray frame;                 // full wire frame (no UDP envelope)
        int        pktLength = 0;         // value written into PKT_LENGTH
        bool       hasMac = false;
        bool       macKeyed = false;      // false => MAC is a zero placeholder
        QString    macHex;                // wire MAC (4 B), if any
        QString    crcHex;                // appended CRC (4 B), if any
        bool       crcVerified = false;   // recomputed CRC matched
        bool       roundTripped = false;  // decoder reproduced the frame
        QVector<FieldRow> decoded;        // what the decoder read back
        QStringList notes;                // non-fatal caveats for the UI
    };

    PacketBuilder();
    bool ready() const { return m_enc.isLoaded(); }

    // captype e.g. "slrp". `sessionKey16` may be empty (MAC becomes a zero
    // placeholder and macKeyed=false). PKT_LENGTH in `header` is ignored and
    // overwritten with the computed total.
    Result build(const QString &captype,
                 QHash<QString, qint64> header,
                 const QVector<Schema::SubEntry> &subs,
                 const QByteArray &sessionKey16 = {}) const;

    const Schema::Encoder &encoder() const { return m_enc; }

private:
    Schema::Encoder m_enc;
    QString m_schemaPath;   // "" = the copy built into the binary
    QString m_schemaNote;   // why the external one was not used, if so
};

#endif  // PACKETBUILDER_H
