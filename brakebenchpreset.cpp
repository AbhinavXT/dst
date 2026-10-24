#include "brakebenchpreset.h"

#include "settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace {

// Stable keys: the display names ("A9 ref") may be reworded, the file may not.
const char *const kAirKey[BrakePlant::kChannels] = { "bp", "bc", "mr" };
const char *const kAirName[BrakePlant::kChannels] = { "BP", "BC", "MR" };
const char *const kPressureKey[Biu::kPressures] = { "bp", "bc", "mr", "a9ref", "sa9ref", "airflow", "feedpipe" };
const char *const kFaultKey[4] = { "none", "stuck", "slow", "leak" };
const char *const kValidKey[3] = { "auto", "1", "0" };
const char kBitChar[3] = { 'A', '1', '0' };
const quint16 kFrameCob[3] = { Biu::kCobPressures, Biu::kCobCodes, Biu::kCobDiscretes };

const QString kStoreKey = QStringLiteral("brakebench/presets");

int indexOf(const char *const *keys, int n, const QString &v, int fallback)
{
    for (int i = 0; i < n; ++i)
        if (v == QLatin1String(keys[i])) return i;
    return fallback;
}

QString hex(quint16 c) { return QStringLiteral("0x") + QStringLiteral("%1").arg(c, 4, 16, QLatin1Char('0')).toUpper(); }
QString frameKey(int i) { return QStringLiteral("0x%1").arg(kFrameCob[i], 0, 16); }    // "0x220"

bool same(double a, double b) { return std::fabs(a - b) < 1e-9; }

QJsonObject readStore()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    return QJsonDocument::fromJson(s.value(kStoreKey).toString().toUtf8()).object();
}

void writeStore(const QJsonObject &o)
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    if (o.isEmpty()) s.remove(kStoreKey);
    else s.setValue(kStoreKey, QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

}  // namespace

QJsonObject BenchPreset::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), 1);
    QJsonObject airs;
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        const Air &a = air[ch];
        QJsonObject j;
        j.insert(QStringLiteral("apply"), a.apply);
        j.insert(QStringLiteral("release"), a.release);
        j.insert(QStringLiteral("fault"), QLatin1String(kFaultKey[qBound(0, a.fault, 3)]));
        j.insert(QStringLiteral("param"), a.param);
        j.insert(QStringLiteral("offset"), a.offset);
        airs.insert(QLatin1String(kAirKey[ch]), j);
    }
    o.insert(QStringLiteral("air"), airs);
    QJsonObject ps;
    for (int id = 0; id < Biu::kPressures; ++id) {
        const Pressure &p = pressure[id];
        QJsonObject j;
        j.insert(QStringLiteral("manual"), p.manual);
        j.insert(QStringLiteral("kg"), p.kg);
        j.insert(QStringLiteral("valid"), QLatin1String(kValidKey[qBound(0, p.valid, 2)]));
        ps.insert(QLatin1String(kPressureKey[id]), j);
    }
    o.insert(QStringLiteral("pressures"), ps);
    // "A1A0AAAA": bit 0 first; A = auto, 1 / 0 forced.
    QJsonArray bitRows;
    for (int byte = 0; byte < 3; ++byte) {
        QString row;
        for (int bit = 0; bit < 8; ++bit) row += QLatin1Char(kBitChar[qBound(0, bits[byte][bit], 2)]);
        bitRows.append(row);
    }
    o.insert(QStringLiteral("bits"), bitRows);
    QJsonArray f, d;
    for (quint16 c : faults) f.append(hex(c));
    for (quint16 c : displays) d.append(hex(c));
    o.insert(QStringLiteral("faults"), f);
    o.insert(QStringLiteral("displays"), d);
    QJsonObject frames;
    for (int i = 0; i < 3; ++i) frames.insert(frameKey(i), frameOn[i]);
    o.insert(QStringLiteral("frames"), frames);
    o.insert(QStringLiteral("silent"), silent);
    return o;
}

BenchPreset BenchPreset::fromJson(const QJsonObject &o, const BenchPreset &defaults)
{
    BenchPreset p = defaults;
    const QJsonObject airs = o.value(QStringLiteral("air")).toObject();
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        const QJsonObject j = airs.value(QLatin1String(kAirKey[ch])).toObject();
        Air &a = p.air[ch];
        a.apply = qMax(0.01, j.value(QStringLiteral("apply")).toDouble(a.apply));
        a.release = qMax(0.01, j.value(QStringLiteral("release")).toDouble(a.release));
        a.fault = indexOf(kFaultKey, 4, j.value(QStringLiteral("fault")).toString(), a.fault);
        a.param = j.value(QStringLiteral("param")).toDouble(a.param);
        a.offset = j.value(QStringLiteral("offset")).toDouble(a.offset);
    }
    const QJsonObject ps = o.value(QStringLiteral("pressures")).toObject();
    for (int id = 0; id < Biu::kPressures; ++id) {
        const QJsonObject j = ps.value(QLatin1String(kPressureKey[id])).toObject();
        Pressure &q = p.pressure[id];
        q.manual = j.value(QStringLiteral("manual")).toBool(q.manual);
        q.kg = j.value(QStringLiteral("kg")).toDouble(q.kg);
        q.valid = indexOf(kValidKey, 3, j.value(QStringLiteral("valid")).toString(), q.valid);
    }
    const QJsonArray bitRows = o.value(QStringLiteral("bits")).toArray();
    for (int byte = 0; byte < 3 && byte < bitRows.size(); ++byte) {
        const QString row = bitRows.at(byte).toString();
        for (int bit = 0; bit < 8 && bit < row.size(); ++bit) {
            const char c = row.at(bit).toLatin1();
            const char *hit = std::find(kBitChar, kBitChar + 3, c);
            if (hit != kBitChar + 3) p.bits[byte][bit] = int(hit - kBitChar);
        }
    }
    auto codes = [&o](const QString &key, QVector<quint16> *out) {
        if (!o.contains(key)) return;
        out->clear();
        for (const QJsonValue &v : o.value(key).toArray()) {
            bool ok = false;
            const uint c = v.toString().toUInt(&ok, 0);
            if (ok && c > 0 && c <= 0xFFFF) out->append(quint16(c));
        }
    };
    codes(QStringLiteral("faults"), &p.faults);
    codes(QStringLiteral("displays"), &p.displays);
    const QJsonObject frames = o.value(QStringLiteral("frames")).toObject();
    for (int i = 0; i < 3; ++i) {
        p.frameOn[i] = frames.value(frameKey(i)).toBool(p.frameOn[i]);
    }
    p.silent = o.value(QStringLiteral("silent")).toBool(p.silent);
    return p;
}

QStringList BenchPreset::differences(const BenchPreset &clean) const
{
    static const char *const faultWord[4] = { "", "stuck", "slow ×%1", "leak %1/s" };
    QStringList out;
    for (int ch = 0; ch < BrakePlant::kChannels; ++ch) {
        const Air &a = air[ch], &c = clean.air[ch];
        const QString n = QLatin1String(kAirName[ch]);
        if (a.fault != 0 && (a.fault != c.fault || !same(a.param, c.param) || c.fault == 0))
            out << n + QLatin1Char(' ') + QString::fromUtf8(faultWord[qBound(0, a.fault, 3)]).arg(a.param);
        if (!same(a.apply, c.apply) || !same(a.release, c.release))
            out << QStringLiteral("%1 rates %2 / %3 per s").arg(n).arg(a.apply).arg(a.release);
        if (!same(a.offset, c.offset))
            out << QStringLiteral("%1 analog %2%3").arg(n, a.offset > 0 ? QStringLiteral("+") : QString()).arg(a.offset, 0, 'f', 2);
    }
    for (int id = 0; id < Biu::kPressures; ++id) {
        const Pressure &q = pressure[id];
        if (q.manual) out << QStringLiteral("%1 manual %2").arg(Biu::pressureName(id)).arg(q.kg, 0, 'f', 2);
        if (q.valid != clean.pressure[id].valid)
            out << QStringLiteral("%1 valid %2").arg(Biu::pressureName(id), q.valid == 1 ? QStringLiteral("1") : QStringLiteral("0"));
    }
    int forced = 0;
    for (int byte = 0; byte < 3; ++byte)
        for (int bit = 0; bit < 8; ++bit) forced += bits[byte][bit] != clean.bits[byte][bit];
    if (forced) out << (forced == 1 ? QStringLiteral("1 bit forced") : QStringLiteral("%1 bits forced").arg(forced));
    for (quint16 c : faults) out << QStringLiteral("fault %1 queued").arg(hex(c));
    for (quint16 c : displays) out << QStringLiteral("display %1 queued").arg(hex(c));
    for (int i = 0; i < 3; ++i)
        if (!frameOn[i]) out << frameKey(i) + QStringLiteral(" off");
    if (silent) out << QStringLiteral("silent");
    return out;
}

// ---- the store ----------------------------------------------------------------------

QStringList BenchPreset::names()
{
    QStringList n = readStore().keys();
    std::sort(n.begin(), n.end(), [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    return n;
}

bool BenchPreset::exists(const QString &name) { return readStore().contains(name); }

bool BenchPreset::load(const QString &name, const BenchPreset &defaults, BenchPreset *out)
{
    const QJsonObject store = readStore();
    if (!store.contains(name)) return false;
    if (out) *out = fromJson(store.value(name).toObject(), defaults);
    return true;
}

void BenchPreset::save(const QString &name, const BenchPreset &p)
{
    QJsonObject store = readStore();
    store.insert(name, p.toJson());
    writeStore(store);
}

void BenchPreset::remove(const QString &name)
{
    QJsonObject store = readStore();
    store.remove(name);
    writeStore(store);
}
