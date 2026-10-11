#include "packetmatrix.h"

#include "packetbuilder.h"
#include "packetsequencedialog.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace PacketMatrix {

namespace {

bool binaryDigits(const QString &s)
{
    for (const QChar c : s)
        if (c != QLatin1Char('0') && c != QLatin1Char('1')) return false;
    return !s.isEmpty();
}

bool settable(const Schema::FieldInfo &fi)
{
    return !fi.isPad && !fi.isCrc && fi.bits > 0 && fi.type.isEmpty()   // typed: session 244
           && fi.name != QLatin1String("PKT_LENGTH");
}

} // namespace

QString FieldRef::label() const
{
    return subType < 0 ? name : structName + QLatin1Char('.') + name;
}

QVector<FieldRef> fieldsOf(const Schema::Encoder &enc, const QString &captype)
{
    QVector<FieldRef> out;
    const Schema::PacketInfo pi = enc.packet(captype);
    if (!pi.ok) return out;
    for (const Schema::FieldInfo &fi : pi.header) {
        if (!settable(fi)) continue;
        FieldRef f;
        f.name = fi.name;
        f.bits = fi.bits;
        f.isSigned = fi.isSigned;
        out << f;
    }
    for (const Schema::CaseInfo &ci : pi.cases) {
        const Schema::StructLayout lay = enc.structLayout(ci.structName);
        for (const Schema::FieldInfo &fi : lay.scalarFields) {
            if (!settable(fi)) continue;
            FieldRef f;
            f.name = fi.name;
            f.bits = fi.bits;
            f.isSigned = fi.isSigned;
            f.subType = ci.type;
            f.structName = ci.structName;
            out << f;
        }
    }
    return out;
}

bool findValue(const QString &text, QString *field, QString *printed)
{
    // "PKT_TYPE as 1000", "REF_PROF_ID (4 bits) as ‘0001’", "FRAME_NUM = 0x1FFFF"
    static const QRegularExpression re(QStringLiteral(
        "\\b([A-Z][A-Z0-9_]{2,})\\s*(?:\\([^)]*\\)\\s*)?(?:as|=|is|of|to)\\s*[\\x{2018}'\"\\x{201C}]?"
        "(0[xX][0-9A-Fa-f]+|\\d+)\\b"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) return false;
    if (field) *field = m.captured(1);
    if (printed) *printed = m.captured(2);
    return true;
}

QString senderOf(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^\\W*(?:[ivx]+[.)]\\s*|\\d+[.)]\\s*)?(Stationary|Station|Onboard)\\b"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) return QString();
    return m.captured(1).compare(QLatin1String("Onboard"), Qt::CaseInsensitive) == 0 ? QStringLiteral("Onboard")
                                                                                     : QStringLiteral("Station");
}

QString senderOfPacket(const QString &captype)
{
    const QString c = captype.toLower();
    if (c == QLatin1String("slrp") || c == QLatin1String("aap") || c == QLatin1String("aep")) return QStringLiteral("Station");
    if (c == QLatin1String("lsrp") || c == QLatin1String("arp")) return QStringLiteral("Onboard");
    return QString();
}

Reading read(const QString &printed, int bits, const QStringList &siblings)
{
    Reading r;
    const QString p = printed.trimmed();
    if (p.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        r.value = p.mid(2).toLongLong(&r.ok, 16);
        r.how = QStringLiteral("%1 = %2").arg(p).arg(r.value);
        return r;
    }
    if (!binaryDigits(p)) {
        r.value = p.toLongLong(&r.ok, 10);
        r.how = QStringLiteral("decimal %1").arg(p);
        return r;
    }
    if (p.size() == 1) {            // 0 and 1 read the same either way
        r.value = p.toLongLong(&r.ok, 10);
        r.how = p;
        return r;
    }
    if (p.size() == bits) {         // as wide as the field: its bits
        r.value = p.toLongLong(&r.ok, 2);
        r.how = QStringLiteral("binary %1 = %2").arg(p).arg(r.value);
        return r;
    }
    // Narrower or wider than the field: the other rows decide, and the row
    // says so. A case printing 0, 1, 10, 11, 100 is counting in binary.
    bool siblingsBinary = true;
    for (const QString &s : siblings) siblingsBinary = siblingsBinary && binaryDigits(s.trimmed());
    r.sure = false;
    if (siblingsBinary) {
        r.value = p.toLongLong(&r.ok, 2);
        r.how = QStringLiteral("binary %1 = %2?").arg(p).arg(r.value);
        r.why = QStringLiteral("%1 digits for a %2-bit field; the other rows print binary").arg(p.size()).arg(bits);
    } else {
        r.value = p.toLongLong(&r.ok, 10);
        r.how = QStringLiteral("decimal %1?").arg(p);
        r.why = QStringLiteral("the other rows print decimal");
    }
    return r;
}

bool parseTyped(const QString &text, qint64 *out)
{
    const QString t = text.trimmed();
    bool ok = false;
    qint64 v = 0;
    if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) v = t.mid(2).toLongLong(&ok, 16);
    else if (t.startsWith(QLatin1String("0b"), Qt::CaseInsensitive)) v = t.mid(2).toLongLong(&ok, 2);
    else v = t.toLongLong(&ok, 10);
    if (ok && out) *out = v;
    return ok && !t.isEmpty();
}

bool fits(const FieldRef &f, qint64 value)
{
    if (f.bits <= 0 || f.bits >= 63) return true;
    if (f.isSigned) {
        const qint64 half = qint64(1) << (f.bits - 1);
        return value >= -half && value < half;
    }
    return value >= 0 && value < (qint64(1) << f.bits);
}

bool apply(const PacketPreset &base, const FieldRef &f, qint64 value, PacketPreset *out, QString *why)
{
    PacketPreset p = base;
    if (f.subType < 0) {
        p.header.insert(f.name, value);
    } else {
        int n = 0;
        for (Schema::SubEntry &s : p.subs) {
            if (s.type != f.subType) continue;
            s.values.insert(f.name, value);
            ++n;
        }
        if (n == 0) {
            if (why) *why = QStringLiteral("the base preset has no %1 sub-packet (type %2) to set %3 in")
                                .arg(f.structName).arg(f.subType).arg(f.name);
            return false;
        }
    }
    // A variation rule on the field would replace the value on every send.
    for (PacketVary::Rule &r : p.vary)
        if (r.field == f.name && r.sub == (f.subType < 0 ? QString() : f.structName)) r.enabled = false;
    *out = p;
    return true;
}

QVector<Outcome> generate(const PacketPreset &base, const FieldRef &f, const QVector<Row> &rows,
                          const QString &folder, const QString &caseId, int delayMs, bool overwrite,
                          PacketBuilder &builder, QString *seqPath)
{
    QVector<Outcome> out;
    QVector<PacketSequenceDialog::FileStep> steps;
    const QDir dir(folder);
    for (const Row &row : rows) {
        Outcome o;
        o.clause = row.clause;
        const QString file = dir.absoluteFilePath(row.clause + QStringLiteral(".packet.json"));
        PacketPreset p;
        if (!fits(f, row.value)) {
            o.why = QStringLiteral("%1 does not fit %2's %3 bits").arg(row.value).arg(f.name).arg(f.bits);
        } else if (apply(base, f, row.value, &p, &o.why)) {
            const PacketBuilder::Result built = p.build(builder, -1, QByteArray());
            if (!built.ok) {
                o.why = QStringLiteral("does not build: %1").arg(built.error);
            } else if (!overwrite && QFileInfo::exists(file)) {
                o.why = QStringLiteral("%1 is already there").arg(QFileInfo(file).fileName());
            } else if (!p.save(file, &o.why)) {
                // o.why says why
            } else {
                o.ok = true;
                o.file = file;
                PacketSequenceDialog::FileStep s;
                s.preset = file;
                s.delayMs = delayMs;
                steps << s;
            }
        }
        out << o;
    }
    if (seqPath) seqPath->clear();
    if (!steps.isEmpty()) {
        const QString seq = dir.absoluteFilePath(caseId + QStringLiteral(".seq.json"));
        if ((overwrite || !QFileInfo::exists(seq)) && PacketSequenceDialog::writeFile(seq, base.dest, false, steps)) {
            if (seqPath) *seqPath = seq;
        }
    }
    return out;
}

} // namespace PacketMatrix
