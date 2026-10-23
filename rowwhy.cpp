#include "rowwhy.h"

#include "capturedecoder.h"
#include "colorrules.h"
#include "rejectrules.h"

#include <QHash>
#include <QMessageBox>
#include <QObject>
#include <QStringList>

QString RowWhy::explain(const QString &text, const ColorRules &rules)
{
    QStringList out;

    const CaptureLine cap = CaptureDecoder::parseLine(text);
    if (cap.valid && !cap.bytes.isEmpty()) {
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(cap, nullptr, 0, &raw, nullptr);
        const auto findings = kavachRejectRules().evaluate(raw, cap.typeToken);
        if (!findings.isEmpty()) {
            out << QObject::tr("A receiver would not process this %1 frame:").arg(cap.typeToken.toUpper());
            for (const auto &f : findings) {
                QString line = QStringLiteral("  • ") + f.text;
                if (!f.rule.note.isEmpty()) line += QStringLiteral(" — ") + f.rule.note;
                out << line;
            }
        }
        if (cap.crcChecked && !cap.crcOk)
            out << QObject::tr("Its CRC does not match its bytes: the frame was damaged on the way, "
                               "or was built with a different CRC.");
    }

    const ColorRules::RuleMatch m = rules.classify(text);
    const QString sev = m.severity == Severity::Error ? QObject::tr("an error")
                      : m.severity == Severity::Warn  ? QObject::tr("a warning")
                                                      : QObject::tr("information");
    if (m.severityRule >= 0) {
        QString line = QObject::tr("It is shown as %1 because its text matches colour rule #%2 (%3).")
                           .arg(sev).arg(m.severityRule).arg(rules.rulePattern(m.severityRule));
        const QString note = rules.ruleNote(m.severityRule);
        if (!note.isEmpty()) line += QStringLiteral(" ") + QObject::tr("The rule's note: %1").arg(note);
        out << line;
    } else {
        out << QObject::tr("No colour rule sets its severity, so it is shown as information.");
    }
    if (m.colorRule >= 0 && m.colorRule != m.severityRule)
        out << QObject::tr("Its colours come from colour rule #%1 (%2), which says nothing about severity.")
                   .arg(m.colorRule).arg(rules.rulePattern(m.colorRule));

    return out.join(QLatin1Char('\n'));
}

void RowWhy::showDialog(QWidget *parent, const QString &text, const ColorRules &rules)
{
    QMessageBox box(parent);
    box.setWindowTitle(QObject::tr("Why is this row coloured?"));
    box.setIcon(QMessageBox::NoIcon);
    box.setTextFormat(Qt::PlainText);
    box.setText(explain(text, rules));
    box.setInformativeText(text.size() > 160 ? text.left(157) + QStringLiteral("…") : text);
    box.setDetailedText(rules.explainClassification(text));
    box.exec();
}
