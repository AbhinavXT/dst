#include "biuprotocol.h"

#include <QCoreApplication>
#include <cmath>

namespace Biu {

namespace {
QString tr(const char *s) { return QCoreApplication::translate("Biu", s); }
QString hex16(quint16 v) { return QStringLiteral("0x") + QString::number(v, 16).toUpper().rightJustified(4, QLatin1Char('0')); }
QString hex8(quint8 v)   { return QStringLiteral("0x") + QString::number(v, 16).toUpper().rightJustified(2, QLatin1Char('0')); }
quint16 le16(const QByteArray &d, int i) { return quint16(quint8(d.at(i)) | (quint8(d.at(i + 1)) << 8)); }
void put16(QByteArray &d, int i, quint16 v) { d[i] = char(v & 0xFF); d[i + 1] = char(v >> 8); }

// The names of a byte's set bits, from `names`.
QString setBits(quint8 v, const QStringList &names)
{
    QStringList on;
    for (int b = 0; b < names.size(); ++b)
        if ((v >> b) & 1) on << names.at(b);
    return on.isEmpty() ? tr("none") : on.join(QStringLiteral(", "));
}
}  // namespace

QString nmtStateName(int state)
{
    switch (state) {
    case BootUp:         return tr("Boot-up");
    case Stopped:        return tr("Stopped");
    case Operational:    return tr("Operational");
    case PreOperational: return tr("Pre-operational");
    default:             return hex8(quint8(state));
    }
}

QString nmtCommandName(int cs)
{
    switch (cs) {
    case NmtStart:     return tr("Start remote node");
    case NmtStop:      return tr("Stop remote node");
    case NmtPreOp:     return tr("Enter pre-operational");
    case NmtResetNode: return tr("Reset node");
    case NmtResetComm: return tr("Reset communication");
    default:           return hex8(quint8(cs));
    }
}

QByteArray toDatagram(const Frame &f)
{
    const QByteArray data = f.data.left(kDataLen);
    QByteArray d(3, '\0');
    d[0] = char(f.cob & 0xFF);
    d[1] = char(f.cob >> 8);
    d[2] = char(3 + data.size());
    return d + data;
}

bool fromDatagram(const QByteArray &datagram, Frame *out, QString *why)
{
    auto fail = [why](const QString &w) { if (why) *why = w; return false; };
    if (datagram.size() < 3) return fail(tr("%1 bytes: shorter than the 3-byte gateway header").arg(datagram.size()));
    const int total = quint8(datagram.at(2));
    if (total < 3 || total > 3 + kDataLen)
        return fail(tr("length byte %1: a frame is 3 to 11 bytes").arg(total));
    if (datagram.size() < total)
        return fail(tr("length byte %1, but %2 bytes came").arg(total).arg(datagram.size()));
    out->cob = le16(datagram, 0);
    out->data = datagram.mid(3, total - 3);
    if (why) why->clear();
    return true;
}

quint8 kgToByte(double kgcm2)
{
    if (!(kgcm2 > 0)) return 0;
    const double raw = std::floor(kgcm2 / kKgPerBit + 0.5);
    return raw >= 255 ? 255 : quint8(raw);
}

double byteToKg(quint8 raw) { return raw * kKgPerBit; }

// ---- command ----------------------------------------------------------------------

bool decodeCommand(const QByteArray &data, Command *out)
{
    if (data.size() < kDataLen) return false;
    out->speedRaw = le16(data, 0);
    out->d1 = quint8(data.at(2));
    out->d2 = quint8(data.at(3));
    out->a9Raw = quint8(data.at(4));
    out->sa9Raw = quint8(data.at(5));
    out->spare6 = quint8(data.at(6));
    out->spare7 = quint8(data.at(7));
    return true;
}

QByteArray encodeCommand(const Command &c)
{
    QByteArray d(kDataLen, '\0');
    put16(d, 0, c.speedRaw);
    d[2] = char(c.d1);
    d[3] = char(c.d2);
    d[4] = char(c.a9Raw);
    d[5] = char(c.sa9Raw);
    d[6] = char(c.spare6);
    d[7] = char(c.spare7);
    return d;
}

QStringList commandBitNames(int byte)
{
    if (byte == 1)
        return { tr("Brake test request"), tr("KAVACH healthy"), tr("BP charging cut-out"), tr("BIU isolate"),
                 tr("Speed valid"), tr("Light engine"), tr("Remote loco"), tr("Dynamic braking") };
    return { tr("Fault code ack"), tr("Display code ack"), tr("A9 valid"), tr("SA9 valid") };
}

// ---- status -----------------------------------------------------------------------

QString pressureName(int id)
{
    static const char *const n[kPressures] = { "BP", "BC", "MR", "A9 ref", "SA9 ref", "Air flow", "Feed pipe" };
    return id >= 0 && id < kPressures ? tr(n[id]) : QString();
}

QString pressureLongName(int id)
{
    static const char *const n[kPressures] = { "Brake pipe", "Brake cylinder", "Main reservoir",
                                               "A9 control reference", "SA9 control reference",
                                               "Air flow", "Feed pipe" };
    return id >= 0 && id < kPressures ? tr(n[id]) : QString();
}

double validLimit(int id)
{
    static const double lim[kPressures] = { 6.0, 4.0, 10.5, 6.0, 4.0, 10.0, 10.0 };
    return id >= 0 && id < kPressures ? lim[id] : 0;
}

QStringList statusBitNames(int byte)
{
    if (byte == 1)
        return { tr("BIU healthy"), tr("Command overridden by loco pilot"), tr("Loco pilot overridden by BIU"),
                 tr("Manual isolate"), tr("BP charging cut-out"), tr("Emergency valve"), tr("Power cut-off relay") };
    if (byte == 2)
        return { tr("Panel test success"), tr("Panel test fail"), tr("Panel test in progress") };
    return { tr("BP cut-out valve on"), tr("BP control valve on"), tr("BC control valve on"), tr("Emergency valve on"),
             tr("BP cut-out valve healthy"), tr("BP control valve healthy"), tr("BC control valve healthy"),
             tr("Emergency valve healthy") };
}

QString statusBitMeaning(int byte, int bit)
{
    if (byte == 1) {
        switch (bit) {
        case StHealthy:          return tr("1 = BIU healthy, 0 = BIU fault");
        case StBpChargingCutout: return tr("1 = cut out, 0 = cut in");
        case StEmergencyValve:   return tr("1 = cut in, 0 = cut out");
        case StPowerCutoffRelay: return tr("1 = on, 0 = off");
        default:                 return tr("1 = yes, 0 = no");
        }
    }
    if (byte == 2) return tr("1 = yes, 0 = no");
    return bit < 4 ? tr("1 = on, 0 = off") : tr("1 = healthy, 0 = faulty");
}

QByteArray encodePressures(const Status &s)
{
    QByteArray d(kDataLen, '\0');
    quint8 v = 0;
    for (int i = 0; i < kPressures; ++i) {
        d[i] = char(s.raw[i]);
        if (s.valid[i]) v |= quint8(1u << i);
    }
    d[7] = char(v);
    return d;
}

QByteArray encodeCodes(const Status &s)
{
    QByteArray d(kDataLen, '\0');
    put16(d, 0, s.isolationCounter);
    put16(d, 2, s.faultCode);
    put16(d, 4, s.displayCode);
    return d;
}

QByteArray encodeDiscretes(const Status &s)
{
    QByteArray d(kDataLen, '\0');
    d[0] = char(s.d1);
    d[1] = char(s.d2);
    d[2] = char(s.d3);
    return d;
}

bool decodePressures(const QByteArray &data, Status *out)
{
    if (data.size() < kDataLen) return false;
    for (int i = 0; i < kPressures; ++i) {
        out->raw[i] = quint8(data.at(i));
        out->valid[i] = (quint8(data.at(7)) >> i) & 1;
    }
    return true;
}

bool decodeCodes(const QByteArray &data, Status *out)
{
    if (data.size() < 6) return false;
    out->isolationCounter = le16(data, 0);
    out->faultCode = le16(data, 2);
    out->displayCode = le16(data, 4);
    return true;
}

bool decodeDiscretes(const QByteArray &data, Status *out)
{
    if (data.size() < 3) return false;
    out->d1 = quint8(data.at(0));
    out->d2 = quint8(data.at(1));
    out->d3 = quint8(data.at(2));
    return true;
}

// ---- codes ------------------------------------------------------------------------

QVector<Code> faultCodes()
{
    return {
        { 0x1001, tr("A9 handle pressure sensor failure") },
        { 0x1002, tr("SA9 handle pressure sensor failure") },
        { 0x1003, tr("Main reservoir pressure sensor failure") },
        { 0x1004, tr("Brake pipe pressure sensor failure") },
        { 0x1005, tr("Brake cylinder pressure sensor failure") },
        { 0x1006, tr("Air flow sensor failure") },
        { 0x1007, tr("Feed pipe pressure sensor failure") },
        { 0x1010, tr("Brake pipe charging cut-out valve failure") },
        { 0x1018, tr("Formation brake control failure") },
        { 0x1020, tr("Independent brake control failure") },
        { 0x1028, tr("Emergency brake control failure") },
        { 0x1030, tr("Traction cut-off control failure") },
        { 0x1038, tr("Pneumatic power supply failure") },
        { 0x1040, tr("BIU ISOLATED") },
    };
}

QVector<Code> displayCodes()
{
    return {
        { 0x2001, tr("Operate BIU BP charging manual cock") },
        { 0x2003, tr("Operate emergency valve manual cock") },
        { 0x2005, tr("Operate BIU isolate switch") },
        { 0x2006, tr("Cannot apply A9 brakes in remote loco; it acts as bogie") },
        { 0x2007, tr("Keep loco BP cut-out cock (52 No) in cut-out position in remote loco") },
    };
}

QString codeText(quint16 code)
{
    if (code == 0) return QString();
    for (const Code &c : faultCodes())   if (c.code == code) return c.text;
    for (const Code &c : displayCodes()) if (c.code == code) return c.text;
    if ((code >= 0x1008 && code <= 0x100F) || (code >= 0x1048 && code <= 0x1FFF) || (code >= 0x2008 && code <= 0x2FFF))
        return tr("Spare");
    return tr("not in the spec's lists");
}

// ---- log --------------------------------------------------------------------------

QString frameName(quint16 cob)
{
    switch (cob) {
    case kCobNmt:             return tr("NMT");
    case kCobCommand:         return tr("Command");
    case kCobPressures:       return tr("Pressures");
    case kCobCodes:           return tr("Codes");
    case kCobDiscretes:       return tr("Discretes");
    case kCobHeartbeatBiu:    return tr("BIU heartbeat");
    case kCobHeartbeatKavach: return tr("KAVACH heartbeat");
    default:                  break;
    }
    if (cob >= 0x180 && cob < 0x200) return tr("Command to node 0x%1").arg(cob - 0x180, 2, 16, QLatin1Char('0'));
    if (cob >= 0x700 && cob < 0x780) return tr("Heartbeat of node 0x%1").arg(cob - 0x700, 2, 16, QLatin1Char('0'));
    return tr("Unknown");
}

QString describe(const Frame &f)
{
    const QByteArray &d = f.data;
    switch (f.cob) {
    case kCobNmt:
        if (d.size() < 2) return tr("short: %1 bytes").arg(d.size());
        return tr("%1, node %2").arg(nmtCommandName(quint8(d.at(0))),
                                     quint8(d.at(1)) == 0 ? tr("all") : hex8(quint8(d.at(1))));
    case kCobHeartbeatBiu:
    case kCobHeartbeatKavach:
        if (d.isEmpty()) return tr("no state byte");
        return nmtStateName(quint8(d.at(0)));
    case kCobCommand: {
        Command c;
        if (!decodeCommand(d, &c)) return tr("short: %1 bytes, a command is 8").arg(d.size());
        return tr("%1 m/s \u00B7 A9 %2%3 \u00B7 SA9 %4%5 \u00B7 %6%7")
            .arg(c.speedMs(), 0, 'f', 2)
            .arg(c.a9(), 0, 'f', 2).arg(c.a9Valid() ? QString() : tr(" (not valid)"))
            .arg(c.sa9(), 0, 'f', 2).arg(c.sa9Valid() ? QString() : tr(" (not valid)"))
            .arg(setBits(c.d1, commandBitNames(1)))
            .arg((c.d2 & 0x03) ? QStringLiteral(" \u00B7 ") + setBits(c.d2 & 0x03, commandBitNames(2)) : QString());
    }
    case kCobPressures: {
        Status s;
        if (!decodePressures(d, &s)) return tr("short: %1 bytes").arg(d.size());
        QStringList parts;
        for (int i = 0; i < kPressures; ++i)
            parts << QStringLiteral("%1 %2%3").arg(pressureName(i)).arg(byteToKg(s.raw[i]), 0, 'f', 2)
                         .arg(s.valid[i] ? QString() : QStringLiteral("\u2715"));
        return parts.join(QStringLiteral(" \u00B7 "));
    }
    case kCobCodes: {
        Status s;
        if (!decodeCodes(d, &s)) return tr("short: %1 bytes").arg(d.size());
        return tr("isolations %1 \u00B7 fault %2%3 \u00B7 display %4%5")
            .arg(s.isolationCounter)
            .arg(hex16(s.faultCode)).arg(s.faultCode ? QStringLiteral(" ") + codeText(s.faultCode) : QString())
            .arg(hex16(s.displayCode)).arg(s.displayCode ? QStringLiteral(" ") + codeText(s.displayCode) : QString());
    }
    case kCobDiscretes: {
        Status s;
        if (!decodeDiscretes(d, &s)) return tr("short: %1 bytes").arg(d.size());
        return tr("D1 %1 \u00B7 D2 %2 \u00B7 D3 %3").arg(hex8(s.d1), hex8(s.d2), hex8(s.d3));
    }
    default:
        return QString();
    }
}

QString hexBytes(const QByteArray &b)
{
    QStringList s;
    for (char c : b) s << QString::number(quint8(c), 16).toUpper().rightJustified(2, QLatin1Char('0'));
    return s.join(QLatin1Char(' '));
}

}  // namespace Biu
