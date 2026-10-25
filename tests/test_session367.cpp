#include "testutil.h"

#include "bookmarks.h"
#include "colorrules.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "reviewpack.h"
#include "reviewpackdialog.h"
#include "sessionreader.h"
#include "sessionwindow.h"
#include "settings.h"
#include "statusline.h"
#include "testrun.h"
#include "theme.h"
#include "uistyle.h"
#include "xlsxbook.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>

// =============================================================================
//  Session 367 — the review pack: one .zip from the test site with the
//  recordings, their notes, the test run and the kavach.xml in force; opened
//  where the review happens. Real frames: 14:02-14:05 of
//  replay/loco_1_1_27062026_140226.cap, @dmi in tab 81_1, the rest in 21_1.
// =============================================================================

namespace {

qint64 at367(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-06-27T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}

QByteArray fileBytes(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString crcHex(const QByteArray &b) { return QStringLiteral("%1").arg(XlsxBook::crc32(b), 8, 16, QLatin1Char('0')); }

}  // namespace

TEST_SUITE(session367)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    QTemporaryDir tmp;

    MessageDispatcher disp;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: the capture opens");
        const qint64 from = at367("14:02:00"), to = at367("14:05:00");
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3 || !l.startsWith('@')) continue;
            const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
            if (ms < from) continue;
            if (ms >= to) break;
            disp.ingestLocal(l.startsWith("@dmi_") ? 81 : 21, 1, l, ms, QString());
        }
        disp.drainNow();
    }
    LogModel *radio = disp.modelForKey(QStringLiteral("21_1"));
    LogModel *dmi = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(radio && dmi && radio->count() > 500 && dmi->count() > 100, "fixture: two tabs of real frames");
    if (!radio || !dmi) return;

    BookmarkStore site;
    site.load(tmp.filePath(QStringLiteral("site.json")));
    const qint64 atEb = at367("14:03:10"), outside = at367("14:01:00");
    site.toggle(QStringLiteral("21_1"), atEb, QStringLiteral("@slrp …"));
    site.setNote(0, QStringLiteral("EB here, no reason on the DMI"));
    site.toggle(QStringLiteral("21_1"), outside, QStringLiteral("before the span"));
    site.toggle(QStringLiteral("81_1"), at367("14:04:00"), QStringLiteral("@dmi …"));

    ReviewPack::Capture c21{ QStringLiteral("21_1"), QStringLiteral("Loco 1"), radio->snapshot(), site.all() };
    ReviewPack::Capture c81{ QStringLiteral("81_1"), QString(), dmi->snapshot(), site.all() };
    CHECK(ReviewPack::captureBase(c21) == QLatin1String("Loco_1_21_1") && ReviewPack::captureBase(c81) == QLatin1String("81_1"),
          "capture file names: the tab's name and key, nothing a file system refuses");

    TestRun::Run run;
    run.title = QStringLiteral("Brake scenarios, run 2");
    run.loco = QStringLiteral("Loco 1");
    run.created = QDateTime::fromMSecsSinceEpoch(at367("13:00:00"));
    const QString runPath = tmp.filePath(QStringLiteral("brakes.testrun.json"));
    CHECK(TestRun::save(runPath, run), "fixture: a test run file");

    ReviewPack::Input in;
    in.site = QStringLiteral("Lingampalli yard");
    in.tester = QStringLiteral("R. Rao");
    in.loco = QStringLiteral("Loco 1");
    in.whatToLookAt = QStringLiteral("EB at 14:03:10, scenario 4.2, second run");
    in.fromMs = at367("14:02:30");
    in.toMs = at367("14:04:30");
    in.captures = { c21, c81 };
    in.testRunPath = runPath;
    in.schemaXml = fileBytes(QStringLiteral(":/schema/kavach.xml"));
    in.schemaFrom = QStringLiteral("built in");
    in.config = { { QStringLiteral("color_rules.json"), QStringLiteral(DL_SRC_DIR "/color_rules.json") },
                  { QStringLiteral("missing.csv"), tmp.filePath(QStringLiteral("not-there.csv")) } };
    in.createdMs = at367("15:00:00");
    CHECK(!in.schemaXml.isEmpty(), "fixture: the built-in kavach.xml");

    int inSpan21 = 0, inSpan81 = 0;
    for (int r = 0; r < radio->count(); ++r)
        if (radio->entryAt(r)->epochMs >= in.fromMs && radio->entryAt(r)->epochMs <= in.toMs) ++inSpan21;
    for (int r = 0; r < dmi->count(); ++r)
        if (dmi->entryAt(r)->epochMs >= in.fromMs && dmi->entryAt(r)->epochMs <= in.toMs) ++inSpan81;

    // ---- write ------------------------------------------------------------------------------
    const QString zip = tmp.filePath(QStringLiteral("ReviewPack_Lingampalli.zip"));
    const ReviewPack::Written w = ReviewPack::write(zip, in);
    CHECK(w.ok, "the pack is written: " + w.error.toUtf8());
    CHECK(w.records == inSpan21 + inSpan81 && w.notes == 2,
          QByteArray("the frames in the span, and the two notes inside it: ") + QByteArray::number(w.records)
              + " frames, " + QByteArray::number(w.notes) + " notes");
    const QStringList want = { QStringLiteral("manifest.json"), QStringLiteral("README.txt"),
                               QStringLiteral("captures/Loco_1_21_1.dlr"), QStringLiteral("captures/Loco_1_21_1.notes.json"),
                               QStringLiteral("captures/81_1.dlr"), QStringLiteral("captures/81_1.notes.json"),
                               QStringLiteral("testrun/brakes.testrun.json"), QStringLiteral("config/kavach.xml"),
                               QStringLiteral("config/color_rules.json") };
    CHECK(w.files == want, "the files, in order: " + w.files.join(QLatin1Char(',')).toUtf8());
    CHECK(w.bytes > 0 && w.bytes < in.schemaXml.size() + (inSpan21 + inSpan81) * 64,
          "deflated: smaller than its parts (" + QByteArray::number(w.bytes) + " bytes)");

    const QHash<QString, QByteArray> raw = XlsxBook::unzipFiles(fileBytes(zip));
    CHECK(raw.size() == want.size(), "a plain zip: Qt's own reader-free unzip reads every entry back");
    const QJsonObject m = QJsonDocument::fromJson(raw.value(QStringLiteral("manifest.json"))).object();
    CHECK(m.value(QStringLiteral("format")).toString() == QLatin1String("dlconsole-review-pack")
              && m.value(QStringLiteral("site")).toString() == in.site
              && m.value(QStringLiteral("what_to_look_at")).toString() == in.whatToLookAt
              && m.value(QStringLiteral("program")).toObject().value(QStringLiteral("qt")).toString() == QLatin1String(qVersion()),
          "the manifest: who, what, which program");
    bool crcs = true;
    for (const QJsonValue &v : m.value(QStringLiteral("files")).toArray()) {
        const QJsonObject f = v.toObject();
        const QByteArray b = raw.value(f.value(QStringLiteral("name")).toString());
        crcs = crcs && qint64(f.value(QStringLiteral("bytes")).toDouble()) == b.size()
               && f.value(QStringLiteral("crc32")).toString() == crcHex(b);
    }
    CHECK(crcs && m.value(QStringLiteral("files")).toArray().size() == 7, "every file listed with its size and CRC-32");
    const QString readme = QString::fromUtf8(raw.value(QStringLiteral("README.txt")));
    CHECK(readme.startsWith(QLatin1String("DLConsole review pack")) && readme.contains(in.whatToLookAt)
              && readme.contains(QLatin1String("Lingampalli yard")) && readme.contains(QLatin1String("no pass/fail judgement")),
          "README.txt: what to look at first, readable without the program");
    CHECK(raw.value(QStringLiteral("config/kavach.xml")) == in.schemaXml, "the site's kavach.xml, byte for byte");

    // ---- open --------------------------------------------------------------------------------
    CHECK(ReviewPack::defaultDir(zip) == tmp.filePath(QStringLiteral("ReviewPack_Lingampalli_files")),
          "unpacked beside the pack, into <pack>_files");
    const ReviewPack::Opened o = ReviewPack::open(zip, ReviewPack::defaultDir(zip));
    CHECK(o.ok && o.damaged.isEmpty() && o.dlrs.size() == 2, "opened: two captures, nothing damaged: " + o.error.toUtf8());
    CHECK(o.testRunPath.endsWith(QLatin1String("testrun/brakes.testrun.json")) && o.schemaPath.endsWith(QLatin1String("config/kavach.xml")),
          "the test run and the schema are found");
    CHECK(o.summary() == QLatin1String("Review pack from Lingampalli yard (R. Rao): 2 capture(s), 2 note(s), a test run"),
          "the summary line: " + o.summary().toUtf8());
    {
        SessionReader r;
        CHECK(r.open(o.dlrs.first()) && r.tabKey() == QLatin1String("21_1"), "the .dlr reads, as tab 21_1");
        int n = 0, same = 0, k = 0;
        while (r.next()) {
            ++n;
            while (k < radio->count() && radio->entryAt(k)->epochMs < in.fromMs) ++k;
            if (k < radio->count() && radio->entryAt(k)->rawBytes == r.wire() && radio->entryAt(k)->epochMs == r.arrivalMs()) ++same;
            ++k;
        }
        CHECK(n == inSpan21 && same == n, "every frame in the span, byte for byte and to the millisecond");
    }
    {
        ColorRules rules;
        NameMap names;
        BookmarkStore reviewer;
        reviewer.load(tmp.filePath(QStringLiteral("reviewer.json")));
        SessionWindow sw(&rules, &names, Theme::Light);
        sw.setAttribute(Qt::WA_DeleteOnClose, false);
        sw.setBookmarkStore(&reviewer);
        sw.setReviewPack(o);
        CHECK(sw.loadFiles(o.dlrs) == inSpan21 + inSpan81, "the viewer loads the pack's captures");
        CHECK(sw.lastMerge().added == 2 && reviewer.count() == 2, "and the site's notes come with them");
        bool shown = false;
        for (QLabel *l : sw.findChildren<QLabel *>())
            if (l->objectName() == QLatin1String("reviewPackWhat") && l->text().contains(in.whatToLookAt)) shown = true;
        CHECK(shown, "the viewer shows what to look at");
    }

    // ---- refused, and what a bad pack cannot do ----------------------------------------------
    {
        ReviewPack::Input none = in;
        none.fromMs = at367("10:00:00");
        none.toMs = at367("10:01:00");
        const ReviewPack::Written e = ReviewPack::write(tmp.filePath(QStringLiteral("empty.zip")), none);
        CHECK(!e.ok && e.error.contains(QLatin1String("nothing to pack")) && !QFile::exists(tmp.filePath(QStringLiteral("empty.zip"))),
              "no frames in the span: refused, no file: " + e.error.toUtf8());
    }
    {
        QFile f(tmp.filePath(QStringLiteral("other.zip")));
        f.open(QIODevice::WriteOnly);
        f.write(XlsxBook::zipFiles({ { QStringLiteral("a.txt"), QByteArray("hello") } }));
        f.close();
        const ReviewPack::Opened n = ReviewPack::open(f.fileName(), tmp.filePath(QStringLiteral("other")));
        CHECK(!n.ok && n.error.contains(QLatin1String("not a review pack")), "a zip that is not a pack: said so");
    }
    {
        // A pack with a name that climbs out of the folder, and a capture
        // whose bytes are not the ones listed.
        QJsonObject bad = m;
        QJsonArray files = bad.value(QStringLiteral("files")).toArray();
        QJsonObject first = files.at(0).toObject();
        first.insert(QStringLiteral("crc32"), QStringLiteral("00000000"));
        files.replace(0, first);
        bad.insert(QStringLiteral("files"), files);
        QVector<QPair<QString, QByteArray>> entries = { { QStringLiteral("manifest.json"), QJsonDocument(bad).toJson() },
                                                       { QStringLiteral("../evil.txt"), QByteArray("x") } };
        for (const QString &name : want.mid(2)) entries.append({ name, raw.value(name) });
        QFile f(tmp.filePath(QStringLiteral("bad.zip")));
        f.open(QIODevice::WriteOnly);
        f.write(XlsxBook::zipFiles(entries));
        f.close();
        const QString into = tmp.filePath(QStringLiteral("bad/inner"));
        const ReviewPack::Opened b = ReviewPack::open(f.fileName(), into);
        CHECK(!QFile::exists(tmp.filePath(QStringLiteral("bad/evil.txt"))) && !QFile::exists(into + QStringLiteral("/evil.txt")),
              "a name with \"..\" is never written");
        CHECK(b.ok && b.damaged == QStringList{ QStringLiteral("captures/Loco_1_21_1.dlr") } && b.dlrs.size() == 1
                  && b.summary().contains(QLatin1String("DAMAGED")),
              "a capture that does not match its CRC is named and not opened: " + b.summary().toUtf8());
    }

    // ---- the dialog ---------------------------------------------------------------------------
    {
        QSettings ini(Settings::iniPath(), QSettings::IniFormat);
        QHash<QString, QVariant> keep;   // the ini is shared with other suites: put it back
        for (const char *k : { "reviewpack/site", "reviewpack/tester", "reviewpack/loco" })
            keep.insert(QLatin1String(k), ini.value(QLatin1String(k)));
        ReviewPack::Input base;
        base.testRunPath = runPath;
        base.schemaXml = in.schemaXml;
        base.schemaFrom = QStringLiteral("built in");
        ReviewPackDialog dlg({ c21, c81 }, base);
        dlg.setWho(QStringLiteral("Yard 2"), QStringLiteral("Tester"), QStringLiteral("Loco 1"));
        dlg.setWhat(QStringLiteral("look at 14:03"));
        dlg.setSpan(in.fromMs, in.toMs);
        dlg.setTabChecked(QStringLiteral("81_1"), false);
        const ReviewPack::Input got = dlg.input();
        CHECK(got.captures.size() == 1 && got.captures.first().tabKey == QLatin1String("21_1") && got.fromMs == in.fromMs
                  && got.toMs == in.toMs && got.testRunPath == runPath && got.whatToLookAt == QLatin1String("look at 14:03"),
              "the dialog's choices are the pack's input");
        CHECK(dlg.countLabel()->text().startsWith(QStringLiteral("%1 frames from 1 tab(s), 1 note(s), the test run").arg(inSpan21)),
              "it says what that is: " + dlg.countLabel()->text().toUtf8());
        const QString path = tmp.filePath(QStringLiteral("dlg.zip"));
        CHECK(dlg.exportTo(path) && QFile::exists(path) && dlg.status()->text().contains(QLatin1String("dlg.zip")),
              "Export writes it and says so: " + dlg.status()->text().toUtf8());
        CHECK(QSettings(Settings::iniPath(), QSettings::IniFormat).value(QStringLiteral("reviewpack/site")).toString()
                  == QLatin1String("Yard 2"),
              "who and where are remembered for the next pack");
        for (auto it = keep.constBegin(); it != keep.constEnd(); ++it) {
            if (it.value().isValid()) ini.setValue(it.key(), it.value());
            else ini.remove(it.key());
        }
    }
}
