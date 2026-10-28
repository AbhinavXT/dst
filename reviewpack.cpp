#include "reviewpack.h"

#include "notessidecar.h"
#include "sessionfile.h"
#include "xlsxbook.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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
    line("Site:   ", "site");
    line("Tester: ", "tester");
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
    }
    const QString run = m.value(QStringLiteral("test_run")).toString();
    t += QStringLiteral("\nTest run: %1\n").arg(run.isEmpty() ? QStringLiteral("none") : run);
    const QJsonObject schema = m.value(QStringLiteral("schema")).toObject();
    t += QStringLiteral("Schema:   %1 (the site's: %2)\n\n")
             .arg(schema.value(QStringLiteral("file")).toString(), schema.value(QStringLiteral("from")).toString());
    t += QStringLiteral("Open it with DLConsole: File > Open review pack...\n"
                        "Each .dlr also opens on its own with File > Open recorded session...\n"
                        "This pack holds what was recorded. It makes no pass/fail judgement.\n");
    return t;
}

}  // namespace

QString captureBase(const Capture &c)
{
    QString n = c.name.isEmpty() || c.name == c.tabKey ? QString() : c.name + QLatin1Char('_');
    n += c.tabKey;
    n.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    return n;
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
        int records = 0;
        qint64 first = 0, last = 0;
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
        if (!records) continue;   // nothing recorded in the span: no empty .dlr
        QString base = captureBase(c);
        for (int n = 2; used.contains(base); ++n) base = captureBase(c) + QStringLiteral("_%1").arg(n);
        used.insert(base);
        const QString dlrName = QStringLiteral("captures/%1.dlr").arg(base);
        files.append({ dlrName, dlr });
        total += dlr.size();
        w.records += records;

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
        QJsonObject o;
        o.insert(QStringLiteral("tab"), c.tabKey);
        o.insert(QStringLiteral("name"), c.name);
        o.insert(QStringLiteral("file"), dlrName);
        o.insert(QStringLiteral("notes_file"), notesName);
        o.insert(QStringLiteral("records"), records);
        o.insert(QStringLiteral("notes"), marks.size());
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

    QJsonObject m;
    m.insert(QStringLiteral("format"), QStringLiteral("dlconsole-review-pack"));
    m.insert(QStringLiteral("version"), 1);
    m.insert(QStringLiteral("created"), timeText(created));
    m.insert(QStringLiteral("created_ms"), double(created));
    m.insert(QStringLiteral("site"), in.site);
    m.insert(QStringLiteral("tester"), in.tester);
    m.insert(QStringLiteral("loco"), in.loco);
    m.insert(QStringLiteral("what_to_look_at"), in.whatToLookAt);
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
        QStringLiteral("^(manifest\\.json|README\\.txt|(captures|testrun|config)/[A-Za-z0-9._ -]+)$"));
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
    QString s = QStringLiteral("Review pack%1: %2 capture(s), %3 note(s)").arg(from).arg(dlrs.size()).arg(notes);
    if (!testRunPath.isEmpty()) s += QStringLiteral(", a test run");
    if (reused) s += QStringLiteral("; unpacked here before, the notes made since kept");
    if (!damaged.isEmpty()) s += QStringLiteral("; DAMAGED, not opened: %1").arg(damaged.join(QStringLiteral(", ")));
    return s;
}

}  // namespace ReviewPack
