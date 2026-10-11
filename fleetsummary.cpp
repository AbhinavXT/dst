#include "fleetsummary.h"

#include "fieldplot.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "missionreport.h"
#include "odometry.h"
#include "radiolinks.h"
#include "runreport.h"
#include "speeddistance.h"

#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace Fleet {
namespace {

// High-rate types no column reads: left out so a day's logs fit in memory.
// shortcut: a fixed list; a new column reading one of these must take it off.
bool skipped(const QString &type)
{
    static const QSet<QString> s{ QStringLiteral("dip1"), QStringLiteral("dip2"), QStringLiteral("dop1"), QStringLiteral("dop2"),
                                  QStringLiteral("analog_top"), QStringLiteral("analog_bottom"), QStringLiteral("linfo"),
                                  QStringLiteral("nmsrssi"), QStringLiteral("ccsys"), QStringLiteral("dlsys") };
    return s.contains(type);
}

struct Line { qint64 ms = 0; QByteArray message; };

// One data line of a .log ("2026-10-08T05:28:54.343Z  81_1 ... @dmi_1_1 ...")
// or a .cap ("@dmi_1_1 2026-10-08T11:01:12 ..."); ms 0 when it is not one.
Line parseLine(const QByteArray &raw, bool isLog)
{
    Line l;
    const QByteArray t = raw.trimmed();
    if (t.isEmpty() || t.startsWith('#')) return l;
    const int at = t.indexOf('@');
    if (at < 0) return l;
    l.message = t.mid(at);
    if (isLog) {
        l.ms = QDateTime::fromString(QString::fromLatin1(t.left(24)), Qt::ISODateWithMs).toMSecsSinceEpoch();
    } else {
        const QList<QByteArray> tok = l.message.split(' ');
        if (tok.size() >= 2) l.ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
    }
    if (l.ms <= 0) l.ms = 0;
    return l;
}

struct FileInfo { bool ok = false; quint8 src = 0; quint16 kvch = 0; qint64 firstMs = 0; };

FileInfo probe(const QString &path)
{
    FileInfo fi;
    const bool isLog = path.endsWith(QLatin1String(".log"), Qt::CaseInsensitive);
    if (!isLog) {
        static const QRegularExpression re(QStringLiteral("^loco_(\\d+)_(\\d+)_"));
        const QRegularExpressionMatch m = re.match(QFileInfo(path).fileName());
        if (!m.hasMatch()) return fi;
        fi.src = quint8(m.captured(1).toUInt());
        fi.kvch = quint16(m.captured(2).toUInt());
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fi;
    bool header = false;
    for (int n = 0; n < 200 && !f.atEnd(); ++n) {
        const QByteArray raw = f.readLine();
        if (isLog && raw.startsWith('#')) {
            const QByteArray t = raw.mid(1).trimmed();
            if (t.startsWith("DLConsole log")) header = true;
            const QByteArray value = t.mid(t.indexOf('=') + 1).trimmed().split(' ').value(0);
            if (t.startsWith("source_id")) fi.src = quint8(value.toUInt());
            if (t.startsWith("kvch_id")) fi.kvch = quint16(value.toUInt());
            continue;
        }
        const Line l = parseLine(raw, isLog);
        if (l.ms) { fi.firstMs = l.ms; break; }
    }
    fi.ok = (header || !isLog) && fi.firstMs > 0;
    return fi;
}

// "4 (Full_Supervision)" -> "Full_Supervision".
QString modeName(const QString &text)
{
    const int open = text.indexOf(QLatin1Char('('));
    return open >= 0 ? text.mid(open + 1).section(QLatin1Char(')'), 0, 0) : text;
}

QString csvField(const QString &s)
{
    if (!s.contains(QLatin1Char(',')) && !s.contains(QLatin1Char('"')) && !s.contains(QLatin1Char('\n'))) return s;
    QString q = s;
    q.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

}  // namespace

QString Row::modesText() const
{
    qint64 all = 0;
    for (const qint64 v : modeMs) all += v;
    if (all <= 0) return QString();
    QVector<QPair<qint64, QString>> v;
    for (auto it = modeMs.cbegin(); it != modeMs.cend(); ++it) v << qMakePair(it.value(), it.key());
    std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList parts;
    for (const auto &p : v) {
        const int pct = int(std::lround(100.0 * double(p.first) / double(all)));
        if (pct > 0) parts << QStringLiteral("%1 %2 %").arg(p.second).arg(pct);
    }
    return parts.join(QStringLiteral(" · "));
}

QVector<Group> scan(const QString &dir)
{
    struct Found { QString path; FileInfo info; };
    QVector<Found> found;
    QDirIterator it(dir, { QStringLiteral("*.log"), QStringLiteral("*.cap") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const FileInfo fi = probe(path);
        if (fi.ok) found << Found{ path, fi };
    }
    std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) { return a.info.firstMs < b.info.firstMs; });
    QMap<QString, Group> groups;                       // "81_1 2026-10-08" -> group, sorted by key
    for (const Found &f : found) {
        const QString loco = QStringLiteral("%1_%2").arg(int(f.info.src)).arg(int(f.info.kvch));
        const QDate day = QDateTime::fromMSecsSinceEpoch(f.info.firstMs).date();
        Group &g = groups[loco + QLatin1Char(' ') + day.toString(Qt::ISODate)];
        g.sourceId = f.info.src;
        g.kvchId = f.info.kvch;
        g.loco = loco;
        g.day = day;
        g.files << f.path;
    }
    return groups.values().toVector();
}

Row summarise(const Group &group, const std::function<bool()> &keepGoing)
{
    MessageDispatcher d;
    d.setPerTabCapacity(4'000'000);
    QStringList unreadable;
    int pending = 0;
    for (const QString &path : group.files) {
        if (keepGoing && !keepGoing()) return Row();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) { unreadable << QFileInfo(path).fileName(); continue; }
        const bool isLog = path.endsWith(QLatin1String(".log"), Qt::CaseInsensitive);
        while (!f.atEnd()) {
            const Line l = parseLine(f.readLine(), isLog);
            if (!l.ms || skipped(captureTypeOf(QString::fromLatin1(l.message.left(32))))) continue;
            d.ingestLocal(group.sourceId, group.kvchId, l.message, l.ms, QString());
            if (++pending >= 50000) {
                d.drainNow();
                pending = 0;
                if (keepGoing && !keepGoing()) return Row();
            }
        }
    }
    d.drainNow();
    Row r = summariseModel(d.modelForKey(group.loco), group.loco);
    r.loco = group.loco;
    r.day = group.day;
    r.files = group.files;
    if (!unreadable.isEmpty()) r.error = QObject::tr("could not read %1").arg(unreadable.join(QStringLiteral(", ")));
    return r;
}

Row summariseModel(const LogModel *model, const QString &tabKey)
{
    Row r;
    r.loco = tabKey;
    if (!model || model->count() == 0) return r;
    r.frames = model->count();

    RunReport::Options o;
    o.maxListRows = 1'000'000;                            // the mode times need every change
    const RunReport::Summary s = RunReport::summarise(model, tabKey, tabKey, o);
    r.firstMs = s.firstMs;
    r.lastMs = s.lastMs;
    QString mode = s.firstMode;
    qint64 since = s.firstMs;
    for (const RunReport::Change &c : s.modeChanges) {
        if (!mode.isEmpty()) r.modeMs[modeName(mode)] += c.ms - since;
        mode = c.to;
        since = c.ms;
    }
    if (!mode.isEmpty()) r.modeMs[modeName(mode)] += s.lastMs - since;

    QMap<QString, int> reasons;
    for (const RunReport::BrakeEvent &b : s.brakes) {
        if (b.type.contains(QLatin1String("EMERGENCY"))) ++r.eb; else ++r.fsb;
        for (const QString &why : b.reasons) reasons[why] += 1;
    }
    QVector<QPair<int, QString>> top;
    for (auto it = reasons.cbegin(); it != reasons.cend(); ++it) top << qMakePair(it.value(), it.key());
    std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList topText;
    for (int i = 0; i < qMin(3, top.size()); ++i) topText << QStringLiteral("%1 ×%2").arg(top.at(i).second).arg(top.at(i).first);
    r.brakeReasons = topText.join(QStringLiteral("; "));

    r.maxSpeedKmh = s.maxSpeedKmh;
    for (const RunReport::FaultEvent &f : s.faults) r.faults += f.raised ? 1 : 0;
    for (const int n : s.rejectClauses) r.rejectFrames += n;
    for (const int n : s.arpRecvRejects) r.rejectFrames += n;
    r.tags = s.distinctTags;
    r.missions = s.missionStarts.size();

    // Session 219: the start-ups, and which step was the slow one.
    QVector<qint64> startUps;
    QMap<QString, int> slowest;
    for (const Missions::Mission &m : Missions::split(model)) {
        if (!m.index || !m.startUpMs()) continue;
        startUps << m.startUpMs();
        slowest[Missions::slowestStep(Missions::startUpSteps(m))] += 1;
    }
    if (!startUps.isEmpty()) {
        std::sort(startUps.begin(), startUps.end());
        r.startUps = startUps.size();
        r.startUpMedianMs = startUps.size() % 2 ? startUps.at(startUps.size() / 2)
                                                : (startUps.at(startUps.size() / 2 - 1) + startUps.at(startUps.size() / 2)) / 2;
        QVector<QPair<int, QString>> order;
        for (auto it = slowest.cbegin(); it != slowest.cend(); ++it) order << qMakePair(it.value(), it.key());
        std::stable_sort(order.begin(), order.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        QStringList parts;
        for (const auto &o : order) parts << QStringLiteral("%1 ×%2").arg(o.second).arg(o.first);
        r.slowestSteps = parts.join(QStringLiteral("; "));
    }

    for (const RadioLinks::Link &l : RadioLinks::build(model).links) {
        if (l.type != QLatin1String("slrp")) continue;
        r.slrpReceived += l.frames - l.repeats;
        r.slrpLost += l.lost;
        r.outages += l.outages.size();
    }
    const Odometry::Report odo = Odometry::build(model);
    r.odoReads = odo.counted;
    r.odoMedianM = odo.medianM;

    // Distance: SpeedDistance's location steps, relocations (over 500 m) left out.
    const SpeedDistance::Trace trace = SpeedDistance::knownOnly(SpeedDistance::extract(model, 4'000'000));
    double m = 0.0;
    for (int i = 1; i < trace.samples.size(); ++i) {
        const double step = std::fabs(trace.samples.at(i).locM - trace.samples.at(i - 1).locM);
        if (step <= 500.0) m += step;
    }
    r.distanceKm = m / 1000.0;
    return r;
}

QString toCsv(const QVector<Row> &rows)
{
    QStringList lines;
    lines << QStringLiteral("loco,day,files,frames,from,to,hours,distance_km,modes,eb,fsb,brake_reasons,max_speed_kmh,"
                            "faults_raised,reject_frames,tags,missions,slrp_received,slrp_lost,slrp_lost_pct,outages,"
                            "odometry_reads,odometry_median_m,startups,startup_median_s,slowest_steps,error");
    auto t = [](qint64 ms) { return ms ? QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")) : QString(); };
    for (const Row &r : rows) {
        QStringList names;
        for (const QString &f : r.files) names << QFileInfo(f).fileName();
        lines << QStringList{
            r.loco, r.day.toString(Qt::ISODate), csvField(names.join(QStringLiteral("; "))), QString::number(r.frames),
            t(r.firstMs), t(r.lastMs), QString::number(double(r.lastMs - r.firstMs) / 3600000.0, 'f', 2),
            QString::number(r.distanceKm, 'f', 2), csvField(r.modesText()), QString::number(r.eb), QString::number(r.fsb),
            csvField(r.brakeReasons), QString::number(r.maxSpeedKmh, 'f', 0), QString::number(r.faults),
            QString::number(r.rejectFrames), QString::number(r.tags), QString::number(r.missions),
            QString::number(r.slrpReceived), QString::number(r.slrpLost), QString::number(r.slrpLostPercent(), 'f', 1),
            QString::number(r.outages), QString::number(r.odoReads), r.odoReads ? QString::number(r.odoMedianM, 'f', 2) : QString(),
            QString::number(r.startUps), r.startUps ? QString::number(r.startUpMedianMs / 1000) : QString(), csvField(r.slowestSteps),
            csvField(r.error)
        }.join(QLatin1Char(','));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

}  // namespace Fleet
