#include "testutil.h"

#include "bookmarks.h"
#include "colorrules.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "reviewpack.h"
#include "reviewpackdialog.h"
#include "sessionwindow.h"
#include "settings.h"
#include "theme.h"
#include "xlsxbook.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

// =============================================================================
//  Session 384 — the reply pack: the reviewer answers the site with the same
//  captures, their own notes added, and the answer; the site opens it and
//  sees the reviewer's notes on its rows. Real frames: 14:02-14:03 of
//  replay/loco_1_1_27062026_140226.cap.
// =============================================================================

namespace {
qint64 at384(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-06-27T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
QByteArray bytes384(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
template <class T> T *named384(QWidget *w, const char *name)
{
    for (T *c : w->findChildren<T *>())
        if (c->objectName() == QLatin1String(name)) return c;
    return nullptr;
}
}  // namespace

TEST_SUITE(session384)
{
    QTemporaryDir tmp;
    MessageDispatcher disp;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: the capture opens");
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3 || !l.startsWith('@')) continue;
            const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
            if (ms < at384("14:02:00")) continue;
            if (ms >= at384("14:03:00")) break;
            disp.ingestLocal(21, 1, l, ms, QString());
        }
        disp.drainNow();
    }
    LogModel *radio = disp.modelForKey(QStringLiteral("21_1"));
    CHECK(radio && radio->count() > 100, "fixture: a tab of real frames");
    if (!radio) return;
    const qint64 siteMark = radio->entryAt(50)->epochMs, reviewerMark = radio->entryAt(200)->epochMs;

    // ---- the site's pack ---------------------------------------------------------------------
    BookmarkStore site;
    site.load(tmp.filePath(QStringLiteral("site.json")));
    site.toggle(QStringLiteral("21_1"), siteMark, QStringLiteral("row 50"));
    site.setNote(0, QStringLiteral("site: EB here?"));
    ReviewPack::Input in;
    in.site = QStringLiteral("Yard");
    in.tester = QStringLiteral("R. Rao");
    in.loco = QStringLiteral("Loco 1");
    in.whatToLookAt = QStringLiteral("Why the EB at 14:02?");
    in.captures = { { QStringLiteral("21_1"), QStringLiteral("Loco 1"), radio->snapshot(), site.all() } };
    in.schemaXml = bytes384(QStringLiteral(":/schema/kavach.xml")) + "<!-- the site's -->";
    in.schemaFrom = QStringLiteral("D:/site/kavach.xml");
    in.createdMs = at384("15:00:00");
    const QString packZip = tmp.filePath(QStringLiteral("Pack.zip"));
    CHECK(ReviewPack::write(packZip, in).ok, "fixture: the site's pack");

    QSettings ini(Settings::iniPath(), QSettings::IniFormat);
    const QVariant iniSite = ini.value(QStringLiteral("reviewpack/site"));
    ini.setValue(QStringLiteral("reviewpack/site"), QStringLiteral("reviewer's own"));
    ini.sync();

    // ---- the reviewer answers ----------------------------------------------------------------
    const QString replyZip = tmp.filePath(QStringLiteral("Reply.zip"));
    {
        const ReviewPack::Opened o = ReviewPack::open(packZip, ReviewPack::defaultDir(packZip));
        CHECK(o.ok && !o.isReply(), "the reviewer opens the pack");
        ColorRules rules;
        NameMap names;
        BookmarkStore reviewer;
        reviewer.load(tmp.filePath(QStringLiteral("reviewer.json")));
        SessionWindow sw(&rules, &names, Theme::Light);
        sw.setAttribute(Qt::WA_DeleteOnClose, false);
        sw.setBookmarkStore(&reviewer);
        sw.setReviewPack(o);
        sw.loadFiles(o.dlrs);
        QPushButton *btn = named384<QPushButton>(&sw, "reviewPackReply");
        CHECK(btn && btn->text() == QStringLiteral("Reply to the site…"), "the panel offers a reply");
        reviewer.toggle(QStringLiteral("21_1"), reviewerMark, QStringLiteral("row 200"));
        for (int i = 0; i < reviewer.count(); ++i)
            if (reviewer.all().at(i).epochMs == reviewerMark) reviewer.setNote(i, QStringLiteral("reviewer: the cause is here"));

        ReviewPackDialog *d = sw.makeReplyDialog();
        CHECK(d && d->windowTitle() == QLatin1String("Reply to the site"), "Reply to the site… opens the dialog as a reply");
        if (!d) return;
        const ReviewPack::Input ri = d->input();
        CHECK(ri.site == QLatin1String("Yard") && ri.loco == QLatin1String("Loco 1") && ri.tester.isEmpty(),
              "the site and loco are the pack's; the reviewer's name is theirs to give");
        CHECK(ri.schemaXml == in.schemaXml && ri.schemaFrom == in.schemaFrom, "the reply carries the site's own kavach.xml");
        d->setWho(QStringLiteral("Yard"), QStringLiteral("A. Reviewer"), QStringLiteral("Loco 1"));
        d->setWhat(QStringLiteral("The EB follows the SLRP at row 200."));
        CHECK(d->exportTo(replyZip), "the reply is written: " + QByteArray());
        CHECK(QSettings(Settings::iniPath(), QSettings::IniFormat).value(QStringLiteral("reviewpack/site")).toString()
                  == QLatin1String("reviewer's own"),
              "the site's name is not remembered as the reviewer's own");
        delete d;
    }
    if (iniSite.isValid()) ini.setValue(QStringLiteral("reviewpack/site"), iniSite);
    else ini.remove(QStringLiteral("reviewpack/site"));

    const QHash<QString, QByteArray> raw = XlsxBook::unzipFiles(bytes384(replyZip));
    const QJsonObject m = QJsonDocument::fromJson(raw.value(QStringLiteral("manifest.json"))).object();
    const QJsonObject to = m.value(QStringLiteral("in_reply_to")).toObject();
    CHECK(to.value(QStringLiteral("what_to_look_at")).toString() == in.whatToLookAt
              && to.value(QStringLiteral("tester")).toString() == in.tester
              && to.value(QStringLiteral("created")).toString() == QLatin1String("2026-06-27 15:00:00"),
          "the manifest names the pack it answers");
    CHECK(QString::fromUtf8(raw.value(QStringLiteral("README.txt"))).contains(QLatin1String("A REPLY to the pack of 2026-06-27 15:00:00 from Yard"))
              && QString::fromUtf8(raw.value(QStringLiteral("summary.html"))).contains(QLatin1String("<h1>DLConsole review pack: a reply</h1>")),
          "README.txt and summary.html say it is a reply");

    // ---- the site opens the reply ------------------------------------------------------------
    const ReviewPack::Opened r = ReviewPack::open(replyZip, ReviewPack::defaultDir(replyZip));
    CHECK(r.ok && r.isReply() && r.summary().startsWith(QLatin1String("Review pack (a reply) from A. Reviewer: 1 capture(s), 2 note(s)")),
          "opened as a reply: " + r.summary().toUtf8());
    {
        ColorRules rules;
        NameMap names;
        SessionWindow sw(&rules, &names, Theme::Light);
        sw.setAttribute(Qt::WA_DeleteOnClose, false);
        sw.setBookmarkStore(&site);
        sw.setReviewPack(r);
        sw.loadFiles(r.dlrs);
        CHECK(sw.lastMerge().added == 1 && sw.lastMerge().differ == 0 && site.count() == 2,
              "the reviewer's note comes onto the site's rows; the site's own is unchanged");
        bool reviewerNote = false;
        for (const Bookmark &b : site.all()) reviewerNote |= b.epochMs == reviewerMark && b.note == QLatin1String("reviewer: the cause is here");
        CHECK(reviewerNote, "with its words");
        QLabel *what = named384<QLabel>(&sw, "reviewPackWhat");
        CHECK(what && what->text().contains(QLatin1String("A reply")) && what->text().contains(in.whatToLookAt)
                  && what->text().contains(QLatin1String("The EB follows the SLRP at row 200.")),
              "the panel shows what was asked and the answer");
        QPushButton *btn = named384<QPushButton>(&sw, "reviewPackReply");
        CHECK(btn && btn->text() == QStringLiteral("Answer the reply…"), "and the site can answer back");
    }
    CHECK(!SessionWindow(nullptr, nullptr, Theme::Light).makeReplyDialog(), "a window with no pack offers no reply");
}
