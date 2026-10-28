#include "simpreview.h"

#include "capturedecoder.h"
#include "logmodel.h"

#include <QHash>
#include <QSet>
#include <QtEndian>

#include <algorithm>

namespace SimPreview {

namespace {

// The simulator's constants (IRS.h).
constexpr double kPi = 3.1428571;
constexpr double kWheel = 950 / 1000.0;
constexpr int    kPulsesPerRev = 30;
constexpr double kKmhToMs = 0.277777778;

bool dupOf(const QByteArray &tag) { return RfidTag::summary(tag).duplicate; }
qint64 locOf(const QByteArray &tag) { return RfidTag::summary(tag).absLoc; }

}  // namespace

quint32 pulsesPerTick(double kmh)
{
    const double v = kmh * kKmhToMs;
    return quint32(((0.1 * v * kPulsesPerRev) / (kPi * kWheel)) * 1000);
}

double metresPerTick(double kmh)
{
    return (kPi * kWheel * pulsesPerTick(kmh)) / kPulsesPerRev / 1000;
}

QVector<Row> rowsOf(const RfidTag::Route &route, bool *fromFileRows)
{
    bool fileRows = true;
    for (const RfidTag::Tag &t : route.tags)
        fileRows = fileRows && t.file.present && RfidTag::fromPages(t.file.pageX, t.file.pageY) == t.bytes;
    QVector<Row> rows;
    if (fileRows) {
        for (const RfidTag::Tag &t : route.tags)
            rows.append({ t.file.rfidId, t.file.tagName, t.file.tagType, t.file.absLoc, t.file.nextAbsLoc, t.bytes });
    } else {
        const QVector<RfidTag::RouteRow> rr = RfidTag::routeRows(route.tags, route.dir);
        for (int i = 0; i < rr.size(); ++i)
            rows.append({ rr.at(i).rfidId, rr.at(i).tagName, rr.at(i).tagType, double(rr.at(i).absLoc),
                          double(rr.at(i).nextAbsLoc), route.tags.at(i).bytes });
    }
    if (fromFileRows) *fromFileRows = fileRows;
    return rows;
}

void Readers::setMissing(const QStringList &names)
{
    m_missing.clear();
    for (const QString &m : names) m_missing.insert(m.trimmed().toUpper());
}

int Readers::step(double d, double extraMetres, QVector<Send> *sends)
{
    int hit = -1;
    for (int i = 0; i < m_rows.size() && hit < 0; ++i) {
        const Row &x = m_rows.at(i);
        if ((x.absLoc <= d && x.nextAbsLoc > d) || (x.absLoc >= d && x.nextAbsLoc < d)) hit = i;
    }
    if (hit < 0) return -1;
    m_passed.insert(hit);
    int s = hit;
    if (m_missing.contains(m_rows.at(hit).id.toUpper())) {
        s = m_previous;                                    // the simulator re-sends the previous row
    } else {
        m_previous = hit;
    }
    if (s < 0) return hit;                                 // nothing before it: nothing sent
    const Row &x = m_rows.at(s);
    if (x.tagType < 9 || x.tagType > 12) return hit;       // no packet for that tag_type
    const auto send = [&](int reader, int row, const QByteArray &tag) {
        if (sends) sends->append({ reader, row, tag });
    };
    if (m_current != x.id) {
        if (x.tagName == QLatin1String("main")) m_first.insert(x.tagType, { s, x.bytes });
        else m_second.insert(x.tagType, { s, x.bytes });
    }
    if (m_current != x.id && m_reader1) {
        send(1, s, x.bytes);
        m_sentOn1.insert(s);
    } else if (m_current == x.id && m_reader2) {
        const double gap = qAbs(d - x.absLoc);
        if (gap >= 20 && gap < 24 + qMax(0.0, extraMetres)) {
            const QPair<int, QByteArray> f = m_first.value(x.tagType, { -1, QByteArray() });
            if (!f.second.isEmpty() && locOf(f.second) != 0) {
                send(2, f.first, f.second);
                m_mainSentOn2.insert(f.first);
                m_first.remove(x.tagType);
            }
            m_secondFlag = true;
        } else if (gap >= 24 && m_secondFlag) {
            const QPair<int, QByteArray> sc = m_second.value(x.tagType, { -1, QByteArray() });
            if (!sc.second.isEmpty() && dupOf(sc.second)) {
                if (locOf(sc.second) != 0) {
                    send(2, sc.first, sc.second);
                    m_second.remove(x.tagType);
                }
                m_secondFlag = false;
            }
        }
    }
    m_current = x.id;
    return hit;
}

Result run(const RfidTag::Route &route, const Options &o)
{
    Result r;
    if (route.dir != RfidTag::DirNominal && route.dir != RfidTag::DirReverse) {
        r.error = QStringLiteral("set the route's direction: the simulator moves by it");
        return r;
    }
    if (route.tags.size() < 2 || o.startRow < 0 || o.startRow >= route.tags.size()) {
        r.error = QStringLiteral("the route needs at least two tags and a start tag in it");
        return r;
    }
    if (o.speedKmh <= 0) {
        r.error = QStringLiteral("the speed must be above 0 (the simulator stops at 0)");
        return r;
    }

    const QVector<Row> rows = rowsOf(route, &r.fromFileRows);
    r.pulsesPerTick = pulsesPerTick(o.speedKmh);
    r.metresPerTick = metresPerTick(o.speedKmh);
    if (r.pulsesPerTick == 0) {
        r.error = QStringLiteral("at %1 km/h the simulator counts 0 pulses a tick and stops").arg(o.speedKmh);
        return r;
    }
    Readers readers(rows);
    readers.setReader(1, o.reader1);
    readers.setReader(2, o.reader2);
    readers.setMissing(o.missing);

    const double sign = route.dir == RfidTag::DirNominal ? 1 : -1;
    double d = rows.at(o.startRow).absLoc;
    QVector<Readers::Send> sends;
    for (int tick = 1; tick <= o.maxTicks; ++tick) {
        d += sign * r.metresPerTick;
        sends.clear();
        if (readers.step(d, 0, &sends) < 0) {
            r.ticks = tick;
            r.endDistance = d;
            r.endReason = QStringLiteral("no row spans %1 m: past the route's last row").arg(d, 0, 'f', 1);
            break;
        }
        for (const Readers::Send &s : sends)
            r.events.append({ tick, tick * 0.1, d, s.reader, s.row, RfidTag::nameOf(s.tag), locOf(s.tag) });
        r.ticks = tick;
        r.endDistance = d;
    }
    if (r.endReason.isEmpty()) r.endReason = QStringLiteral("stopped after %1 ticks").arg(o.maxTicks);

    // What the run went past without a reader-1 send: the last row (never
    // spanned) and missing tags.
    const int from = o.startRow;
    for (int i = from; i < rows.size(); ++i) {
        if (!readers.sentOn1().contains(i) && (readers.passed().contains(i) || i == rows.size() - 1))
            r.neverOnReader1 << rows.at(i).id;
        if (readers.sentOn1().contains(i) && rows.at(i).tagName == QLatin1String("main") && !readers.mainSentOn2(i)
            && o.reader2)
            r.mainsMissedOnReader2 << rows.at(i).id;
    }
    r.ok = true;
    return r;
}

Against against(const Result &preview, const QVector<PlanRun::Read> &reads)
{
    Against a;
    a.readOf.fill(-1, preview.events.size());
    a.lagS.fill(0, preview.events.size());
    QHash<int, int> next;                                  // reader -> first read not yet looked at
    QSet<int> used;
    for (int i = 0; i < preview.events.size(); ++i) {
        const Event &e = preview.events.at(i);
        for (int k = next.value(e.reader); k < reads.size(); ++k) {
            if (reads.at(k).reader != e.reader || RfidTag::nameOf(reads.at(k).bytes) != e.tag) continue;
            a.readOf[i] = k;
            next.insert(e.reader, k + 1);
            used.insert(k);
            break;
        }
    }
    qint64 t0 = 0, firstMs = 0, lastMs = 0;
    bool any = false;
    for (int i = 0; i < a.readOf.size(); ++i) {
        const int k = a.readOf.at(i);
        if (k < 0) continue;
        const qint64 ms = reads.at(k).ms;
        if (!any) t0 = ms - qint64(preview.events.at(i).timeS * 1000);
        firstMs = any ? qMin(firstMs, ms) : ms;
        lastMs = any ? qMax(lastMs, ms) : ms;
        any = true;
        a.lagS[i] = (ms - t0) / 1000.0 - preview.events.at(i).timeS;
        a.maxLagS = qMax(a.maxLagS, qAbs(a.lagS.at(i)));
        ++a.matched;
    }
    for (int k = 0; any && k < reads.size(); ++k)
        if (!used.contains(k) && reads.at(k).ms >= firstMs && reads.at(k).ms <= lastMs) a.unpredicted << k;
    return a;
}

double kmhOfPulses(quint32 pulses)
{
    return (kPi * kWheel * pulses) / kPulsesPerRev / 1000 * 10 * 3.6;
}

SpeedSeen speedsOf(const LogModel *model)
{
    SpeedSeen s;
    if (!model) return s;
    QVector<double> kmh;
    for (int i = 0; i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e || !e->text.startsWith(QLatin1String("@speed_"))) continue;
        const CaptureLine c = CaptureDecoder::parseLine(e->text);
        if (!c.valid || c.bytes.size() < 14) continue;     // reader, dir, tacho 1, tacho 2, crc
        const uchar *b = reinterpret_cast<const uchar *>(c.bytes.constData());
        ++s.frames;
        s.crcZero += qFromLittleEndian<quint32>(b + 10) == 0 ? 1 : 0;
        const quint32 p = qFromLittleEndian<quint32>(b + 2);
        if (p > 0) kmh << kmhOfPulses(p);
    }
    s.moving = kmh.size();
    if (!kmh.isEmpty()) {
        std::sort(kmh.begin(), kmh.end());
        s.minKmh = kmh.first();
        s.maxKmh = kmh.last();
        s.medianKmh = kmh.at(kmh.size() / 2);
    }
    return s;
}

}  // namespace SimPreview
