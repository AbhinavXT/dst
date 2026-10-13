#include "testrun.h"

#include "testcatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace TestRun {

namespace {

QString firstLine(const TestCatalog::Case &k)
{
    for (const QString *t : { &k.title, &k.purpose, &k.input, &k.expected }) {
        const QString line = t->section(QLatin1Char('\n'), 0, 0).trimmed();
        if (!line.isEmpty()) return line;
    }
    return QString();
}

QString stamp(const QDateTime &t) { return t.isValid() ? t.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString(); }

QString csvCell(QString s)
{
    if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')) || s.contains(QLatin1Char('\n'))) {
        s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        s = QLatin1Char('"') + s + QLatin1Char('"');
    }
    return s;
}

QString byWhen(const Result &x)
{
    QStringList parts;
    if (!x.tester.isEmpty()) parts << x.tester;
    if (x.when.isValid()) parts << stamp(x.when);
    return parts.join(QStringLiteral(" \u00B7 "));
}

QStringList idsOf(const TestCatalog::Catalog &cat)
{
    QStringList ids;
    for (const TestCatalog::Case &k : cat.cases) ids << k.id;
    return ids;
}

}  // namespace

QString verdictName(Verdict v)
{
    switch (v) {
    case Verdict::Pass: return QStringLiteral("Pass");
    case Verdict::Fail: return QStringLiteral("Fail");
    case Verdict::NotApplicable: return QStringLiteral("N/A");
    default: return QStringLiteral("Not tested");
    }
}

QString verdictKey(Verdict v)
{
    switch (v) {
    case Verdict::Pass: return QStringLiteral("pass");
    case Verdict::Fail: return QStringLiteral("fail");
    case Verdict::NotApplicable: return QStringLiteral("na");
    default: return QString();
    }
}

Verdict verdictFromKey(const QString &key)
{
    if (key == QLatin1String("pass")) return Verdict::Pass;
    if (key == QLatin1String("fail")) return Verdict::Fail;
    if (key == QLatin1String("na")) return Verdict::NotApplicable;
    return Verdict::NotTested;
}

Result resultOf(const Run &run, const QString &id)
{
    return run.results.value(id);
}

void setResult(Run *run, const QString &id, const Result &r)
{
    if (!run || id.isEmpty()) return;
    // Not tested with nothing written: no entry. With remarks it is kept,
    // so a note made before the test is run is not lost.
    if (r.verdict == Verdict::NotTested && r.remarks.trimmed().isEmpty() && r.evidence.trimmed().isEmpty())
        run->results.remove(id);
    else
        run->results.insert(id, r);
}

Counts count(const Run &run, const QStringList &ids)
{
    Counts c;
    // QStringList(...): in Qt 5 keys() is a QList<QString>, and ?: needs one type.
    const QStringList list = ids.isEmpty() ? QStringList(run.results.keys()) : ids;
    for (const QString &id : list) {
        switch (run.results.value(id).verdict) {
        case Verdict::Pass: ++c.pass; break;
        case Verdict::Fail: ++c.fail; break;
        case Verdict::NotApplicable: ++c.na; break;
        default: ++c.notTested; break;
        }
    }
    return c;
}

QStringList orphans(const Run &run, const QStringList &ids)
{
    QStringList out;
    for (auto it = run.results.cbegin(); it != run.results.cend(); ++it)
        if (!ids.contains(it.key())) out << it.key();
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return TestCatalog::compareIds(a, b) < 0; });
    return out;
}

bool load(const QString &path, Run *out, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    const QJsonObject o = doc.object();
    if (pe.error != QJsonParseError::NoError || o.value(QStringLiteral("kind")).toString() != QLatin1String("dlconsole-testrun")) {
        if (err) *err = pe.error != QJsonParseError::NoError ? pe.errorString() : QStringLiteral("not a DLConsole test run");
        return false;
    }
    Run r;
    r.title = o.value(QStringLiteral("title")).toString();
    r.loco = o.value(QStringLiteral("loco")).toString();
    r.firmware = o.value(QStringLiteral("firmware")).toString();
    r.site = o.value(QStringLiteral("site")).toString();
    r.testers = o.value(QStringLiteral("testers")).toString();
    r.catalog = o.value(QStringLiteral("catalog")).toString();
    r.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODate);
    const QJsonArray results = o.value(QStringLiteral("results")).toArray();
    for (const QJsonValue &v : results) {
        const QJsonObject e = v.toObject();
        const QString id = e.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;
        Result x;
        x.verdict = verdictFromKey(e.value(QStringLiteral("result")).toString());
        x.tester = e.value(QStringLiteral("tester")).toString();
        x.when = QDateTime::fromString(e.value(QStringLiteral("when")).toString(), Qt::ISODate);
        x.remarks = e.value(QStringLiteral("remarks")).toString();
        x.evidence = e.value(QStringLiteral("evidence")).toString();
        r.results.insert(id, x);
    }
    *out = r;
    return true;
}

bool save(const QString &path, const Run &run, QString *err)
{
    QJsonObject o;
    o.insert(QStringLiteral("kind"), QStringLiteral("dlconsole-testrun"));
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("title"), run.title);
    o.insert(QStringLiteral("loco"), run.loco);
    o.insert(QStringLiteral("firmware"), run.firmware);
    o.insert(QStringLiteral("site"), run.site);
    o.insert(QStringLiteral("testers"), run.testers);
    o.insert(QStringLiteral("catalog"), run.catalog);
    o.insert(QStringLiteral("created"), run.created.toString(Qt::ISODate));
    QStringList ids = run.results.keys();
    std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) { return TestCatalog::compareIds(a, b) < 0; });
    QJsonArray results;
    for (const QString &id : ids) {
        const Result &x = run.results.value(id);
        QJsonObject e;
        e.insert(QStringLiteral("id"), id);
        e.insert(QStringLiteral("result"), verdictKey(x.verdict));
        if (!x.tester.isEmpty()) e.insert(QStringLiteral("tester"), x.tester);
        if (x.when.isValid()) e.insert(QStringLiteral("when"), x.when.toString(Qt::ISODate));
        if (!x.remarks.isEmpty()) e.insert(QStringLiteral("remarks"), x.remarks);
        if (!x.evidence.isEmpty()) e.insert(QStringLiteral("evidence"), x.evidence);
        results.append(e);
    }
    o.insert(QStringLiteral("results"), results);
    QSaveFile f(path);
    const QByteArray data = QJsonDocument(o).toJson(QJsonDocument::Indented);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (err) *err = f.errorString();
        return false;
    }
    return true;
}

QString toHtml(const Run &run, const TestCatalog::Catalog &cat)
{
    const QStringList ids = idsOf(cat);
    const Counts c = count(run, ids);
    auto esc = [](const QString &s) { return s.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")); };
    auto cls = [](Verdict v) {
        return v == Verdict::Pass ? QStringLiteral("pass") : v == Verdict::Fail ? QStringLiteral("fail")
             : v == Verdict::NotApplicable ? QStringLiteral("na") : QStringLiteral("nt");
    };
    QString h;
    h += QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\"><title>%1</title><style>"
                        "body{font-family:Segoe UI,Arial,sans-serif;margin:24px;color:#1d2228}"
                        "h1{font-size:20px;margin:0 0 4px}h2{font-size:16px;margin:22px 0 6px}"
                        "table{border-collapse:collapse;width:100%}td,th{border:1px solid #c9ced4;padding:4px 6px;"
                        "vertical-align:top;font-size:13px;text-align:left}th{background:#eef0f3}"
                        ".pass{color:#1b6b2e;font-weight:600}.fail{color:#a4161a;font-weight:600}"
                        ".na{color:#5a6169}.nt{color:#5a6169}.who{color:#5a6169;font-size:12px}"
                        ".note{color:#5a6169;font-size:12px;margin:6px 0 14px}"
                        "</style></head><body>").arg(esc(run.title.isEmpty() ? QStringLiteral("Test run") : run.title));
    h += QStringLiteral("<h1>%1</h1>").arg(esc(run.title.isEmpty() ? QStringLiteral("Test run") : run.title));
    h += QStringLiteral("<table style=\"width:auto\"><tr><th>Loco</th><td>%1</td><th>Firmware</th><td>%2</td></tr>"
                        "<tr><th>Site</th><td>%3</td><th>Testers</th><td>%4</td></tr>"
                        "<tr><th>Catalog</th><td>%5</td><th>Started</th><td>%6</td></tr></table>")
             .arg(esc(run.loco), esc(run.firmware), esc(run.site), esc(run.testers), esc(run.catalog), stamp(run.created));
    h += QStringLiteral("<p class=\"note\">Results as recorded by the testers. DLConsole records them; it does not "
                        "set or judge them.</p>");
    h += QStringLiteral("<p><span class=\"pass\">%1 Pass</span> &middot; <span class=\"fail\">%2 Fail</span> &middot; "
                        "<span class=\"na\">%3 N/A</span> &middot; <span class=\"nt\">%4 not tested</span> "
                        "&middot; %5 of %6 clauses done</p>")
             .arg(c.pass).arg(c.fail).arg(c.na).arg(c.notTested).arg(c.done()).arg(c.total());
    auto row = [&](const TestCatalog::Case &k, const Result &x) {
        return QStringLiteral("<tr><td>%1</td><td>%2</td><td class=\"%3\">%4</td><td>%5%6</td><td class=\"who\">%7</td></tr>")
            .arg(esc(k.id), esc(firstLine(k)), cls(x.verdict), verdictName(x.verdict), esc(x.remarks),
                 x.evidence.isEmpty() ? QString() : QStringLiteral("<div class=\"who\">Evidence: %1</div>").arg(esc(x.evidence)),
                 esc(byWhen(x)));
    };
    const QString head = QStringLiteral("<tr><th>Clause</th><th>Test scenario</th><th>Result</th><th>Remarks</th><th>By, when</th></tr>");
    if (c.fail) {
        h += QStringLiteral("<h2>Failed (%1)</h2><table>").arg(c.fail) + head;
        for (const TestCatalog::Case &k : cat.cases) {
            const Result x = run.results.value(k.id);
            if (x.verdict == Verdict::Fail) h += row(k, x);
        }
        h += QStringLiteral("</table>");
    }
    int sec = -1;
    for (const TestCatalog::Case &k : cat.cases) {
        const int s = TestCatalog::sectionOf(k.id);
        if (s != sec) {
            if (sec >= 0) h += QStringLiteral("</table>");
            sec = s;
            const TestCatalog::Section *sp = TestCatalog::section(cat, s);
            QStringList secIds;
            for (const TestCatalog::Case &q : cat.cases) if (TestCatalog::sectionOf(q.id) == s) secIds << q.id;
            const Counts sc = count(run, secIds);
            h += QStringLiteral("<h2>%1 %2 <span class=\"who\">%3 of %4 done</span></h2><table>")
                     .arg(s).arg(esc(sp ? sp->title : QString())).arg(sc.done()).arg(sc.total()) + head;
        }
        h += row(k, run.results.value(k.id));
    }
    if (sec >= 0) h += QStringLiteral("</table>");
    const QStringList lost = orphans(run, ids);
    if (!lost.isEmpty())
        h += QStringLiteral("<h2>Results for clauses this catalog does not have</h2><p>%1</p>")
                 .arg(esc(lost.join(QStringLiteral(", "))));
    h += QStringLiteral("</body></html>");
    return h;
}

QString toCsv(const Run &run, const TestCatalog::Catalog &cat)
{
    QStringList lines;
    lines << QStringLiteral("Clause,Section,Test scenario,Result,Tester,When,Remarks,Evidence");
    for (const TestCatalog::Case &k : cat.cases) {
        const Result x = run.results.value(k.id);
        lines << QStringList{ csvCell(k.id), QString::number(TestCatalog::sectionOf(k.id)), csvCell(firstLine(k)),
                              verdictName(x.verdict), csvCell(x.tester), stamp(x.when), csvCell(x.remarks),
                              csvCell(x.evidence) }.join(QLatin1Char(','));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

}  // namespace TestRun
