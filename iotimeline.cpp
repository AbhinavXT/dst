#include "iotimeline.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "missionreport.h"

#include <QHash>
#include <QMap>
#include <QRegularExpression>

namespace IoTimeline {
namespace {

struct Signal {
    QString row;
    QVector<qint64> ms;
    QVector<QString> v;
};

// "5 dmi1_common_nc" -> "dmi1_common_nc"; "pin[3]" stays.
QString signalName(const QString &field)
{
    static const QRegularExpression lead(QStringLiteral("^\\d+\\s+"));
    QString s = field.trimmed();
    s.remove(lead);
    return s;
}

}  // namespace

FaultTimeline::Timeline build(const LogModel *model)
{
    FaultTimeline::Timeline t;
    if (!model || model->count() == 0) return t;

    // Pass 1: every DIO signal's samples, by row name.
    QMap<QString, Signal> sigs;              // ordered by row name: DIP1, DIP2, DOP1, DOP2
    for (int i = 0; i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        if (!t.fromMs) t.fromMs = e->epochMs;
        t.toMs = qMax(t.toMs, e->epochMs);
        const QString &tx = e->text;
        QString group;
        if (tx.startsWith(QLatin1String("@dip1_"))) group = QStringLiteral("DIP1");
        else if (tx.startsWith(QLatin1String("@dip2_"))) group = QStringLiteral("DIP2");
        else if (tx.startsWith(QLatin1String("@dop1_"))) group = QStringLiteral("DOP1");
        else if (tx.startsWith(QLatin1String("@dop2_"))) group = QStringLiteral("DOP2");
        else continue;
        const CaptureLine cap = CaptureDecoder::parseLine(tx);
        if (!cap.valid) continue;
        const bool out = group.startsWith(QLatin1String("DOP"));
        static const QRegularExpression numbered(QStringLiteral("^\\d+\\s+\\w+$"));
        for (const FieldRow &r : CaptureDecoder::describe(cap)) {
            const QString field = r.field.trimmed();
            // Inputs are numbered ("5 dmi1_common_nc"); outputs are pin[i].
            if (out ? !field.startsWith(QLatin1String("pin[")) : !numbered.match(field).hasMatch()) continue;
            QString name = signalName(field);
            QString value = r.value.trimmed();
            if (out) {
                // "a=1 b=1 fault=1 trans=0 output=1": the output bit.
                const int at = value.indexOf(QLatin1String("output="));
                if (!name.startsWith(QLatin1String("pin[")) || at < 0) continue;
                value = value.mid(at + 7, 1);
                name = QStringLiteral("pin %1").arg(name.mid(4, name.indexOf(QLatin1Char(']')) - 4));
            }
            if (value != QLatin1String("0") && value != QLatin1String("1")) continue;
            Signal &s = sigs[group + QLatin1Char(' ') + name];
            s.row = group + QLatin1Char(' ') + name;
            s.ms << e->epochMs;
            s.v << value;
        }
    }

    // Pass 2: the changing ones, as bars where they differ from their usual value.
    for (const Signal &s : sigs) {
        int ones = 0;
        for (const QString &v : s.v) ones += v == QLatin1String("1") ? 1 : 0;
        const QString usual = ones * 2 >= s.v.size() ? QStringLiteral("1") : QStringLiteral("0");
        bool changes = false;
        for (int k = 1; k < s.v.size() && !changes; ++k) changes = s.v.at(k) != s.v.at(0);
        if (!changes) continue;
        t.rows << s.row;
        bool open = false;
        for (int k = 0; k < s.v.size(); ++k) {
            const bool off = s.v.at(k) != usual;
            if (off && !open) {
                t.bars << FaultTimeline::Bar{ s.row, QStringLiteral("reads %1 (usually %2)").arg(s.v.at(k), usual),
                                              s.ms.at(k), s.ms.at(k), true, s.row.left(4) };
                open = true;
            } else if (off) {
                t.bars.last().toMs = s.ms.at(k);
            } else if (open) {
                t.bars.last().toMs = s.ms.at(k);   // back to usual: the bar ends here
                t.bars.last().open = false;
                open = false;
            }
        }
    }

    // Modes and System_Failure onsets, as on the fault timeline.
    for (const Missions::Mission &m : Missions::split(model)) {
        if (m.firstMode.isEmpty()) continue;
        qint64 at = m.fromMs;
        QString mode = m.firstMode;
        for (const RunReport::Change &c : m.modes) {
            t.modes << FaultTimeline::ModeSpan{ at, c.ms, mode };
            if (c.to.startsWith(QLatin1String("12 "))) t.failures << c.ms;
            at = c.ms;
            mode = c.to;
        }
        t.modes << FaultTimeline::ModeSpan{ at, m.toMs, mode };
    }
    return t;
}

}  // namespace IoTimeline
