#include "tagcheck.h"

#include "capturedecoder.h"
#include "logmodel.h"

#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace TagCheck {

int Report::duplicatesRead() const
{
    int n = 0;
    for (const Tag &t : tags) n += t.duplicate ? 1 : 0;
    return n;
}

int Report::duplicatesReportedMissing() const
{
    int n = 0;
    for (const Tag &t : tags) n += t.duplicateMissingMs ? 1 : 0;
    return n + duplicateMissingOther.size();
}

Report build(const LogModel *model, qint64 fromMs, qint64 toMs)
{
    Report r;
    if (!model) return r;
    QHash<QString, int> index;           // tag -> r.tags
    QHash<QString, qint64> nmsMissing;   // tag -> first report
    QStringList nmsOrder;
    QStringList route;                   // the latest SLRP route tag list
    QSet<QString> everRouted;
    QString prev;
    static const QRegularExpression tagRe(QStringLiteral("tag=(\\d+)"));

    for (int i = 0; i < model->count(); ++i) {
        const LogEntryPtr e = model->entryAt(i);
        if (!e) continue;
        if ((fromMs > 0 && e->epochMs < fromMs) || (toMs > 0 && e->epochMs > toMs)) continue;
        const QString &tx = e->text;
        const bool rfid = tx.startsWith(QLatin1String("@rfid_"));
        const bool slrp = tx.startsWith(QLatin1String("@slrp_"));
        const bool nms = tx.startsWith(QLatin1String("@nmshlth_"));
        if (!rfid && !slrp && !nms) continue;
        const CaptureLine cap = CaptureDecoder::parseLine(tx);
        if (!cap.valid) continue;
        QHash<QString, qint64> raw;
        const QVector<FieldRow> rows = CaptureDecoder::describe(cap, nullptr, 0, &raw);

        if (slrp) {
            QStringList list;
            for (const FieldRow &f : rows) {
                if (!f.field.trimmed().startsWith(QLatin1String("tag["))) continue;
                const QRegularExpressionMatch m = tagRe.match(f.value);
                if (m.hasMatch()) list << m.captured(1);
            }
            if (!list.isEmpty()) { route = list; for (const QString &t : list) everRouted.insert(t); }
            continue;
        }
        if (nms) {
            for (const FieldRow &f : rows) {
                if (f.field.trimmed() != QLatin1String("DUPLICATE_MISSING_RFID")) continue;
                const QString tag = f.value.trimmed().section(QLatin1Char(' '), 0, 0);
                if (!nmsMissing.contains(tag)) { nmsMissing.insert(tag, e->epochMs); nmsOrder << tag; }
            }
            continue;
        }
        // @rfid
        if (!raw.contains(QStringLiteral("unique"))) continue;
        const QString tag = QString::number(raw.value(QStringLiteral("unique")));
        if (tag == QLatin1String("0")) continue;
        if (!index.contains(tag)) {
            index.insert(tag, r.tags.size());
            r.tags << Tag{ tag, e->epochMs, false, false, 0, false };
        }
        Tag &t = r.tags[index.value(tag)];
        if (raw.value(QStringLiteral("duplication"), 0) == 0) t.main = true; else t.duplicate = true;
        if (tag != prev) {
            if (!prev.isEmpty()) {
                const int a = route.indexOf(prev), b = route.indexOf(tag);
                if (a >= 0 && b > a + 1) r.skipped << Skipped{ e->epochMs, prev, tag, route.mid(a + 1, b - a - 1) };
            }
            prev = tag;
        }
    }
    for (Tag &t : r.tags) {
        t.onRoute = everRouted.contains(t.tag);
        t.duplicateMissingMs = nmsMissing.value(t.tag, 0);
    }
    for (const QString &tag : nmsOrder) if (!index.contains(tag)) r.duplicateMissingOther << tag;
    return r;
}

QString toHtml(const Report &r, int maxRows)
{
    auto hms = [](qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss")); };
    QString h = QStringLiteral("<h2>RFID tags: %1 read</h2>").arg(r.tags.size());
    if (r.tags.isEmpty() && r.duplicateMissingOther.isEmpty()) return h;
    h += QStringLiteral("<p>Duplicate tag read for %1 of them; the NMS reported a duplicate missing for %2. "
                        "Route tags passed without a read: %3.</p>")
             .arg(r.duplicatesRead()).arg(r.duplicatesReportedMissing()).arg(r.skipped.size());
    h += QStringLiteral("<table><tr><th>Tag</th><th>First read</th><th>Main</th><th>Duplicate</th>"
                        "<th>NMS: duplicate missing</th><th>On an SLRP route</th></tr>");
    for (int i = 0; i < r.tags.size() && i < maxRows; ++i) {
        const Tag &t = r.tags.at(i);
        h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td></tr>")
                 .arg(t.tag, hms(t.firstMs), t.main ? QStringLiteral("read") : QStringLiteral("—"),
                      t.duplicate ? QStringLiteral("read") : QStringLiteral("not read"),
                      t.duplicateMissingMs ? hms(t.duplicateMissingMs) : QString(), t.onRoute ? QStringLiteral("yes") : QString());
    }
    h += QStringLiteral("</table>");
    if (!r.skipped.isEmpty()) {
        h += QStringLiteral("<table><tr><th>At</th><th>Read</th><th>Then read</th><th>Route tags between, not read</th></tr>");
        for (int i = 0; i < r.skipped.size() && i < maxRows; ++i)
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                     .arg(hms(r.skipped.at(i).ms), r.skipped.at(i).from, r.skipped.at(i).to, r.skipped.at(i).passed.join(QStringLiteral(", ")));
        h += QStringLiteral("</table>");
    }
    if (!r.duplicateMissingOther.isEmpty())
        h += QStringLiteral("<p>The NMS also named tags not read in this span: %1.</p>").arg(r.duplicateMissingOther.join(QStringLiteral(", ")));
    return h;
}

}  // namespace TagCheck
