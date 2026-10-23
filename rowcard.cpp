#include "rowcard.h"

#include "capturedecoder.h"
#include "fieldinspector.h"
#include "rejectrules.h"

#include <QHash>

QString RowCard::html(const QString &text)
{
    const CaptureLine cap = CaptureDecoder::parseLine(text);
    if (!cap.valid || cap.bytes.isEmpty()) return QString();

    QHash<QString, qint64> raw;
    const QVector<FieldRow> rows = CaptureDecoder::describe(cap, nullptr, 0, &raw, nullptr);

    QString out = QStringLiteral("<b>%1 · %2 B</b>").arg(cap.typeToken.toUpper().toHtmlEscaped()).arg(cap.bytes.size());
    if (cap.crcChecked) out += cap.crcOk ? QObject::tr(" &nbsp;✓ CRC pass") : QObject::tr(" &nbsp;✕ CRC fail");

    QString table;
    for (const QString &want : FieldInspector::keyFields()) {
        for (const FieldRow &r : rows) {
            if (r.field.trimmed().compare(want, Qt::CaseInsensitive) != 0) continue;
            table += QStringLiteral("<tr><td>%1&nbsp;&nbsp;</td><td><b>%2</b></td></tr>")
                         .arg(FieldInspector::tileCaption(want).toHtmlEscaped(),
                              FieldInspector::tileValue(r.value).toHtmlEscaped());
            break;
        }
    }
    if (!table.isEmpty()) out += QStringLiteral("<table>%1</table>").arg(table);

    // Same words as the inspector's chip: what a receiver would do, not a verdict.
    const auto findings = kavachRejectRules().evaluate(raw, cap.typeToken);
    if (!findings.isEmpty()) {
        out += QStringLiteral("<br>▲ ") + QObject::tr("A receiver would not process this frame: %1")
                                              .arg(findings.first().text.toHtmlEscaped());
        if (findings.size() > 1) out += QObject::tr(" (and %1 more)").arg(findings.size() - 1);
    }
    return out;
}
