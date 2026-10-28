#include "testutil.h"

#include "bookmarks.h"
#include "dmichanges.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "reviewpack.h"
#include "testrun.h"
#include "xlsxbook.h"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 383 — summary.html in the review pack: what to look at, the
//  notes, what changed on the DMI, the test run and the attachments, for a
//  reader without DLConsole. Real frames: the 2026-10-08 run
//  replay/2026-10-08/loco_1_1_08102026_105200.cap (the DMI changes mode and
//  brake, session 375), @dmi in tab 81_1, the rest in 21_1.
// =============================================================================

namespace {
qint64 at383(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session383)
{
    QTemporaryDir tmp;
    MessageDispatcher disp;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: the capture opens");
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3 || !l.startsWith('@')) continue;
            const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
            disp.ingestLocal(l.startsWith("@dmi_") ? 81 : 21, 1, l, ms, QString());
        }
        disp.drainNow();
    }
    LogModel *radio = disp.modelForKey(QStringLiteral("21_1"));
    LogModel *dmi = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(radio && dmi && dmi->count() > 100, "fixture: two tabs of real frames");
    if (!radio || !dmi) return;
    int changes = 0;
    for (const QString &loco : DmiChanges::locos(dmi)) changes += DmiChanges::list(dmi, loco).size();
    CHECK(changes > 0, "fixture: the DMI changes in this run");

    BookmarkStore site;
    site.load(tmp.filePath(QStringLiteral("site.json")));
    site.toggle(QStringLiteral("21_1"), at383("10:53:00"), QStringLiteral("@slrp row"));
    site.setNote(0, QStringLiteral("EB here, <no> reason"));

    TestRun::Run run;
    run.title = QStringLiteral("Brake scenarios, run 2");
    run.testers = QStringLiteral("R. Rao");
    TestRun::Result r;
    r.verdict = TestRun::Verdict::Fail;
    r.tester = QStringLiteral("R. Rao");
    r.remarks = QStringLiteral("EB late");
    TestRun::setResult(&run, QStringLiteral("4.2.1"), r);
    const QString runPath = tmp.filePath(QStringLiteral("brakes.testrun.json"));
    CHECK(TestRun::save(runPath, run), "fixture: a test run with one result");

    ReviewPack::Input in;
    in.site = QStringLiteral("Yard & line 3");
    in.whatToLookAt = QStringLiteral("EB at 14:03:10 <scenario 4.2>");
    in.captures = { { QStringLiteral("21_1"), QString(), radio->snapshot(), site.all() },
                    { QStringLiteral("81_1"), QString(), dmi->snapshot(), site.all() } };
    in.testRunPath = runPath;
    in.attachments = QStringList{ QStringLiteral(DL_SRC_DIR "/myapp_icon.png") };
    in.createdMs = at383("15:00:00");
    const QString zip = tmp.filePath(QStringLiteral("Pack.zip"));
    const ReviewPack::Written w = ReviewPack::write(zip, in);
    CHECK(w.ok && w.files.value(2) == QLatin1String("summary.html"), "summary.html is the third file: " + w.error.toUtf8());

    QFile zf(zip);
    zf.open(QIODevice::ReadOnly);
    const QString h = QString::fromUtf8(XlsxBook::unzipFiles(zf.readAll()).value(QStringLiteral("summary.html")));
    CHECK(h.startsWith(QLatin1String("<!doctype html>")) && h.contains(QLatin1String("<meta charset=\"utf-8\">")),
          "a complete page");
    CHECK(h.contains(QLatin1String("EB at 14:03:10 &lt;scenario 4.2&gt;")) && h.contains(QLatin1String("Yard &amp; line 3"))
              && !h.contains(QLatin1String("<scenario")),
          "what to look at and the site, escaped");
    CHECK(h.contains(QLatin1String("<h2>Notes (1)</h2>")) && h.contains(QLatin1String("2026-10-08 10:53:00.000"))
              && h.contains(QLatin1String("EB here, &lt;no&gt; reason")),
          "the note with its time to the millisecond");
    CHECK(h.contains(QStringLiteral("<h2>What changed on the DMI (%1)</h2>").arg(changes)),
          "every change on the DMI, as the DMI-changes window lists them (" + QByteArray::number(changes) + ")");
    CHECK(h.contains(QLatin1String("<td>4.2.1</td><td>Fail</td><td>R. Rao</td><td>EB late</td>"))
              && h.contains(QLatin1String("as recorded at the site")),
          "the test run's result, said as the site's");
    CHECK(h.contains(QLatin1String("<img src=\"attachments/myapp_icon.png\"")), "an image attachment shown inline");
    CHECK(h.contains(QLatin1String("It makes no pass/fail judgement")), "and no judgement of its own");

    const ReviewPack::Opened o = ReviewPack::open(zip, ReviewPack::defaultDir(zip));
    CHECK(o.ok && o.damaged.isEmpty() && QFile::exists(o.dir + QStringLiteral("/summary.html")),
          "unpacked beside the captures, not counted as damaged");

    // A span with no DMI change and no notes says so.
    in.fromMs = at383("10:52:00");
    in.toMs = at383("10:52:01");
    in.testRunPath.clear();
    in.attachments.clear();
    const ReviewPack::Written q = ReviewPack::write(tmp.filePath(QStringLiteral("quiet.zip")), in);
    QFile qf(tmp.filePath(QStringLiteral("quiet.zip")));
    qf.open(QIODevice::ReadOnly);
    const QString qh = QString::fromUtf8(XlsxBook::unzipFiles(qf.readAll()).value(QStringLiteral("summary.html")));
    CHECK(q.ok && qh.contains(QLatin1String("No notes in the span.")) && qh.contains(QLatin1String("No change on a DMI"))
              && !qh.contains(QLatin1String("<h2>The test run")) && !qh.contains(QLatin1String("<h2>Attachments")),
          "a quiet span: no notes, no change, said; no run, no attachments, no section");
}
