#include "radiohealth.h"

#include "capturedecoder.h"
#include "fieldplot.h"
#include "logmodel.h"

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <algorithm>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace RadioHealth {
namespace {

// The number a display value starts with ("36 °C", "1.0 W", "-3"); NaN if none.
double leadingNumber(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(-?\\d+(?:\\.\\d+)?)"));
    const QRegularExpressionMatch m = re.match(text);
    return m.hasMatch() ? m.captured(1).toDouble() : std::numeric_limits<double>::quiet_NaN();
}

// Spells of a condition sampled over time: a sample with it on extends the
// open spell (or opens one); a sample with it off, or a silence longer than
// kSpellGapMs, closes it.
struct SpanTracker {
    QVector<Span> *out = nullptr;
    bool open = false;
    qint64 lastMs = 0;
    void observe(bool on, qint64 ms, const QString &what)
    {
        if (open && ms - lastMs > kSpellGapMs) open = false;
        if (on) {
            if (!open) { out->append(Span{ ms, ms, what, false }); open = true; }
            else out->last().toMs = ms;
        } else {
            open = false;
        }
        lastMs = ms;
    }
};

QString dur(qint64 ms)
{
    const qint64 s = ms / 1000;
    if (s < 60) return QStringLiteral("%1 s").arg(s);
    if (s < 3600) return QStringLiteral("%1 min %2 s").arg(s / 60).arg(s % 60);
    return QStringLiteral("%1 h %2 min").arg(s / 3600).arg((s % 3600) / 60);
}

void sortSpans(QVector<Span> &v)
{
    std::sort(v.begin(), v.end(), [](const Span &a, const Span &b) { return a.fromMs < b.fromMs; });
}

}  // namespace

Report build(const LogModel *model, qint64 fromMs, qint64 toMs)
{
    Report r;
    if (!model || model->count() == 0) return r;

    // ---- DMI -----------------------------------------------------------------------------
    r.signal.name = QStringLiteral("DMI signal");
    r.signal.unit = QStringLiteral("bars");
    {
        bool capped = false;
        const QVector<RowFields> rows = collectRowFields(model, QStringLiteral("dmi"),
            { QStringLiteral("signal_strength"), QStringLiteral("alarm_code") }, 200000, fromMs, toMs, &capped);
        SpanTracker none{ &r.noRadio }, hole{ &r.radioHole };
        for (const RowFields &f : rows) {
            if (!f.has(QStringLiteral("signal_strength"))) continue;
            const qint64 bars = f.raw.value(QStringLiteral("signal_strength"));
            r.signal.ms << f.epochMs;
            r.signal.v << double(qMin<qint64>(bars, 5));
            none.observe(bars == 0, f.epochMs, QStringLiteral("no radio (signal_strength 0)"));
            hole.observe(f.display.value(QStringLiteral("alarm_code")).contains(QLatin1String("Approaching Radio Hole")),
                         f.epochMs, QStringLiteral("Approaching Radio Hole"));
        }
    }

    // ---- CCSYS ---------------------------------------------------------------------------
    {
        const QStringList fields{ QStringLiteral("active_radio"), QStringLiteral("health_radio1"), QStringLiteral("health_radio2"),
                                  QStringLiteral("radio1_temp"), QStringLiteral("radio2_temp"),
                                  QStringLiteral("radio1_pa_temp"), QStringLiteral("radio2_pa_temp"),
                                  QStringLiteral("radio1_forward_power"), QStringLiteral("radio2_forward_power") };
        const QVector<RowFields> rows = collectRowFields(model, QStringLiteral("ccsys"), fields, 200000, fromMs, toMs);
        if (!rows.isEmpty()) {
            Series t1{ QStringLiteral("Radio 1"), QStringLiteral("°C"), {}, {} }, t2{ QStringLiteral("Radio 2"), QStringLiteral("°C"), {}, {} };
            Series p1{ QStringLiteral("PA 1"), QStringLiteral("°C"), {}, {} }, p2{ QStringLiteral("PA 2"), QStringLiteral("°C"), {}, {} };
            Series f1{ QStringLiteral("Radio 1"), QStringLiteral("W"), {}, {} }, f2{ QStringLiteral("Radio 2"), QStringLiteral("W"), {}, {} };
            SpanTracker fail1{ &r.radioFail }, fail2{ &r.radioFail }, noActive{ &r.radioFail };
            for (const RowFields &f : rows) {
                auto add = [&](Series &s, const char *field) {
                    if (!f.has(QLatin1String(field))) return;
                    s.ms << f.epochMs;
                    s.v << leadingNumber(f.display.value(QLatin1String(field)));
                };
                add(t1, "radio1_temp"); add(t2, "radio2_temp");
                add(p1, "radio1_pa_temp"); add(p2, "radio2_pa_temp");
                add(f1, "radio1_forward_power"); add(f2, "radio2_forward_power");
                fail1.observe(f.has(QStringLiteral("health_radio1")) && f.display.value(QStringLiteral("health_radio1")) != QLatin1String("OK"),
                              f.epochMs, QStringLiteral("Radio-1 not OK (CCSYS)"));
                fail2.observe(f.has(QStringLiteral("health_radio2")) && f.display.value(QStringLiteral("health_radio2")) != QLatin1String("OK"),
                              f.epochMs, QStringLiteral("Radio-2 not OK (CCSYS)"));
                noActive.observe(f.has(QStringLiteral("active_radio")) && f.raw.value(QStringLiteral("active_radio")) == 0,
                                 f.epochMs, QStringLiteral("no active radio (CCSYS)"));
            }
            r.temperatures = { t1, t2, p1, p2 };
            r.power = { f1, f2 };
        }
    }

    // ---- NMS health -----------------------------------------------------------------------
    // Its fields are events with meanings (eid): they come as display rows
    // ("3  (Radio Fail)") and not in the numeric map collectRowFields() keys
    // on, so this one is read directly.
    {
        SpanTracker f1{ &r.radioFail }, f2{ &r.radioFail };
        for (int i = 0; i < model->count(); ++i) {
            const LogEntryPtr e = model->entryAt(i);
            if (!e || !e->text.startsWith(QLatin1String("@nmshlth_"))) continue;
            if ((fromMs > 0 && e->epochMs < fromMs) || (toMs > 0 && e->epochMs > toMs)) continue;
            const QVector<FieldRow> rows = CaptureDecoder::describe(CaptureDecoder::parseLine(e->text));
            for (const FieldRow &row : rows) {
                const QString name = row.field.trimmed();
                if (name == QLatin1String("RADIO_1_HEALTH"))
                    f1.observe(leadingNumber(row.value) == 3, e->epochMs, QStringLiteral("Radio-1 Radio Fail (NMS)"));
                else if (name == QLatin1String("RADIO_2_HEALTH"))
                    f2.observe(leadingNumber(row.value) == 3, e->epochMs, QStringLiteral("Radio-2 Radio Fail (NMS)"));
            }
        }
    }
    sortSpans(r.radioFail);

    // ---- DLSYS: GSM -----------------------------------------------------------------------
    {
        const QVector<RowFields> rows = collectRowFields(model, QStringLiteral("dlsys"),
            { QStringLiteral("gsm1_rssi"), QStringLiteral("gsm2_rssi") }, 200000, fromMs, toMs);
        if (!rows.isEmpty()) {
            Series g1{ QStringLiteral("GSM-1"), QStringLiteral("RSSI"), {}, {} }, g2{ QStringLiteral("GSM-2"), QStringLiteral("RSSI"), {}, {} };
            for (const RowFields &f : rows) {
                auto add = [&](Series &s, const char *field) {
                    if (!f.has(QLatin1String(field))) return;
                    const qint64 v = f.raw.value(QLatin1String(field));
                    s.ms << f.epochMs;
                    s.v << (v == 99 ? std::numeric_limits<double>::quiet_NaN() : double(v));   // 99: not known
                };
                add(g1, "gsm1_rssi");
                add(g2, "gsm2_rssi");
            }
            r.gsm = { g1, g2 };
        }
    }

    // ---- extent, and each no-radio spell against the announcements ----------------------
    auto extend = [&r](qint64 ms) {
        if (!r.fromMs || ms < r.fromMs) r.fromMs = ms;
        r.toMs = qMax(r.toMs, ms);
    };
    for (const qint64 ms : r.signal.ms) extend(ms);
    for (const Series &s : r.temperatures) for (const qint64 ms : s.ms) extend(ms);
    for (const Series &s : r.gsm) for (const qint64 ms : s.ms) extend(ms);
    matchAnnouncements(r);
    return r;
}

void matchAnnouncements(Report &r)
{
    r.noRadioTotalMs = r.longestNoRadioMs = 0;
    r.announcedSpells = 0;
    for (Span &s : r.noRadio) {
        const qint64 len = s.toMs - s.fromMs;
        r.noRadioTotalMs += len;
        r.longestNoRadioMs = qMax(r.longestNoRadioMs, len);
        s.announced = false;
        for (const Span &h : r.radioHole) {
            if (h.toMs >= s.fromMs - kRadioHoleLeadMs && h.fromMs <= s.toMs) { s.announced = true; break; }
        }
        r.announcedSpells += s.announced ? 1 : 0;
    }
}

QString summaryText(const Report &r)
{
    if (!r.any()) return QObject::tr("No @dmi, @ccsys or @dlsys in this tab: nothing to show.");
    QStringList parts;
    if (r.signal.isEmpty()) {
        parts << QObject::tr("No @dmi: no signal bars.");
    } else if (r.noRadio.isEmpty()) {
        parts << QObject::tr("The DMI showed radio signal throughout.");
    } else {
        parts << QObject::tr("No radio on the DMI in %1 spell(s), %2 in all, the longest %3.")
                     .arg(r.noRadio.size()).arg(dur(r.noRadioTotalMs), dur(r.longestNoRadioMs));
        parts << QObject::tr("%1 of them followed a radio-hole announcement (within %2 s before, or during); %3 did not.")
                     .arg(r.announcedSpells).arg(kRadioHoleLeadMs / 1000).arg(r.noRadio.size() - r.announcedSpells);
    }
    if (!r.radioFail.isEmpty()) parts << QObject::tr("A radio reported not OK in %1 spell(s).").arg(r.radioFail.size());
    return parts.join(QLatin1Char(' '));
}

}  // namespace RadioHealth
