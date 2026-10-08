#include "lccheck.h"

#include "capturedecoder.h"
#include "logmodel.h"

#include <QDateTime>

namespace LcCheck {
namespace {

QString rowValue(const QVector<FieldRow> &rows, const char *name)
{
    for (const FieldRow &r : rows)
        if (r.field.trimmed() == QLatin1String(name)) return r.value.trimmed();
    return QString();
}

// "5 horn1_solenoid_no" style DIO rows: the value of the field ending `name`.
QString dioValue(const QVector<FieldRow> &rows, const char *name)
{
    for (const FieldRow &r : rows)
        if (r.field.trimmed().endsWith(QLatin1String(name))) return r.value.trimmed();
    return QString();
}

}  // namespace

QVector<Approach> build(const LogModel *model, qint64 fromMs, qint64 toMs)
{
    QVector<Approach> out;
    if (!model) return out;

    // Pass 1: the horn samples and their usual value; the approaches.
    QVector<QPair<qint64, QString>> h1, h2;
    int ones1 = 0, ones2 = 0;
    for (int i = 0; i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        if ((fromMs > 0 && e->epochMs < fromMs) || (toMs > 0 && e->epochMs > toMs)) continue;
        const bool dmi = e->text.startsWith(QLatin1String("@dmi_"));
        const bool dip = e->text.startsWith(QLatin1String("@dip1_"));
        if (!dmi && !dip) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(e->text);
        if (!cap.valid) continue;
        const QVector<FieldRow> rows = CaptureDecoder::describe(cap);
        if (dip) {
            const QString a = dioValue(rows, "horn1_solenoid_no"), b = dioValue(rows, "horn2_solenoid_no");
            if (!a.isEmpty()) { h1 << qMakePair(e->epochMs, a); ones1 += a == QLatin1String("1") ? 1 : 0; }
            if (!b.isEmpty()) { h2 << qMakePair(e->epochMs, b); ones2 += b == QLatin1String("1") ? 1 : 0; }
            continue;
        }
        const QString lc = rowValue(rows, "lc.id");
        if (lc.isEmpty() || lc.startsWith(QLatin1String("0 "))) continue;
        const int dist = rowValue(rows, "lc.distance").section(QLatin1Char(' '), 0, 0).toInt();
        if (out.isEmpty() || out.last().lc != lc || e->epochMs - out.last().toMs > 30000) {
            Approach a;
            a.lc = lc;
            a.manning = rowValue(rows, "lc.manning");
            a.autoWhistle = rowValue(rows, "lc.auto_whistle");
            a.fromMs = a.toMs = e->epochMs;
            a.fromDistM = a.toDistM = dist;
            out << a;
        } else {
            out.last().toMs = e->epochMs;
            out.last().toDistM = dist;
        }
    }
    const QString usual1 = ones1 * 2 >= h1.size() ? QStringLiteral("1") : QStringLiteral("0");
    const QString usual2 = ones2 * 2 >= h2.size() ? QStringLiteral("1") : QStringLiteral("0");

    // Pass 2: horn spells inside each approach (either solenoid off its usual value).
    for (Approach &a : out) {
        bool open = false;
        int j2 = 0;
        for (int j = 0; j < h1.size(); ++j) {
            const qint64 ms = h1.at(j).first;
            if (ms < a.fromMs) continue;
            if (ms > a.toMs) break;
            a.dioSeen = true;
            while (j2 + 1 < h2.size() && h2.at(j2 + 1).first <= ms) ++j2;
            const bool on = h1.at(j).second != usual1 || (j2 < h2.size() && h2.at(j2).second != usual2);
            if (on && !open) { a.horn << qMakePair(ms, ms); open = true; }
            else if (on) a.horn.last().second = ms;
            else open = false;
        }
    }
    return out;
}

QString toHtml(const QVector<Approach> &a, int maxRows)
{
    auto hms = [](qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")); };
    QString h = QStringLiteral("<h2>Level crossings approached (DMI lc.id): %1</h2>").arg(a.size());
    if (a.isEmpty()) return h;
    h += QStringLiteral("<p>The horn is the DIO horn solenoid feedback (@dip1) reading other than its usual value.</p>"
                        "<table><tr><th>LC</th><th></th><th>Auto whistle</th><th>From</th><th>To</th><th>Distance shown</th>"
                        "<th>Horn during the approach</th></tr>");
    for (int i = 0; i < a.size() && i < maxRows; ++i) {
        const Approach &x = a.at(i);
        QStringList horn;
        for (const auto &s : x.horn) horn << (s.first == s.second ? hms(s.first) : hms(s.first) + QStringLiteral("–") + hms(s.second));
        const QString hornText = !x.dioSeen ? QStringLiteral("not known (no @dip1)")
                               : horn.isEmpty() ? QStringLiteral("none") : horn.join(QStringLiteral(", "));
        h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6 → %7 m</td><td>%8</td></tr>")
                 .arg(x.lc.toHtmlEscaped(), x.manning.toHtmlEscaped(), x.autoWhistle.toHtmlEscaped(), hms(x.fromMs), hms(x.toMs))
                 .arg(x.fromDistM).arg(x.toDistM).arg(hornText);
    }
    h += QStringLiteral("</table>");
    return h;
}

}  // namespace LcCheck
