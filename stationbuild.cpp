#include "stationbuild.h"

#include "rfidtag.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace StationBuild {

namespace {

QString tr(const char *s) { return QCoreApplication::translate("StationBuild", s); }

constexpr qint64 kMaxLocation = 8388606;   // abs_loc: 23 bits, 8388607 = not applicable
constexpr int kApproach = 1200;            // m before the entry points: the approach tag
constexpr int kHome = 200;                 // m before them: the home signal
constexpr int kStarter = 150;              // m inside the yard's end: a starter signal
constexpr int kExit = 200;                 // m past the exit points
constexpr int kTurnout = 100;              // m a point takes from one line to the other
constexpr int kPair = 4;                   // m from a main tag to its duplicate

bool isDuplicate(const QString &name) { return name.endsWith(QLatin1Char('D')); }

QString pointTag(QString n)
{
    n = n.trimmed();
    if (n.endsWith(QLatin1String(".0"))) n.chop(2);
    return n;
}

}  // namespace

QString stepTitle(int step)
{
    switch (step) {
    case StepStation:  return tr("Station");
    case StepLines:    return tr("Lines");
    case StepTags:     return tr("Tags (RFID)");
    case StepPoints:   return tr("Points");
    case StepSignals:  return tr("Signals");
    case StepCircuits: return tr("Track circuits");
    case StepLabels:   return tr("Labels");
    case StepRoutes:   return tr("Routes (KAVACH)");
    case StepCheck:    return tr("Check");
    }
    return {};
}

QString stepHelp(int step)
{
    switch (step) {
    case StepStation:
        return tr("The station's KAVACH ID and its centre in metres, on the same scale as the tag locations. "
                  "New station… builds one with its lines, tags, points and signals, ready to move to the surveyed locations.");
    case StepLines:
        return tr("Each track: line is where it is drawn (DM DN main, UM UP main, DL1, DL2, … the DN loops, UL1, UL2, … the UP "
                  "loops), name its label. tags lists its tags in order. Right-click the drawing for a new line.");
    case StepTags:
        return tr("Every RFID tag, as the loco reads it (page_x / page_y). Select one: the form beside the drawing sets its "
                  "location, TINs, placement and station; each change re-encodes it with a fresh CRC-30. Right-click a line "
                  "on the drawing for a new tag there; drag a tag to move it.");
    case StepPoints:
        return tr("Each turnout: it leaves line1 at location1 and reaches line2 at location2; tags are those on the turnout. "
                  "Right-click a line on the drawing for a new point there.");
    case StepSignals:
        return tr("Each signal stands at its foot tag. Its foot tag's placement says which way it faces: 1 Signal foot "
                  "(Nominal), 2 (Reverse), 6 both. Right-click a tag on the drawing for a new signal; right-click a signal for "
                  "its heads.");
    case StepCircuits:
        return tr("Each track circuit: cid (its number in the station's packets), name, line, from startx to endx (m), or "
                  "a location only; station is its station ID. Double-click one on the drawing to show it occupied.");
    case StepLabels:
        return tr("Free text on the drawing at a location (m); posy is its height.");
    case StepRoutes:
        return tr("The station's Table of Control: each route from its entry signal to its exit signal, its tags in the order "
                  "the loco reads them, points and track circuits, and its track profile. Packet Maker answers a loco "
                  "with them.");
    case StepCheck:
        return tr("Everything the steps above report, in one list. Click a problem to go to its row.");
    }
    return {};
}

bool stepOptional(int step) { return step == StepCircuits || step == StepLabels; }

int sheetOfStep(int step)
{
    switch (step) {
    case StepStation:  return StationLayout::SheetStation;
    case StepLines:    return StationLayout::SheetLines;
    case StepTags:     return StationLayout::SheetTags;
    case StepPoints:   return StationLayout::SheetPoints;
    case StepSignals:  return StationLayout::SheetSignals;
    case StepCircuits: return StationLayout::SheetCircuits;
    case StepLabels:   return StationLayout::SheetTexts;
    }
    return StationLayout::SheetNone;
}

int stepOfSheet(int sheet)
{
    for (int s = 0; s < StepCount; ++s)
        if (sheet != StationLayout::SheetNone && sheetOfStep(s) == sheet) return s;
    return -1;
}

QVector<StepState> steps(const StationLayout::Layout &l, int routeCount, const QStringList &kavachNotes)
{
    using namespace StationLayout;
    QVector<StepState> out(StepCount);
    out[StepStation].count = l.stations.size();
    out[StepLines].count = l.lines.size();
    out[StepTags].count = l.tags.size();
    out[StepPoints].count = l.points.size();
    out[StepSignals].count = l.signalList.size();
    out[StepLabels].count = l.texts.size();
    out[StepRoutes].count = routeCount;

    for (const Problem &p : problems(l)) {
        const int s = stepOfSheet(p.sheet);
        if (s >= 0) out[s].problems << p;
    }
    if (l.stations.isEmpty()) out[StepStation].problems.append({ SheetStation, -1, tr("no station row: give the station's ID and its centre") });
    if (l.lines.isEmpty()) out[StepLines].problems.append({ SheetLines, -1, tr("no lines yet: DM (DN main) and UM (UP main) first") });

    // A main tag on no line and no point is drawn under "(no line)".
    QSet<QString> placed;
    for (const Line &ln : l.lines) for (const QString &t : ln.tags) placed.insert(t);
    for (const Point &p : l.points) for (const QString &t : p.tags.split(QLatin1Char(','), Qt::SkipEmptyParts)) placed.insert(pointTag(t));
    for (int i = 0; i < l.tags.size(); ++i) {
        const QString &n = l.tags.at(i).name;
        if (!isDuplicate(n) && !placed.contains(n))
            out[StepTags].problems.append({ SheetTags, i, tr("tag %1 is on no line or point: drawn under (no line)").arg(n) });
    }
    // A signal's foot tag says which way the signal faces.
    QHash<QString, TagInfo> infoOf;
    for (const Tag &t : l.tags) if (!infoOf.contains(t.name)) infoOf.insert(t.name, info(t));
    for (int i = 0; i < l.signalList.size(); ++i) {
        const Signal &sg = l.signalList.at(i);
        const TagInfo in = infoOf.value(sg.footTag);
        if (sg.route.isEmpty() && in.ok && in.type == 9 && in.placement != 1 && in.placement != 2 && in.placement != 6)
            out[StepSignals].problems.append({ SheetSignals, i, tr("signal %1: its foot tag %2 is placed %3 (%4), not at a signal foot")
                                                                .arg(sg.name, sg.footTag).arg(in.placement)
                                                                .arg(RfidTag::enumLabel(QStringLiteral("rfidPlace"), in.placement)) });
    }
    // Track circuits: the sheet's rows; one not drawn, or on a line the lines sheet lacks.
    QSet<QString> lineIds;
    for (const Line &ln : l.lines) lineIds.insert(ln.line);
    for (const XlsxBook::Sheet &sh : l.otherSheets)
        if (sh.name.compare(QLatin1String("circuits"), Qt::CaseInsensitive) == 0) out[StepCircuits].count += qMax(0, int(sh.rows.size()) - 1);
    QStringList notes;
    const QVector<Circuit> cs = circuits(l, &notes);
    for (const QString &n : notes) out[StepCircuits].problems.append({ SheetCircuits, -1, n });
    for (const Circuit &c : cs)
        if (!lineIds.contains(c.line)) out[StepCircuits].problems.append({ SheetCircuits, -1, tr("circuit %1: line %2 is not in the lines sheet").arg(c.name, c.line) });
    for (const QString &n : kavachNotes) out[StepRoutes].problems.append({ SheetNone, -1, n });

    for (int s = 0; s < StepCheck; ++s) out[StepCheck].problems += out[s].problems;
    out[StepCheck].count = out[StepCheck].problems.size();
    return out;
}

int tagsNeeded(const Plan &p) { return 2 * 6 + 2 * (p.downLoops + p.upLoops); }
int tinsNeeded(const Plan &p) { return 2 * 3 + p.downLoops + p.upLoops; }

namespace {
qint64 half(const Plan &p) { return p.loopLength / 2 + kTurnout; }
}

qint64 firstLocation(const Plan &p) { return p.centre - half(p) - kApproach - kPair; }
qint64 lastLocation(const Plan &p) { return p.centre + half(p) + kApproach + kPair; }

StationLayout::Layout newStation(const Plan &p, QString *err)
{
    using namespace StationLayout;
    auto refuse = [err](const QString &why) {
        if (err) *err = why;
        return Layout();
    };
    if (p.stationId < 1 || p.stationId > 65535) return refuse(tr("station ID %1: 1 to 65535").arg(p.stationId));
    if (p.downLoops < 0 || p.upLoops < 0 || p.downLoops > 9 || p.upLoops > 9) return refuse(tr("0 to 9 loops a side"));
    if (p.loopLength < 200 || p.loopLength > 5000) return refuse(tr("loop length %1 m: 200 to 5000 m").arg(p.loopLength));
    if (firstLocation(p) < 0 || lastLocation(p) > kMaxLocation)
        return refuse(tr("the station would run from %1 m to %2 m: tag locations are 0 to %3 m")
                          .arg(firstLocation(p)).arg(lastLocation(p)).arg(kMaxLocation));
    if (p.firstTag < 1 || p.firstTag + tagsNeeded(p) - 1 > 1023)
        return refuse(tr("tags %1 to %2: a unique id is 1 to 1023").arg(p.firstTag).arg(p.firstTag + tagsNeeded(p) - 1));
    if (p.firstTin < 1 || p.firstTin + tinsNeeded(p) - 1 > 255)
        return refuse(tr("TINs %1 to %2: a TIN is 1 to 255").arg(p.firstTin).arg(p.firstTin + tinsNeeded(p) - 1));

    Layout l;
    l.stations << Station{ p.stationId, p.centre };
    int nextId = p.firstTag, nextTin = p.firstTin, nextSignal = 1, nextPoint = 1;
    QHash<QString, QVector<QPair<qint64, QString>>> onLine;   // line -> (location, tag)
    QString why;

    // A main tag and its duplicate on `line`, run `sign` (+1 Nominal, -1 Reverse). `ahead` / `behind`:
    // the TIN of the track the tag leads onto the way the line is run, and the other way.
    auto addPair = [&](const QString &line, int sign, qint64 loc, int ahead, int behind, int placement) {
        const int id = nextId++;
        QString main;
        for (int dup : { 0, 1 }) {
            const QHash<QString, qint64> v{
                { QStringLiteral("type"), 9 }, { QStringLiteral("version"), 1 }, { QStringLiteral("unique"), id },
                { QStringLiteral("abs_loc"), loc + dup * sign * kPair },
                { QStringLiteral("tin_nom"), sign > 0 ? ahead : behind }, { QStringLiteral("tin_rev"), sign > 0 ? behind : ahead },
                { QStringLiteral("stn_nom"), p.stationId }, { QStringLiteral("stn_rev"), p.stationId },
                { QStringLiteral("sect_nom"), 0 }, { QStringLiteral("sect_rev"), 0 },
                { QStringLiteral("placement"), placement }, { QStringLiteral("duplication"), dup },
                { QStringLiteral("comm_nom"), 0 }, { QStringLiteral("comm_rev"), 0 } };
            QString e;
            const QByteArray b = RfidTag::build(v, &e);
            if (b.isEmpty()) { if (why.isEmpty()) why = tr("tag %1: %2").arg(id).arg(e); continue; }
            const QString name = RfidTag::nameOf(b);
            l.tags << Tag{ name, RfidTag::pageX(b), RfidTag::pageY(b) };
            onLine[line].append({ loc + dup * sign * kPair, name });
            if (!dup) main = name;
        }
        return main;
    };
    auto addSignal = [&](const QString &foot, bool home) {
        Signal s;
        s.footTag = foot;
        s.name = QStringLiteral("S%1").arg(nextSignal++);
        s.sigId = home ? 13 : 3;   // the Python tool's sig_id: 3 aspects, 13 with calling-on
        s.stationId = p.stationId;
        s.aspects = 3;
        s.callingOn = home;
        l.signalList << s;
    };

    const qint64 from = p.centre - half(p), to = p.centre + half(p);
    struct Main { QString line, name, loopPrefix, loopName; int sign, loops; };
    const Main mains[] = {
        { QStringLiteral("DM"), QStringLiteral("DN MAIN"), QStringLiteral("DL"), QStringLiteral("DN LOOP %1"), 1, p.downLoops },
        { QStringLiteral("UM"), QStringLiteral("UP MAIN"), QStringLiteral("UL"), QStringLiteral("UP LOOP %1"), -1, p.upLoops },
    };
    QVector<Line> lines;
    for (const Main &m : mains) {
        const int sign = m.sign;
        const qint64 entry = sign > 0 ? from : to, exit = sign > 0 ? to : from;
        const int foot = sign > 0 ? 1 : 2;   // Signal foot (Nominal) / (Reverse)
        const int before = nextTin++, yard = nextTin++, after = nextTin++;
        lines << Line{ m.name, m.line, {} };
        addPair(m.line, sign, entry - sign * kApproach, before, before, 0);
        addSignal(addPair(m.line, sign, entry - sign * kHome, yard, before, foot), true);
        addPair(m.line, sign, p.centre, yard, yard, 0);
        addSignal(addPair(m.line, sign, exit - sign * kStarter, yard, yard, foot), false);
        addPair(m.line, sign, exit + sign * kExit, after, yard, 0);
        addPair(m.line, sign, exit + sign * kApproach, after, after, 0);
        for (int k = 1; k <= m.loops; ++k) {
            const QString loop = m.loopPrefix + QString::number(k);
            const int tin = nextTin++;
            lines << Line{ m.loopName.arg(k), loop, {} };
            l.points << Point{ QStringLiteral("P%1").arg(nextPoint++), entry, m.line, entry + sign * kTurnout, loop, QString(), p.stationId, {}, {}, {} };
            l.points << Point{ QStringLiteral("P%1").arg(nextPoint++), exit - sign * kTurnout, loop, exit, m.line, QString(), p.stationId, {}, {}, {} };
            addPair(loop, sign, entry + sign * kStarter, tin, tin, 0);
            addSignal(addPair(loop, sign, exit - sign * kStarter, tin, tin, foot), false);
        }
    }
    if (!why.isEmpty()) return refuse(why);
    for (Line &ln : lines) {
        QVector<QPair<qint64, QString>> at = onLine.value(ln.line);
        std::stable_sort(at.begin(), at.end(), [](const QPair<qint64, QString> &a, const QPair<qint64, QString> &b) { return a.first < b.first; });
        for (const auto &x : at) ln.tags << x.second;
    }
    l.lines = lines;
    return l;
}

}  // namespace StationBuild
