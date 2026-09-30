#ifndef MESSAGEHEADER_H
#define MESSAGEHEADER_H
// =====================================================================
//  messageheader.{h,cpp} -- the STRUCT_MESSAGE_HEADER that wraps a
//  radio packet on the wire.
//
//      struct PACK {
//          uint8_t  src_id;          // 7  (LCU/host)
//          uint8_t  dest_id;         // 2  (loco)
//          uint16_t message_id;      // per packet type (see below)
//          uint16_t message_length;  // packet bytes + sizeof(header)
//          uint16_t seq_num;         // increasing per datagram
//      };                            // 8 bytes, serialised LITTLE-ENDIAN
//
//  message_id: slrp=9, lsrp=10, aap=11, aep=12, arp=13.
//
//  The header is a transport envelope only: it is prepended to the built
//  frame at send time and is NOT covered by the packet's CBC-MAC or CRC
//  (those are computed over the packet body alone, which PacketBuilder
//  already does). The receiver strips it before decoding — which is why a
//  captured @slrp line carries no header.
// =====================================================================
#include <QByteArray>
#include <QJsonArray>
#include <QString>
#include <QVector>

namespace MessageHeader {

constexpr int SIZE   = 8;
constexpr quint8 kSrcId  = 7;   // default host id
constexpr quint8 kDestId = 2;   // default loco id

// One line about the shape of the header for this captype, or empty when
// there is nothing worth saying.
//
// It exists because the Packet Maker offers both `arp` and `arprecv` and
// builds byte-identical datagrams for them, which looks like a bug and is
// not: DLConsole is always the peer TRANSMITTING to the loco, so what it
// sends is by definition what the loco receives, and that form has an 8-byte
// header. The 10-byte form with the station id on the end is what a loco's
// own radio emits, and this program does not emit it. Better said once in
// the preview than rediscovered by an operator comparing two hex dumps.
QString shapeNote(const QString &captype);

// message_id for a captype, or -1 if that type is not sent with a header.
int  messageId(const QString &captype);
bool applies(const QString &captype);

// Build the 8-byte little-endian header. `packetLen` is the built frame's
// length (body + MAC + CRC); message_length is packetLen + SIZE.
QByteArray build(const QString &captype, int packetLen, quint16 seqNum,
                 quint8 srcId = kSrcId, quint8 destId = kDestId);

// ---- extra header fields -------------------------------------------------
//
// Operator-defined fields placed AFTER the 8-byte header and BEFORE the
// packet — e.g. a uint16 station_id, which is what turns the 8-byte form into
// the 10-byte one a loco's own radio emits, or a uint32 loco_id.
//
//      [ 8-byte header ][ extra 0 ][ extra 1 ] ... [ packet ]
//
// Like the header they are transport envelope only: not covered by the
// packet's CRC or MAC. They ARE counted in message_length, because that is
// how the header describes its own size (message_length - PKT_LENGTH); a
// receiver that is told the header is 8 bytes when it is 10 would read the
// station id as the first two bytes of the packet.
//
// Only rows with `enabled` set go on the wire; the rest are kept so a field
// can be switched off and on without retyping it.
struct Extra {
    QString name;
    int     bytes     = 2;       // 1, 2 or 4 (uint8 / uint16 / uint32)
    qint64  value     = 0;       // unsigned; must fit in `bytes`
    bool    bigEndian = false;   // default matches the header: little-endian
    bool    enabled   = false;

    bool operator==(const Extra &o) const {
        return name == o.name && bytes == o.bytes && value == o.value
            && bigEndian == o.bigEndian && enabled == o.enabled;
    }
};

// The shipped starting rows, both unticked: station_id (uint16) and
// loco_id (uint32), value 0.
QVector<Extra> defaultExtras();

// Parse an operator-typed value: decimal, or hex with a 0x prefix. Unsigned
// only. Refuses anything that does not fit in `bytes` rather than truncating
// it — a station id of 70000 in a uint16 is a typo, not 4464.
bool parseExtraValue(const QString &text, int bytes, qint64 *out, QString *err = nullptr);

// Every ENABLED row must have a supported width and a value that fits.
// Disabled rows are not checked: they do not go out.
bool validateExtras(const QVector<Extra> &extras, QString *err = nullptr);

// The bytes of the enabled rows, in order. Assumes validateExtras() passed;
// returns empty if it would not.
QByteArray encodeExtras(const QVector<Extra> &extras);
int        extrasSize(const QVector<Extra> &extras);   // enabled rows only

// Header + enabled extras, with message_length = packetLen + SIZE + extras.
// Empty if the captype carries no header or the extras are invalid.
QByteArray buildWithExtras(const QString &captype, int packetLen, quint16 seqNum,
                           quint8 srcId, quint8 destId,
                           const QVector<Extra> &extras);

// "station_id=0x0102(u16 LE)  loco_id=…" — enabled rows only, for previews.
QString describeExtras(const QVector<Extra> &extras);

QJsonArray     extrasToJson(const QVector<Extra> &extras);
QVector<Extra> extrasFromJson(const QJsonArray &a);

}  // namespace MessageHeader

#endif  // MESSAGEHEADER_H
