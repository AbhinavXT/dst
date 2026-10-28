#include "reviewpack.h"

#include "dmichanges.h"
#include "logmodel.h"
#include "notessidecar.h"
#include "sessionfile.h"
#include "testrun.h"
#include "xlsxbook.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>
#include <QTemporaryDir>

namespace ReviewPack {
namespace {

// A zip of more than this is refused: a QByteArray holds under 2 GiB on Qt 5,
// and the zip has no zip64. A narrower span is the way out.
constexpr qint64 kMaxBytes = qint64(1536) * 1024 * 1024;

void put16(QByteArray &b, quint16 v) { b.append(char(v & 0xff)); b.append(char(v >> 8)); }
void put32(QByteArray &b, quint32 v) { put16(b, quint16(v & 0xffff)); put16(b, quint16(v >> 16)); }

struct Entry {
    QString name;
    QByteArray data;
};

// Raw deflate is zlib's stream without its 2-byte header and 4-byte Adler-32;
// qCompress adds a 4-byte length in front of that. Stored instead when
// deflate does not make it smaller.
QByteArray deflateRaw(const QByteArray &in)
{
    const QByteArray z = qCompress(in, 6);
    if (z.size() < 4 + 2 + 4) return QByteArray();
    return z.mid(4 + 2, z.size() - 4 - 2 - 4);
}

QByteArray zipOf(const QVector<Entry> &files, qint64 whenMs)
{
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(whenMs);
    const quint16 dosTime = quint16((t.time().hour() << 11) | (t.time().minute() << 5) | (t.time().second() / 2));
    const quint16 dosDate = quint16(((qMax(1980, t.date().year()) - 1980) << 9) | (t.date().month() << 5) | t.date().day());
    QByteArray out, central;
    for (const Entry &f : files) {
        const QByteArray name = f.name.toUtf8();
        const quint32 crc = XlsxBook::crc32(f.data);
        QByteArray body = deflateRaw(f.data);
        quint16 method = 8;
        if (body.isEmpty() || body.size() >= f.data.size()) { body = f.data; method = 0; }
        const quint32 offset = quint32(out.size());
        for (int pass = 0; pass < 2; ++pass) {
            QByteArray &b = pass == 0 ? out : central;
            put32(b, pass == 0 ? 0x04034b50u : 0x02014b50u);
            if (pass == 1) put16(b, 20);           // made by
            put16(b, 20);                          // needed
            put16(b, 0x0800);                      // flags: UTF-8 names
            put16(b, method);
            put16(b, dosTime);
            put16(b, dosDate);
            put32(b, crc);
            put32(b, quint32(body.size()));
            put32(b, quint32(f.data.size()));
            put16(b, quint16(name.size()));
            put16(b, 0);                           // extra
            if (pass == 1) {
                put16(b, 0); put16(b, 0); put16(b, 0);   // comment, disk, internal attributes
                put32(b, 0);                       // external attributes
                put32(b, offset);
            }
            b.append(name);
            if (pass == 0) b.append(body);
        }
    }
    const quint32 cdOffset = quint32(out.size());
    out.append(central);
    put32(out, 0x06054b50u);
    put16(out, 0); put16(out, 0);
    put16(out, quint16(files.size())); put16(out, quint16(files.size()));
    put32(out, quint32(central.size()));
    put32(out, cdOffset);
    put16(out, 0);
    return out;
}

QString hex8(quint32 v) { return QStringLiteral("%1").arg(v, 8, 16, QLatin1Char('0')); }

QString timeText(qint64 ms)
{
    return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QString();
}

// "21_1" → 21, 1.
void idsOf(const QString &tabKey, quint8 *src, quint16 *kvch)
{
    const int us = tabKey.indexOf(QLatin1Char('_'));
    *src = us > 0 ? quint8(tabKey.left(us).toInt()) : 0;
    *kvch = us > 0 ? quint16(tabKey.mid(us + 1).toInt()) : 0;
}

// What summary.html shows beyond the manifest: per capture, its notes and
// what changed on each loco's DMI.
struct CaptureSummary {
    QString file;
    QVector<Bookmark> marks;
    QVector<QPair<QString, DmiChanges::Change>> dmi;   // (loco, change)
};

QString esc(const QString &s) { return s.toHtmlEscaped(); }

// summary.html (session 383): the pack for a reader without DLConsole, in
// any browser. Times are the frames' own, as on the rows.
QString summaryHtml(const QJsonObject &m, const QVector<CaptureSummary> &caps, const QString &testRunPath)
{
    QString h;
    h += QStringLiteral("<!doctype html>\n<html><head><meta charset=\"utf-8\"><title>Review pack%1</title>\n"
                        "<style>body{font:14px/1.45 sans-serif;margin:24px;max-width:1100px;color:#1a1a1a;background:#fff}"
                        "h1{font-size:20px}h2{font-size:16px;margin-top:28px;border-bottom:1px solid #bbb}"
                        "table{border-collapse:collapse;margin:6px 0}td,th{border:1px solid #ccc;padding:3px 8px;"
                        "text-align:left;vertical-align:top}th{background:#eee}.what{background:#fff6d6;padding:10px;"
                        "border:1px solid #d9c27a;white-space:pre-wrap}.muted{color:#555}img{max-width:480px;"
                        "border:1px solid #ccc;margin:4px}</style></head><body>\n")
             .arg(m.value(QStringLiteral("site")).toString().isEmpty() ? QString()
                                                                       : QStringLiteral(" — ") + esc(m.value(QStringLiteral("site")).toString()));
    const QJsonObject reply = m.value(QStringLiteral("in_reply_to")).toObject();
    h += reply.isEmpty() ? QStringLiteral("<h1>DLConsole review pack</h1>\n")
                         : QStringLiteral("<h1>DLConsole review pack: a reply</h1>\n<p>To the pack of %1 from %2. "
                                          "They asked:</p><div class=\"what\">%3</div>\n")
                               .arg(esc(reply.value(QStringLiteral("created")).toString()),
                                    esc(reply.value(QStringLiteral("site")).toString()),
                                    esc(reply.value(QStringLiteral("what_to_look_at")).toString()));
    const QString what = m.value(QStringLiteral("what_to_look_at")).toString();
    h += QStringLiteral("<p><b>%1</b></p><div class=\"what\">%2</div>\n")
             .arg(reply.isEmpty() ? QStringLiteral("What to look at") : QStringLiteral("The answer"),
                  what.isEmpty() ? QStringLiteral("(not given)") : esc(what));
    h += QStringLiteral("<table>");
    auto row = [&h](const QString &k, const QString &v) {
        if (!v.isEmpty()) h += QStringLiteral("<tr><th>%1</th><td>%2</td></tr>").arg(k, esc(v));
    };
    row(QStringLiteral("Site"), m.value(QStringLiteral("site")).toString());
    row(reply.isEmpty() ? QStringLiteral("Tester") : QStringLiteral("Reviewer"), m.value(QStringLiteral("tester")).toString());
    row(QStringLiteral("Loco"), m.value(QStringLiteral("loco")).toString());
    row(QStringLiteral("Made"), m.value(QStringLiteral("created")).toString());
    const QJsonObject span = m.value(QStringLiteral("span")).toObject();
    row(QStringLiteral("Span"), span.value(QStringLiteral("from")).toString().isEmpty()
                                    ? QStringLiteral("everything in the chosen tabs")
                                    : span.value(QStringLiteral("from")).toString() + QStringLiteral(" to ")
                                          + span.value(QStringLiteral("to")).toString());
    const QJsonObject prog = m.value(QStringLiteral("program")).toObject();
    row(QStringLiteral("Program"), QStringLiteral("%1 %2, Qt %3, %4").arg(prog.value(QStringLiteral("name")).toString(),
                                                                          prog.value(QStringLiteral("version")).toString(),
                                                                          prog.value(QStringLiteral("qt")).toString(),
                                                                          prog.value(QStringLiteral("os")).toString()));
    row(QStringLiteral("Schema"), m.value(QStringLiteral("schema")).toObject().value(QStringLiteral("from")).toString());
    h += QStringLiteral("</table>\n");

    h += QStringLiteral("<h2>Captures</h2>\n<table><tr><th>File</th><th>Tab</th><th>Frames</th><th>From</th><th>To</th>"
                        "<th>Notes</th></tr>");
    for (const QJsonValue &v : m.value(QStringLiteral("captures")).toArray()) {
        const QJsonObject c = v.toObject();
        h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td></tr>")
                 .arg(esc(c.value(QStringLiteral("file")).toString()), esc(c.value(QStringLiteral("tab")).toString()))
                 .arg(c.value(QStringLiteral("records")).toInt())
                 .arg(c.value(QStringLiteral("first")).toString(), c.value(QStringLiteral("last")).toString())
                 .arg(c.value(QStringLiteral("notes")).toInt());
    }
    h += QStringLiteral("</table>\n");

    QVector<QPair<QString, Bookmark>> notes;
    for (const CaptureSummary &c : caps)
        for (const Bookmark &b : c.marks) notes.append({ c.file, b });
    std::sort(notes.begin(), notes.end(), [](const auto &a, const auto &b) { return a.second.epochMs < b.second.epochMs; });
    h += QStringLiteral("<h2>Notes (%1)</h2>\n").arg(notes.size());
    if (notes.isEmpty()) {
        h += QStringLiteral("<p class=\"muted\">No notes in the span.</p>\n");
    } else {
        h += QStringLiteral("<table><tr><th>Time</th><th>Tab</th><th>Note</th><th>Row</th></tr>");
        for (const auto &n : notes)
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td class=\"muted\">%4</td></tr>")
                     .arg(timeText(n.second.epochMs) + QStringLiteral(".%1").arg(n.second.epochMs % 1000, 3, 10, QLatin1Char('0')),
                          esc(n.second.tabKey), esc(n.second.note.isEmpty() ? QStringLiteral("(marked, no note)") : n.second.note),
                          esc(n.second.messageSnippet.left(160)));
        h += QStringLiteral("</table>\n");
    }

    int dmiCount = 0;
    for (const CaptureSummary &c : caps) dmiCount += c.dmi.size();
    h += QStringLiteral("<h2>What changed on the DMI (%1)</h2>\n").arg(dmiCount);
    if (!dmiCount) {
        h += QStringLiteral("<p class=\"muted\">No change on a DMI in the span (or no @dmi frames).</p>\n");
    } else {
        h += QStringLiteral("<p class=\"muted\">Each @dmi frame compared with the one before; speed, distances and "
                            "location left out.</p>\n<table><tr><th>Time</th><th>Loco</th><th>Change</th></tr>");
        for (const CaptureSummary &c : caps)
            for (const auto &d : c.dmi)
                h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
                         .arg(timeText(d.second.ms), esc(d.first), esc(d.second.text()));
        h += QStringLiteral("</table>\n");
    }

    TestRun::Run run;
    if (!testRunPath.isEmpty() && TestRun::load(testRunPath, &run)) {
        h += QStringLiteral("<h2>The test run, as recorded at the site</h2>\n<p>%1%2</p>\n")
                 .arg(esc(run.title), run.testers.isEmpty() ? QString() : QStringLiteral(" — ") + esc(run.testers));
        QStringList ids = run.results.keys();
        ids.sort();
        if (ids.isEmpty()) {
            h += QStringLiteral("<p class=\"muted\">No clause has a result yet.</p>\n");
        } else {
            h += QStringLiteral("<table><tr><th>Clause</th><th>Result</th><th>By</th><th>Remarks</th><th>Evidence</th></tr>");
            for (const QString &id : ids) {
                const TestRun::Result r = run.results.value(id);
                h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
                         .arg(esc(id), esc(TestRun::verdictName(r.verdict)), esc(r.tester), esc(r.remarks), esc(r.evidence));
            }
            h += QStringLiteral("</table>\n");
        }
    }

    const QJsonArray att = m.value(QStringLiteral("attachments")).toArray();
    if (!att.isEmpty()) {
        h += QStringLiteral("<h2>Attachments</h2>\n<p>");
        static const QStringList images = { QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                            QStringLiteral("gif"), QStringLiteral("svg") };
        for (const QJsonValue &v : att) {
            const QString n = esc(v.toString());
            h += images.contains(QFileInfo(v.toString()).suffix().toLower())
                     ? QStringLiteral("<a href=\"%1\"><img src=\"%1\" alt=\"%1\"></a> ").arg(n)
                     : QStringLiteral("<a href=\"%1\">%1</a><br>").arg(n);
        }
        h += QStringLiteral("</p><p class=\"muted\">Unzip the pack first: the links point at its files.</p>\n");
    }
    h += QStringLiteral("<p class=\"muted\">This page shows what was recorded. It makes no pass/fail judgement. "
                        "Open the pack with DLConsole (File &gt; Open review pack...) for the frames themselves.</p>\n"
                        "</body></html>\n");
    return h;
}

QString readme(const QJsonObject &m)
{
    QString t;
    t += QStringLiteral("DLConsole review pack\n=====================\n\n");
    const QString what = m.value(QStringLiteral("what_to_look_at")).toString();
    t += QStringLiteral("What to look at:\n  %1\n\n")
             .arg(what.isEmpty() ? QStringLiteral("(not given)") : QString(what).replace(QLatin1Char('\n'), QStringLiteral("\n  ")));
    auto line = [&t, &m](const char *label, const char *key) {
        const QString v = m.value(QLatin1String(key)).toString();
        if (!v.isEmpty()) t += QStringLiteral("%1 %2\n").arg(QLatin1String(label), v);
    };
    const QJsonObject reply = m.value(QStringLiteral("in_reply_to")).toObject();
    if (!reply.isEmpty())
        t += QStringLiteral("A REPLY to the pack of %1 from %2.\n\n")
                 .arg(reply.value(QStringLiteral("created")).toString(), reply.value(QStringLiteral("site")).toString());
    line("Site:   ", "site");
    line(reply.isEmpty() ? "Tester: " : "Reviewer:", "tester");
    line("Loco:   ", "loco");
    line("Made:   ", "created");
    const QJsonObject span = m.value(QStringLiteral("span")).toObject();
    t += QStringLiteral("Span:    %1\n").arg(span.value(QStringLiteral("from")).toString().isEmpty()
                                                 ? QStringLiteral("everything in the chosen tabs")
                                                 : span.value(QStringLiteral("from")).toString() + QStringLiteral(" to ")
                                                       + span.value(QStringLiteral("to")).toString());
    const QJsonObject prog = m.value(QStringLiteral("program")).toObject();
    const QString who = QStringLiteral("%1 %2").arg(prog.value(QStringLiteral("name")).toString(),
                                                    prog.value(QStringLiteral("version")).toString()).trimmed();
    t += QStringLiteral("Program: %1, Qt %2, %3\n\n")
             .arg(who, prog.value(QStringLiteral("qt")).toString(), prog.value(QStringLiteral("os")).toString());
    t += QStringLiteral("Captures:\n");
    for (const QJsonValue &v : m.value(QStringLiteral("captures")).toArray()) {
        const QJsonObject c = v.toObject();
        t += QStringLiteral("  %1  (tab %2): %3 frames, %4 to %5, %6 note(s)\n")
                 .arg(c.value(QStringLiteral("file")).toString(), c.value(QStringLiteral("tab")).toString())
                 .arg(c.value(QStringLiteral("records")).toInt())
                 .arg(c.value(QStringLiteral("first")).toString(), c.value(QStringLiteral("last")).toString())
                 .arg(c.value(QStringLiteral("notes")).toInt());
        if (c.contains(QStringLiteral("key_frames")))
            t += QStringLiteral("      of them %1 key and config frame(s) from before the span (@auth_keys, @rand_num, @linfo):\n"
                                "      they carry key material\n").arg(c.value(QStringLiteral("key_frames")).toInt());
    }
    const QJsonArray att = m.value(QStringLiteral("attachments")).toArray();
    if (!att.isEmpty()) {
        t += QStringLiteral("\nAttachments:\n");
        for (const QJsonValue &v : att) t += QStringLiteral("  %1\n").arg(v.toString());
    }
    const QString run = m.value(QStringLiteral("test_run")).toString();
    t += QStringLiteral("\nTest run: %1\n").arg(run.isEmpty() ? QStringLiteral("none") : run);
    const QJsonObject schema = m.value(QStringLiteral("schema")).toObject();
    t += QStringLiteral("Schema:   %1 (the site's: %2)\n\n")
             .arg(schema.value(QStringLiteral("file")).toString(), schema.value(QStringLiteral("from")).toString());
    t += QStringLiteral("summary.html: the same, with the notes, what changed on the DMI and the test run,\n"
                        "for any browser.\n"
                        "Open it with DLConsole: File > Open review pack...\n"
                        "Each .dlr also opens on its own with File > Open recorded session...\n"
                        "This pack holds what was recorded. It makes no pass/fail judgement.\n");
    return t;
}

}  // namespace

bool isKeyFrame(const QString &text)
{
    return text.startsWith(QLatin1String("@auth_key")) || text.startsWith(QLatin1String("@rand_num"))
        || text.startsWith(QLatin1String("@linfo"));
}

QString captureBase(const Capture &c)
{
    QString n = c.name.isEmpty() || c.name == c.tabKey ? QString() : c.name + QLatin1Char('_');
    n += c.tabKey;
    n.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    return n;
}

QString attachmentName(const QString &localPath)
{
    QString n = QFileInfo(localPath).fileName();
    n.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    n.replace(QRegularExpression(QStringLiteral("\\.{2,}")), QStringLiteral("."));   // open() refuses ".."
    return n.isEmpty() ? QStringLiteral("file") : n;
}

Input replyBase(const Opened &pack)
{
    Input in;
    const QJsonObject &m = pack.manifest;
    for (const char *k : { "site", "tester", "created", "what_to_look_at" })
        in.inReplyTo.insert(QLatin1String(k), m.value(QLatin1String(k)));
    in.site = m.value(QStringLiteral("site")).toString();
    in.loco = m.value(QStringLiteral("loco")).toString();
    in.testRunPath = pack.testRunPath;
    QFile xml(pack.schemaPath);
    if (xml.open(QIODevice::ReadOnly)) in.schemaXml = xml.readAll();   // the site's: the reply decodes as theirs did
    in.schemaFrom = m.value(QStringLiteral("schema")).toObject().value(QStringLiteral("from")).toString();
    return in;
}

QString defaultDir(const QString &zipPath)
{
    const QFileInfo fi(zipPath);
    return fi.path() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral("_files");
}

Written write(const QString &zipPath, const Input &in)
{
    Written w;
    const qint64 created = in.createdMs > 0 ? in.createdMs : QDateTime::currentMSecsSinceEpoch();
    const bool all = in.fromMs <= 0 && in.toMs <= 0;
    QVector<Entry> files;
    qint64 total = 0;
    QJsonArray caps;
    QVector<CaptureSummary> summaries;
    QSet<QString> used;
    for (const Capture &c : in.captures) {
        QByteArray dlr;
        quint8 src = 0;
        quint16 kvch = 0;
        idsOf(c.tabKey, &src, &kvch);
        SessionFile::FileHeader h;
        h.createdMs = created;
        h.sourceId = src;
        h.kvchId = kvch;
        dlr += SessionFile::encodeHeader(h);
        int records = 0, keyFrames = 0;
        qint64 first = 0, last = 0;
        if (!all && in.keyFrames) {
            // The newest of each kind before From, by its whole tag
            // (@auth_keys1_1_1 and @auth_keys2_1_1 are both kept).
            QMap<QString, LogEntryPtr> lastOf;
            for (const LogEntryPtr &e : c.rows)
                if (e && e->epochMs < in.fromMs && !e->rawBytes.isEmpty() && e->rawBytes.size() <= SessionFile::kMaxWireBytes
                    && isKeyFrame(e->text))
                    lastOf.insert(e->text.section(QLatin1Char(' '), 0, 0), e);
            QVector<LogEntryPtr> keys = lastOf.values().toVector();
            std::stable_sort(keys.begin(), keys.end(), [](const LogEntryPtr &a, const LogEntryPtr &b) { return a->epochMs < b->epochMs; });
            for (const LogEntryPtr &e : keys) {
                dlr += SessionFile::encodeRecord(e->epochMs, e->rawBytes);
                if (!records) first = e->epochMs;
                last = e->epochMs;
                ++records;
                ++keyFrames;
            }
        }
        for (const LogEntryPtr &e : c.rows) {
            if (!e || e->rawBytes.isEmpty() || e->rawBytes.size() > SessionFile::kMaxWireBytes) continue;
            if (!all && (e->epochMs < in.fromMs || e->epochMs > in.toMs)) continue;
            dlr += SessionFile::encodeRecord(e->epochMs, e->rawBytes);
            if (!records) first = e->epochMs;
            last = e->epochMs;
            ++records;
            if (total + dlr.size() > kMaxBytes) {
                w.error = QStringLiteral("more than %1 MB of frames: choose a shorter span or fewer tabs")
                              .arg(kMaxBytes / (1024 * 1024));
                return w;
            }
        }
        if (records == keyFrames) continue;   // nothing recorded in the span: no empty .dlr
        QString base = captureBase(c);
        for (int n = 2; used.contains(base); ++n) base = captureBase(c) + QStringLiteral("_%1").arg(n);
        used.insert(base);
        const QString dlrName = QStringLiteral("captures/%1.dlr").arg(base);
        files.append({ dlrName, dlr });
        total += dlr.size();
        w.records += records;
        w.keyFrames += keyFrames;

        const QVector<Bookmark> marks = NotesSidecar::marksIn(c.marks, c.tabKey, all ? first : qMax(first, in.fromMs),
                                                              all ? last : qMin(last, in.toMs));
        QString notesName;
        if (!marks.isEmpty()) {
            // The sidecar as NotesSidecar writes it, through a scratch file.
            QTemporaryDir tmp;
            const QString scratch = tmp.filePath(base + QStringLiteral(".dlr"));
            QString err;
            if (!NotesSidecar::write(scratch, marks, &err)) { w.error = err; return w; }
            QFile f(NotesSidecar::pathFor(scratch));
            if (!f.open(QIODevice::ReadOnly)) { w.error = f.errorString(); return w; }
            notesName = QStringLiteral("captures/%1.notes.json").arg(base);
            files.append({ notesName, f.readAll() });
            w.notes += marks.size();
        }
        CaptureSummary cs;
        cs.file = dlrName;
        cs.marks = marks;
        {
            LogModel model(nullptr, records + 1);
            QVector<LogEntryPtr> span;
            for (const LogEntryPtr &e : c.rows)
                if (e && (all || (e->epochMs >= in.fromMs && e->epochMs <= in.toMs))) span.append(e);
            model.appendEntries(span);
            for (const QString &loco : DmiChanges::locos(&model))
                for (const DmiChanges::Change &ch : DmiChanges::list(&model, loco)) cs.dmi.append({ loco, ch });
        }
        summaries.append(cs);
        QJsonObject o;
        o.insert(QStringLiteral("tab"), c.tabKey);
        o.insert(QStringLiteral("name"), c.name);
        o.insert(QStringLiteral("file"), dlrName);
        o.insert(QStringLiteral("notes_file"), notesName);
        o.insert(QStringLiteral("records"), records);
        o.insert(QStringLiteral("notes"), marks.size());
        if (keyFrames) o.insert(QStringLiteral("key_frames"), keyFrames);
        o.insert(QStringLiteral("first_ms"), double(first));
        o.insert(QStringLiteral("last_ms"), double(last));
        o.insert(QStringLiteral("first"), timeText(first));
        o.insert(QStringLiteral("last"), timeText(last));
        caps.append(o);
    }
    if (caps.isEmpty()) {
        w.error = QStringLiteral("no recorded frames in the chosen tabs and span: nothing to pack");
        return w;
    }

    QString runName;
    if (!in.testRunPath.isEmpty()) {
        QFile f(in.testRunPath);
        if (f.open(QIODevice::ReadOnly)) {
            runName = QStringLiteral("testrun/") + QFileInfo(in.testRunPath).fileName();
            files.append({ runName, f.readAll() });
        }
    }
    QString schemaName;
    if (!in.schemaXml.isEmpty()) {
        schemaName = QStringLiteral("config/kavach.xml");
        files.append({ schemaName, in.schemaXml });
    }
    for (const auto &c : in.config) {
        QFile f(c.second);
        if (!f.open(QIODevice::ReadOnly)) continue;   // a file the site does not have is not an error
        files.append({ QStringLiteral("config/") + QFileInfo(c.first).fileName(), f.readAll() });
    }

    QJsonArray attached;
    QSet<QString> attNames;
    for (const QString &path : in.attachments) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {   // the site chose it: leaving it out silently would mislead
            w.error = QStringLiteral("cannot read the attachment %1: %2").arg(QFileInfo(path).fileName(), f.errorString());
            return w;
        }
        const QByteArray data = f.readAll();
        total += data.size();
        if (total > kMaxBytes) {
            w.error = QStringLiteral("more than %1 MB with the attachments: leave some out").arg(kMaxBytes / (1024 * 1024));
            return w;
        }
        const QString base = attachmentName(path);
        QString name = base;
        for (int n = 2; attNames.contains(name); ++n)
            name = QFileInfo(base).completeBaseName() + QStringLiteral("_%1.").arg(n) + QFileInfo(base).suffix();
        attNames.insert(name);
        files.append({ QStringLiteral("attachments/") + name, data });
        attached.append(QStringLiteral("attachments/") + name);
    }

    QJsonObject m;
    m.insert(QStringLiteral("format"), QStringLiteral("dlconsole-review-pack"));
    m.insert(QStringLiteral("version"), 1);
    m.insert(QStringLiteral("created"), timeText(created));
    m.insert(QStringLiteral("created_ms"), double(created));
    m.insert(QStringLiteral("site"), in.site);
    m.insert(QStringLiteral("tester"), in.tester);
    m.insert(QStringLiteral("loco"), in.loco);
    m.insert(QStringLiteral("what_to_look_at"), in.whatToLookAt);
    if (!in.inReplyTo.isEmpty()) m.insert(QStringLiteral("in_reply_to"), in.inReplyTo);
    QJsonObject span;
    if (!all) {
        span.insert(QStringLiteral("from_ms"), double(in.fromMs));
        span.insert(QStringLiteral("to_ms"), double(in.toMs));
        span.insert(QStringLiteral("from"), timeText(in.fromMs));
        span.insert(QStringLiteral("to"), timeText(in.toMs));
    }
    m.insert(QStringLiteral("span"), span);
    QJsonObject prog;
    prog.insert(QStringLiteral("name"), QCoreApplication::applicationName());
    prog.insert(QStringLiteral("version"), QCoreApplication::applicationVersion());
    prog.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
    prog.insert(QStringLiteral("os"), QSysInfo::prettyProductName());
    m.insert(QStringLiteral("program"), prog);
    m.insert(QStringLiteral("captures"), caps);
    m.insert(QStringLiteral("test_run"), runName);
    if (!attached.isEmpty()) m.insert(QStringLiteral("attachments"), attached);
    QJsonObject schema;
    schema.insert(QStringLiteral("file"), schemaName);
    schema.insert(QStringLiteral("from"), in.schemaFrom);
    m.insert(QStringLiteral("schema"), schema);
    QJsonArray list;
    for (const Entry &e : files) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), e.name);
        o.insert(QStringLiteral("bytes"), double(e.data.size()));
        o.insert(QStringLiteral("crc32"), hex8(XlsxBook::crc32(e.data)));
        list.append(o);
    }
    m.insert(QStringLiteral("files"), list);

    files.prepend({ QStringLiteral("summary.html"), summaryHtml(m, summaries, in.testRunPath).toUtf8() });
    files.prepend({ QStringLiteral("README.txt"), readme(m).toUtf8() });
    files.prepend({ QStringLiteral("manifest.json"), QJsonDocument(m).toJson(QJsonDocument::Indented) });

    const QByteArray zip = zipOf(files, created);
    QSaveFile f(zipPath);
    if (!f.open(QIODevice::WriteOnly)) { w.error = f.errorString(); return w; }
    f.write(zip);
    if (!f.commit()) { w.error = f.errorString(); return w; }
    for (const Entry &e : files) w.files << e.name;
    w.bytes = zip.size();
    w.ok = true;
    return w;
}

Opened open(const QString &zipPath, const QString &intoDir)
{
    Opened o;
    QFile f(zipPath);
    if (!f.open(QIODevice::ReadOnly)) {
        o.error = QStringLiteral("%1: %2").arg(QFileInfo(zipPath).fileName(), f.errorString());
        return o;
    }
    QString err;
    const QHash<QString, QByteArray> files = XlsxBook::unzipFiles(f.readAll(), &err);
    if (files.isEmpty()) {
        o.error = QStringLiteral("%1 is not a readable zip: %2").arg(QFileInfo(zipPath).fileName(), err);
        return o;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(files.value(QStringLiteral("manifest.json")), &perr);
    o.manifest = doc.object();
    if (perr.error != QJsonParseError::NoError
        || o.manifest.value(QStringLiteral("format")).toString() != QLatin1String("dlconsole-review-pack")) {
        o.error = QStringLiteral("%1 is a zip, but not a review pack (no manifest.json from DLConsole)")
                      .arg(QFileInfo(zipPath).fileName());
        return o;
    }

    // Only the pack's own folders, one level deep: a name with "..", a
    // drive or a leading slash would write outside the folder.
    static const QRegularExpression okName(
        QStringLiteral("^(manifest\\.json|README\\.txt|summary\\.html|(captures|testrun|config|attachments)/[A-Za-z0-9._ -]+)$"));
    QDir dir(intoDir);
    if (!dir.mkpath(QStringLiteral("."))) {
        o.error = QStringLiteral("cannot make the folder %1").arg(intoDir);
        return o;
    }
    // The same pack unpacked here before: the notes files beside its
    // captures hold the reviewer's notes since then, so they are kept.
    {
        QFile prev(dir.filePath(QStringLiteral("manifest.json")));
        o.reused = prev.open(QIODevice::ReadOnly) && prev.readAll() == files.value(QStringLiteral("manifest.json"));
    }
    QHash<QString, QJsonObject> listed;
    for (const QJsonValue &v : o.manifest.value(QStringLiteral("files")).toArray())
        listed.insert(v.toObject().value(QStringLiteral("name")).toString(), v.toObject());
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString name = it.key();
        if (!okName.match(name).hasMatch() || name.contains(QLatin1String(".."))) continue;
        const QJsonObject l = listed.value(name);
        if (name != QLatin1String("manifest.json") && name != QLatin1String("README.txt")
            && name != QLatin1String("summary.html")
            && (l.isEmpty() || qint64(l.value(QStringLiteral("bytes")).toDouble()) != it.value().size()
                || l.value(QStringLiteral("crc32")).toString() != hex8(XlsxBook::crc32(it.value())))) {
            o.damaged << name;
            continue;
        }
        if (o.reused && name.endsWith(QLatin1String(".notes.json")) && QFile::exists(dir.filePath(name))) continue;
        dir.mkpath(QFileInfo(name).path());
        QSaveFile out(dir.filePath(name));
        if (!out.open(QIODevice::WriteOnly) || out.write(it.value()) != it.value().size() || !out.commit()) {
            o.error = QStringLiteral("cannot write %1 in %2").arg(name, intoDir);
            return o;
        }
    }
    for (const QJsonValue &v : o.manifest.value(QStringLiteral("captures")).toArray()) {
        const QString name = v.toObject().value(QStringLiteral("file")).toString();
        if (!name.isEmpty() && !o.damaged.contains(name) && QFile::exists(dir.filePath(name)))
            o.dlrs << QDir::cleanPath(dir.absoluteFilePath(name));
    }
    const QString run = o.manifest.value(QStringLiteral("test_run")).toString();
    if (!run.isEmpty() && QFile::exists(dir.filePath(run))) o.testRunPath = dir.absoluteFilePath(run);
    const QString schema = o.manifest.value(QStringLiteral("schema")).toObject().value(QStringLiteral("file")).toString();
    if (!schema.isEmpty() && QFile::exists(dir.filePath(schema))) o.schemaPath = dir.absoluteFilePath(schema);
    for (const QJsonValue &v : o.manifest.value(QStringLiteral("attachments")).toArray()) {
        const QString name = v.toString();
        if (!name.isEmpty() && !o.damaged.contains(name) && okName.match(name).hasMatch() && QFile::exists(dir.filePath(name)))
            o.attachments << QDir::cleanPath(dir.absoluteFilePath(name));
    }
    o.dir = dir.absolutePath();
    if (o.dlrs.isEmpty()) {
        o.error = o.damaged.isEmpty() ? QStringLiteral("the pack holds no capture")
                                      : QStringLiteral("every capture in the pack is damaged: %1").arg(o.damaged.join(QStringLiteral(", ")));
        return o;
    }
    o.ok = true;
    return o;
}

QString Opened::summary() const
{
    const QString site = manifest.value(QStringLiteral("site")).toString();
    const QString tester = manifest.value(QStringLiteral("tester")).toString();
    int notes = 0;
    for (const QJsonValue &v : manifest.value(QStringLiteral("captures")).toArray())
        notes += v.toObject().value(QStringLiteral("notes")).toInt();
    QString from = site.isEmpty() ? QString() : QStringLiteral(" from %1").arg(site);
    if (!tester.isEmpty()) from += QStringLiteral(" (%1)").arg(tester);
    if (isReply()) from = QStringLiteral(" (a reply) from %1").arg(tester.isEmpty() ? QStringLiteral("the reviewer") : tester);
    QString s = QStringLiteral("Review pack%1: %2 capture(s), %3 note(s)").arg(from).arg(dlrs.size()).arg(notes);
    if (!testRunPath.isEmpty()) s += QStringLiteral(", a test run");
    if (!attachments.isEmpty()) s += QStringLiteral(", %1 attachment(s)").arg(attachments.size());
    if (reused) s += QStringLiteral("; unpacked here before, the notes made since kept");
    if (!damaged.isEmpty()) s += QStringLiteral("; DAMAGED, not opened: %1").arg(damaged.join(QStringLiteral(", ")));
    return s;
}

}  // namespace ReviewPack
