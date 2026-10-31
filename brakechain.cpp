#include "brakechain.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "runreport.h"

#include <QDateTime>
#include <QHash>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace BrakeChain {
namespace {

constexpr double kKgPerBit = 0.05;   // every @biu_* pressure (biuprotocol.h)

struct DmiRow { qint64 ms; int row; int brake; QString type; double speed; bool hasSpeed; };
struct Cmd { qint64 ms; int row; bool a9Valid, sa9Valid, isolate; int a9, sa9; };
struct Air { qint64 ms; int row; double bp, bc; bool bpOk, bcOk; QString card; };

bool sameCommand(const Cmd &a, const Cmd &b)
{
    return a.a9Valid == b.a9Valid && a.sa9Valid == b.sa9Valid && a.isolate == b.isolate
           && (!a.a9Valid || a.a9 == b.a9) && (!a.sa9Valid || a.sa9 == b.sa9);
}

QString kg(double v) { return QString::number(v, 'f', 2); }

QString commandText(const Cmd *before, const Cmd &c)
{
    auto one = [](bool valid, int raw) { return valid ? kg(raw * kKgPerBit) : QStringLiteral("not valid"); };
    QStringList parts;
    if (!before || before->a9Valid != c.a9Valid || before->a9 != c.a9)
        parts << (before ? QStringLiteral("A9 %1 → %2").arg(one(before->a9Valid, before->a9), one(c.a9Valid, c.a9))
                         : QStringLiteral("A9 %1").arg(one(c.a9Valid, c.a9)));
    if (!before || before->sa9Valid != c.sa9Valid || before->sa9 != c.sa9)
        parts << (before ? QStringLiteral("SA9 %1 → %2").arg(one(before->sa9Valid, before->sa9), one(c.sa9Valid, c.sa9))
                         : QStringLiteral("SA9 %1").arg(one(c.sa9Valid, c.sa9)));
    if (before && before->isolate != c.isolate) parts << (c.isolate ? QStringLiteral("BIU isolated") : QStringLiteral("BIU not isolated"));
    return parts.join(QStringLiteral(", ")) + QStringLiteral(" kg/cm²");
}

// The first change in [lo, hi), with any others logged at the same instant.
Link firstChange(const QVector<Change> &changes, qint64 lo, qint64 hi)
{
    Link l;
    QStringList same;
    for (const Change &c : changes) {
        if (c.ms < lo) continue;
        if (c.ms >= hi) break;
        if (!l.seen()) { l.ms = c.ms; l.row = c.row; }
        if (c.ms != l.ms) break;
        same << c.what;
    }
    l.what = same.join(QStringLiteral("; "));
    return l;
}

// Pressure links over one source's samples: where it stood before `lo`,
// the first move of kMoveKg, then the first sample at the targets.
void airLinks(const QVector<Air> &air, qint64 lo, qint64 hi, const Cmd *target, qint64 targetFrom,
              Link *moves, Link *atTarget, const QString &label)
{
    const Air *base = nullptr;
    for (const Air &a : air) {
        if (a.ms >= lo) break;
        base = &a;
    }
    for (const Air &a : air) {
        if (a.ms < lo) continue;
        if (a.ms >= hi) break;
        if (!moves->seen() && base) {
            const bool bpMoved = a.bpOk && base->bpOk && std::fabs(a.bp - base->bp) >= kMoveKg - 1e-9;
            const bool bcMoved = a.bcOk && base->bcOk && std::fabs(a.bc - base->bc) >= kMoveKg - 1e-9;
            if (bpMoved || bcMoved) {
                moves->ms = a.ms;
                moves->row = a.row;
                QStringList w;
                if (bpMoved) w << QStringLiteral("BP %1 → %2").arg(kg(base->bp), kg(a.bp));
                if (bcMoved) w << QStringLiteral("BC %1 → %2").arg(kg(base->bc), kg(a.bc));
                moves->what = QStringLiteral("%1 %2 kg/cm²").arg(label.isEmpty() ? a.card : label, w.join(QStringLiteral(", ")));
            }
        }
        if (!atTarget->seen() && target && a.ms >= targetFrom && (target->a9Valid || target->sa9Valid)) {
            const bool bpAt = !target->a9Valid || (a.bpOk && std::fabs(a.bp - target->a9 * kKgPerBit) <= kAtTargetKg + 1e-9);
            const bool bcAt = !target->sa9Valid || (a.bcOk && std::fabs(a.bc - target->sa9 * kKgPerBit) <= kAtTargetKg + 1e-9);
            if (bpAt && bcAt) {
                atTarget->ms = a.ms;
                atTarget->row = a.row;
                QStringList w;
                if (target->a9Valid) w << QStringLiteral("BP %1 (A9 %2)").arg(kg(a.bp), kg(target->a9 * kKgPerBit));
                if (target->sa9Valid) w << QStringLiteral("BC %1 (SA9 %2)").arg(kg(a.bc), kg(target->sa9 * kKgPerBit));
                atTarget->what = QStringLiteral("%1 %2 kg/cm²").arg(label.isEmpty() ? a.card : label, w.join(QStringLiteral(", ")));
            }
        }
    }
}

// The earlier of two links (either may be unseen).
Link earlier(const Link &a, const Link &b)
{
    if (!a.seen()) return b;
    if (!b.seen()) return a;
    return b.ms < a.ms ? b : a;
}

}  // namespace

QString stepName(int step)
{
    switch (step) {
    case Decision:       return QStringLiteral("KAVACH decides");
    case Output:         return QStringLiteral("Brake output");
    case RelayFeedback:  return QStringLiteral("Relay feedback");
    case BiuCommand:     return QStringLiteral("BIU command");
    case BiuMoves:       return QStringLiteral("BIU pressure moves");
    case BiuAtTarget:    return QStringLiteral("BIU at target");
    case AnalogMoves:    return QStringLiteral("Analog pressure moves");
    case AnalogAtTarget: return QStringLiteral("Analog at target");
    case SpeedFalls:     return QStringLiteral("Speed falls");
    case Standstill:     return QStringLiteral("Standstill");
    default:             return QString();
    }
}

QString seriesName(int id)
{
    switch (id) {
    case Speed:          return QStringLiteral("Speed (km/h)");
    case BiuBp:          return QStringLiteral("BIU BP");
    case BiuBc:          return QStringLiteral("BIU BC");
    case A9:             return QStringLiteral("A9 (BP target)");
    case Sa9:            return QStringLiteral("SA9 (BC target)");
    case AnalogTopBp:    return QStringLiteral("Analog top BP");
    case AnalogTopBc:    return QStringLiteral("Analog top BC");
    case AnalogBottomBp: return QStringLiteral("Analog bottom BP");
    case AnalogBottomBc: return QStringLiteral("Analog bottom BC");
    default:             return QString();
    }
}

Chain build(const LogModel *model)
{
    Chain c;
    if (!model || model->count() == 0) return c;

    QVector<DmiRow> dmi;
    QVector<Cmd> cmds;
    QVector<Air> biuAir, topAir, bottomAir;
    QHash<QString, QString> lastBit;          // "DOP1 emergency_brake" -> "1"
    static const QRegularExpression lead(QStringLiteral("^\\d+\\s+"));
    static const QStringList brakePins{ QStringLiteral("normal_brake"), QStringLiteral("full_service_brake"),
                                        QStringLiteral("emergency_brake"), QStringLiteral("light_engine_brake") };

    for (int i = 0; i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        if (!c.fromMs) c.fromMs = e->epochMs;
        c.toMs = qMax(c.toMs, e->epochMs);
        const QString &tx = e->text;
        if (!tx.startsWith(QLatin1Char('@'))) continue;
        const bool isDmi = tx.startsWith(QLatin1String("@dmi_"));
        const bool isDop = tx.startsWith(QLatin1String("@dop1_")) || tx.startsWith(QLatin1String("@dop2_"));
        const bool isDip = tx.startsWith(QLatin1String("@dip1_")) || tx.startsWith(QLatin1String("@dip2_"))
                           || tx.startsWith(QLatin1String("@dip_old_"));
        const bool isCmd = tx.startsWith(QLatin1String("@biu_cmd_"));
        const bool isPress = tx.startsWith(QLatin1String("@biu_press_"));
        const bool isTop = tx.startsWith(QLatin1String("@analog_top_"));
        const bool isBottom = tx.startsWith(QLatin1String("@analog_bottom_"));
        if (!(isDmi || isDop || isDip || isCmd || isPress || isTop || isBottom)) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(tx);
        if (!cap.valid || cap.bytes.isEmpty()) continue;
        QHash<QString, qint64> raw;
        const QVector<FieldRow> rows = CaptureDecoder::describe(cap, nullptr, 0, &raw);
        const qint64 ms = e->epochMs;

        if (isDmi) {
            if (!raw.contains(QStringLiteral("brake_type"))) continue;
            DmiRow d{ ms, i, int(raw.value(QStringLiteral("brake_type"))), QString(), 0, raw.contains(QStringLiteral("train_speed")) };
            if (d.hasSpeed) d.speed = double(raw.value(QStringLiteral("train_speed")));
            for (const FieldRow &r : rows)
                if (r.field.trimmed() == QLatin1String("brake_type")) { d.type = r.value.trimmed(); break; }
            dmi << d;
            if (d.hasSpeed) c.series[Speed] << Point{ ms, d.speed };
        } else if (isDop || isDip) {
            ++c.dioFrames;
            const QString group = isDop ? (tx.startsWith(QLatin1String("@dop1_")) ? QStringLiteral("DOP1") : QStringLiteral("DOP2"))
                                        : tx.startsWith(QLatin1String("@dip1_")) ? QStringLiteral("DIP1")
                                        : tx.startsWith(QLatin1String("@dip2_")) ? QStringLiteral("DIP2") : QStringLiteral("DIP_OLD");
            for (const FieldRow &r : rows) {
                QString name = r.field.trimmed();
                name.remove(lead);
                QString value = r.value.trimmed();
                if (isDop) {
                    if (!brakePins.contains(name)) continue;
                    const int at = value.indexOf(QLatin1String("output="));
                    if (at < 0) continue;
                    value = value.mid(at + 7, 1);
                } else if (!name.contains(QLatin1String("relay_fb")) && !name.endsWith(QLatin1String("_fdbk"))) {
                    continue;
                }
                if (value != QLatin1String("0") && value != QLatin1String("1")) continue;
                const QString key = group + QLatin1Char(' ') + name;
                const auto was = lastBit.constFind(key);
                if (was != lastBit.constEnd() && *was != value) {
                    const QString what = isDop ? QStringLiteral("%1 output %2 → %3").arg(key, *was, value)
                                               : QStringLiteral("%1 %2 → %3").arg(key, *was, value);
                    (isDop ? c.outputs : c.relays) << Change{ ms, i, what };
                }
                lastBit.insert(key, value);
            }
        } else if (isCmd) {
            if (!raw.contains(QStringLiteral("a9_bp_target"))) continue;
            ++c.biuCommands;
            Cmd k{ ms, i, raw.value(QStringLiteral("a9_valid")) != 0, raw.value(QStringLiteral("sa9_valid")) != 0,
                   raw.value(QStringLiteral("biu_isolate")) != 0, int(raw.value(QStringLiteral("a9_bp_target"))),
                   int(raw.value(QStringLiteral("sa9_bc_target"))) };
            cmds << k;
            if (k.a9Valid) c.series[A9] << Point{ ms, k.a9 * kKgPerBit };
            if (k.sa9Valid) c.series[Sa9] << Point{ ms, k.sa9 * kKgPerBit };
        } else if (isPress) {
            if (!raw.contains(QStringLiteral("bp"))) continue;
            ++c.biuPressures;
            Air a{ ms, i, raw.value(QStringLiteral("bp")) * kKgPerBit, raw.value(QStringLiteral("bc")) * kKgPerBit,
                   raw.value(QStringLiteral("bp_valid")) != 0, raw.value(QStringLiteral("bc_valid")) != 0, QStringLiteral("BIU") };
            biuAir << a;
            if (a.bpOk) c.series[BiuBp] << Point{ ms, a.bp };
            if (a.bcOk) c.series[BiuBc] << Point{ ms, a.bc };
        } else {
            // Floats: the decoder's %g text (the schema has no integer for them).
            double ch[2] = { 0, 0 };
            bool ok[2] = { false, false };
            for (const FieldRow &r : rows) {
                const QString f = r.field.trimmed();
                const int k = f == QLatin1String("channel1_data") ? 0 : f == QLatin1String("channel2_data") ? 1 : -1;
                if (k >= 0) ch[k] = r.value.trimmed().toDouble(&ok[k]);
            }
            if (!ok[0] && !ok[1]) continue;
            ++c.analogFrames;
            Air a{ ms, i, ch[0], ch[1], ok[0], ok[1], isTop ? QStringLiteral("analog top") : QStringLiteral("analog bottom") };
            (isTop ? topAir : bottomAir) << a;
            if (a.bpOk) c.series[isTop ? AnalogTopBp : AnalogBottomBp] << Point{ ms, a.bp };
            if (a.bcOk) c.series[isTop ? AnalogTopBc : AnalogBottomBc] << Point{ ms, a.bc };
        }
    }

    // In time order: a tab's serial and UDP lines may interleave out of order.
    auto byMs = [](const auto &x, const auto &y) { return x.ms < y.ms; };
    std::stable_sort(dmi.begin(), dmi.end(), byMs);
    std::stable_sort(cmds.begin(), cmds.end(), byMs);
    for (QVector<Air> *v : { &biuAir, &topAir, &bottomAir }) std::stable_sort(v->begin(), v->end(), byMs);
    std::stable_sort(c.outputs.begin(), c.outputs.end(), byMs);
    std::stable_sort(c.relays.begin(), c.relays.end(), byMs);
    for (QVector<Point> &v : c.series) std::stable_sort(v.begin(), v.end(), byMs);

    // Applications: brake_type turning to 2-5, or to another of them.
    int prev = -1;
    for (const DmiRow &d : dmi) {
        const bool braking = d.brake >= 2 && d.brake <= 5;
        if (braking && d.brake != prev) {
            Application a;
            a.fromMs = a.toMs = d.ms;
            a.row = d.row;
            a.brakeType = d.brake;
            a.type = d.type;
            c.apps << a;
        } else if (braking && !c.apps.isEmpty()) {
            c.apps.last().toMs = d.ms;
        }
        prev = d.brake;
    }
    if (c.apps.isEmpty()) return c;

    // Reasons: session 175's, for the EB / FSB spans that hold an onset.
    const QVector<RunReport::BrakeEvent> events = RunReport::brakeEvents(model);
    for (Application &a : c.apps)
        for (const RunReport::BrakeEvent &ev : events)
            if (a.fromMs >= ev.fromMs && a.fromMs <= ev.toMs)
                for (const QString &r : ev.reasons) if (!a.reasons.contains(r)) a.reasons << r;

    for (int n = 0; n < c.apps.size(); ++n) {
        Application &a = c.apps[n];
        const qint64 lo = n > 0 ? qMax(a.fromMs - kLeadMs, c.apps.at(n - 1).fromMs + 1) : a.fromMs - kLeadMs;
        const qint64 hi = qMin(a.fromMs + kFollowMs, n + 1 < c.apps.size() ? c.apps.at(n + 1).fromMs : a.fromMs + kFollowMs);

        a.links[Decision] = Link{ a.fromMs, a.row, a.type + (a.reasons.isEmpty() ? QString() : QStringLiteral(": ") + a.reasons.join(QStringLiteral("; "))) };
        a.links[Output] = firstChange(c.outputs, lo, hi);
        a.links[RelayFeedback] = firstChange(c.relays, lo, hi);

        // The command that differs from the one standing before the window.
        const Cmd *base = nullptr;
        const Cmd *changed = nullptr;
        for (const Cmd &k : cmds) {
            if (k.ms < lo) { base = &k; continue; }
            if (k.ms >= hi) break;
            if (!base ? (k.a9Valid || k.sa9Valid || k.isolate) : !sameCommand(*base, k)) { changed = &k; break; }
        }
        if (changed) a.links[BiuCommand] = Link{ changed->ms, changed->row, commandText(base, *changed) };
        airLinks(biuAir, lo, hi, changed, changed ? changed->ms : 0, &a.links[BiuMoves], &a.links[BiuAtTarget], QStringLiteral("BIU"));
        Link topMoves, topAt, bottomMoves, bottomAt;
        airLinks(topAir, lo, hi, changed, changed ? changed->ms : 0, &topMoves, &topAt, QString());
        airLinks(bottomAir, lo, hi, changed, changed ? changed->ms : 0, &bottomMoves, &bottomAt, QString());
        a.links[AnalogMoves] = earlier(topMoves, bottomMoves);
        a.links[AnalogAtTarget] = earlier(topAt, bottomAt);

        // Speed: the onset's, then the first 1 km/h below it, then 0.
        for (const DmiRow &d : dmi) {
            if (d.ms > a.fromMs) break;
            if (d.hasSpeed) a.speedAtOnset = d.speed;
        }
        if (a.speedAtOnset > 0) {
            const qint64 end = a.fromMs + kFollowMs;
            for (const DmiRow &d : dmi) {
                if (d.ms <= a.fromMs || !d.hasSpeed) continue;
                if (d.ms > end) break;
                if (!a.links[SpeedFalls].seen() && d.speed <= a.speedAtOnset - 1)
                    a.links[SpeedFalls] = Link{ d.ms, d.row, QStringLiteral("%1 → %2 km/h").arg(a.speedAtOnset).arg(d.speed) };
                if (d.speed == 0) {
                    a.links[Standstill] = Link{ d.ms, d.row, QStringLiteral("0 km/h") };
                    break;
                }
            }
        }
    }
    return c;
}

QString delayText(qint64 ms)
{
    const QString sign = ms < 0 ? QStringLiteral("−") : QStringLiteral("+");
    const qint64 a = ms < 0 ? -ms : ms;
    return a % 1000 == 0 ? QStringLiteral("%1%2 s").arg(sign).arg(a / 1000)
                         : QStringLiteral("%1%2 s").arg(sign).arg(a / 1000.0, 0, 'f', a % 100 == 0 ? 1 : 3);
}

QString toCsv(const Chain &c)
{
    auto field = [](QString s) {
        if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"'))) s = QLatin1Char('"') + s.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
        return s;
    };
    QStringList head{ QStringLiteral("onset"), QStringLiteral("brake"), QStringLiteral("reasons"), QStringLiteral("speed at onset km/h") };
    for (int s = Output; s < StepCount; ++s) head << stepName(s) + QStringLiteral(" s");
    QString out = head.join(QLatin1Char(',')) + QLatin1Char('\n');
    for (const Application &a : c.apps) {
        QStringList row{ QDateTime::fromMSecsSinceEpoch(a.fromMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                         field(a.type), field(a.reasons.join(QStringLiteral("; "))),
                         a.speedAtOnset < 0 ? QString() : QString::number(a.speedAtOnset) };
        for (int s = Output; s < StepCount; ++s)
            row << (a.links[s].seen() ? QString::number(a.delayMs(s) / 1000.0, 'f', 3) : QString());
        out += row.join(QLatin1Char(',')) + QLatin1Char('\n');
    }
    return out;
}

}  // namespace BrakeChain
