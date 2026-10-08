#include "faulttimeline.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "missionreport.h"

#include <QDateTime>
#include <QObject>
#include <QHash>
#include <QMap>
#include <QSet>

namespace FaultTimeline {
namespace {

QString hms(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")); }

}  // namespace

QVector<Bar> Timeline::activeAt(qint64 ms) const
{
    QVector<Bar> out;
    for (const Bar &b : bars)
        if (b.fromMs <= ms && ms <= b.toMs) out << b;
    return out;
}

Timeline build(const LogModel *model)
{
    Timeline t;
    if (!model || model->count() == 0) return t;
    auto addRow = [&t](const QString &row) { if (!t.rows.contains(row)) t.rows << row; };

    // NMS: per reporting subsystem, the faults it listed last.
    QHash<int, QSet<QString>> listed;            // subsystem -> keys
    QHash<QString, int> openNms;                 // key -> bar index
    // CCSYS: per element, the open bar.
    QHash<QString, int> openLcu;

    for (int i = 0; i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        const qint64 ms = e->epochMs;
        if (!t.fromMs) t.fromMs = ms;
        t.toMs = qMax(t.toMs, ms);
        const bool nms = e->text.startsWith(QLatin1String("@nmsflt_"));
        const bool cc = e->text.startsWith(QLatin1String("@ccsys_"));
        if (!nms && !cc) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(e->text);
        if (!cap.valid) continue;

        if (nms) {
            const QVector<ActiveFaultInfo> now = CaptureDecoder::faultsOf(cap);
            // The subsystem this frame speaks for (an empty list clears its faults).
            QHash<QString, qint64> raw;
            CaptureDecoder::describe(cap, nullptr, 0, &raw);
            const int sub = raw.contains(QStringLiteral("reporting_subsystem")) ? int(raw.value(QStringLiteral("reporting_subsystem")))
                          : now.isEmpty() ? -1 : now.first().subsystem;
            if (sub < 0) continue;
            QSet<QString> keys;
            for (const ActiveFaultInfo &f : now) {
                const QString key = QStringLiteral("%1:%2:%3:%4").arg(f.subsystem).arg(f.moduleId).arg(f.codeType).arg(f.faultId);
                keys.insert(key);
                if (!openNms.contains(key)) {
                    const QString row = f.moduleName.isEmpty() ? QStringLiteral("module %1").arg(f.moduleId) : f.moduleName;
                    addRow(row);
                    t.bars << Bar{ row, f.faultName.isEmpty() ? QStringLiteral("fault %1").arg(f.faultId) : f.faultName, ms, ms, true,
                                   QStringLiteral("NMS") };
                    openNms.insert(key, t.bars.size() - 1);
                } else {
                    t.bars[openNms.value(key)].toMs = ms;
                }
            }
            // Cleared: listed before by this subsystem, not now.
            for (const QString &key : listed.value(sub)) {
                if (keys.contains(key) || !openNms.contains(key)) continue;
                Bar &b = t.bars[openNms.take(key)];
                b.toMs = ms;
                b.open = false;
            }
            listed[sub] = keys;
            continue;
        }

        // CCSYS: lcu_elem_status1/2 display rows, "can0=1 can1=1 radio1=0 ...".
        for (const FieldRow &row : CaptureDecoder::describe(cap)) {
            const QString name = row.field.trimmed();
            const int lcu = name == QLatin1String("lcu_elem_status1") ? 1 : name == QLatin1String("lcu_elem_status2") ? 2 : 0;
            if (!lcu) continue;
            for (const QString &part : row.value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                const int eq = part.indexOf(QLatin1Char('='));
                if (eq <= 0) continue;
                const QString element = QStringLiteral("LCU-%1 %2").arg(lcu).arg(part.left(eq));
                const bool down = part.mid(eq + 1) == QLatin1String("0");
                if (down) {
                    if (openLcu.contains(element)) t.bars[openLcu.value(element)].toMs = ms;
                    else {
                        addRow(element);
                        t.bars << Bar{ element, QStringLiteral("down (0)"), ms, ms, true, QStringLiteral("CCSYS") };
                        openLcu.insert(element, t.bars.size() - 1);
                    }
                } else if (openLcu.contains(element)) {
                    Bar &b = t.bars[openLcu.take(element)];
                    b.toMs = ms;
                    b.open = false;
                }
            }
        }
    }
    for (Bar &b : t.bars) if (b.open) b.toMs = t.toMs;

    // Modes and System_Failure onsets, from the missions.
    for (const Missions::Mission &m : Missions::split(model)) {
        if (m.firstMode.isEmpty()) continue;
        qint64 at = m.fromMs;
        QString mode = m.firstMode;
        for (const RunReport::Change &c : m.modes) {
            t.modes << ModeSpan{ at, c.ms, mode };
            if (c.to.startsWith(QLatin1String("12 "))) t.failures << c.ms;
            at = c.ms;
            mode = c.to;
        }
        t.modes << ModeSpan{ at, m.toMs, mode };
        if (m.firstMode.startsWith(QLatin1String("12 "))) t.failures << m.fromMs;
    }
    return t;
}

QString failuresHtml(const Timeline &t)
{
    if (t.failures.isEmpty()) return QObject::tr("<p>No System_Failure in this tab.</p>");
    QString h = QObject::tr("<p><b>Faults raised at each System_Failure</b></p>");
    for (qint64 ms : t.failures) {
        const QVector<Bar> act = t.activeAt(ms);
        h += QStringLiteral("<p>%1 — %2</p>").arg(hms(ms))
                 .arg(act.isEmpty() ? QObject::tr("no fault raised") : QObject::tr("%1 raised:").arg(act.size()));
        if (act.isEmpty()) continue;
        h += QStringLiteral("<ul>");
        for (const Bar &b : act)
            h += QStringLiteral("<li>%1: %2 <span style=\"opacity:0.75\">(%3, since %4)</span></li>")
                     .arg(b.row.toHtmlEscaped(), b.fault.toHtmlEscaped(), b.source, hms(b.fromMs));
        h += QStringLiteral("</ul>");
    }
    return h;
}

}  // namespace FaultTimeline
