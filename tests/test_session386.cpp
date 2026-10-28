#include "testutil.h"

#include "colorrules.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "reviewpack.h"
#include "reviewpackdialog.h"
#include "sessionkeystore.h"
#include "sessionreader.h"
#include "sessionwindow.h"
#include "theme.h"
#include "xlsxbook.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

// =============================================================================
//  Session 386 — key frames in the review pack, and the recording's own
//  session key. A From / To span cut off the @auth_keys / @rand_num frames
//  sent at session start; ticked, the last of each kind before From goes in.
//  The recording window derives its keys from its own frames. Real frames:
//  replay/2026-10-08/loco_1_1_08102026_105200.cap (@auth_keys at 10:52:36,
//  @rand_num at 11:12:03 and 11:12:05, SLRPs from 11:12:34).
// =============================================================================

namespace {
qint64 at386(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
QByteArray bytes386(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}  // namespace

TEST_SUITE(session386)
{
    CHECK(ReviewPack::isKeyFrame(QStringLiteral("@auth_keys1_1_1 2026")) && ReviewPack::isKeyFrame(QStringLiteral("@auth_key2_1_1 x"))
              && ReviewPack::isKeyFrame(QStringLiteral("@rand_num_1_1 x")) && ReviewPack::isKeyFrame(QStringLiteral("@linfo_1_1 x"))
              && !ReviewPack::isKeyFrame(QStringLiteral("@slrp_1_1 x")) && !ReviewPack::isKeyFrame(QStringLiteral("@auth_1_1 x")),
          "which frames are key and config frames");

    QTemporaryDir tmp;
    MessageDispatcher disp;
    {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture: the capture opens");
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3 || !l.startsWith('@') || l.startsWith("@dmi_")) continue;
            disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
        }
        disp.drainNow();
    }
    LogModel *radio = disp.modelForKey(QStringLiteral("21_1"));
    CHECK(radio && radio->count() > 1000, "fixture: the run's radio and system frames");
    if (!radio) return;
    LogEntryPtr slrp;
    for (int r = 0; r < radio->count() && slrp.isNull(); ++r)
        if (radio->entryAt(r)->text.startsWith(QLatin1String("@slrp_1_1")) && radio->entryAt(r)->epochMs >= at386("11:12:40"))
            slrp = radio->entryAt(r);
    CHECK(!slrp.isNull(), "fixture: an SLRP inside the span");
    if (slrp.isNull()) return;
    SessionKeyStore::Mac atSite = SessionKeyStore::Mac::NoKey;
    {
        // The whole run up to the SLRP, as the site's live window had it.
        SessionKeyStore whole;
        for (int r = 0; r < radio->count() && radio->entryAt(r)->epochMs <= slrp->epochMs; ++r) whole.observe(radio->entryAt(r));
        atSite = whole.verifyMac(QStringLiteral("slrp"), slrp->rawBytes, 1);
        CHECK(!whole.snapshots().isEmpty() && atSite != SessionKeyStore::Mac::NoKey,
              "fixture: the run derives a session key that checks its SLRPs (verdict " + QByteArray::number(int(atSite)) + ")");
    }

    ReviewPack::Input in;
    in.site = QStringLiteral("Yard");
    in.captures = { { QStringLiteral("21_1"), QString(), radio->snapshot(), {} } };
    in.fromMs = at386("11:12:30");
    in.toMs = at386("11:13:00");
    ColorRules rules;
    NameMap names;

    // ---- not ticked: the span alone, no key ---------------------------------------------------
    const QString plainZip = tmp.filePath(QStringLiteral("Plain.zip"));
    const ReviewPack::Written p = ReviewPack::write(plainZip, in);
    CHECK(p.ok && p.keyFrames == 0, "not ticked: no key frames");
    {
        const ReviewPack::Opened o = ReviewPack::open(plainZip, ReviewPack::defaultDir(plainZip));
        SessionWindow sw(&rules, &names, Theme::Light);
        sw.setAttribute(Qt::WA_DeleteOnClose, false);
        sw.loadFiles(o.dlrs);
        CHECK(!sw.keys(), "the span alone holds no session key, and none is made up");
    }

    // ---- ticked ------------------------------------------------------------------------------
    in.keyFrames = true;
    const QString zip = tmp.filePath(QStringLiteral("Keys.zip"));
    const ReviewPack::Written w = ReviewPack::write(zip, in);
    CHECK(w.ok && w.keyFrames == 3 && w.records == p.records + 3,
          "ticked: @auth_keys1, @auth_keys2 and @rand_num, the newest of each: " + QByteArray::number(w.keyFrames));
    const QHash<QString, QByteArray> raw = XlsxBook::unzipFiles(bytes386(zip));
    const QJsonObject cap = QJsonDocument::fromJson(raw.value(QStringLiteral("manifest.json"))).object()
                                .value(QStringLiteral("captures")).toArray().first().toObject();
    CHECK(cap.value(QStringLiteral("key_frames")).toInt() == 3, "the manifest counts them");
    CHECK(QString::fromUtf8(raw.value(QStringLiteral("README.txt"))).contains(QLatin1String("they carry key material")),
          "README.txt says what they carry");
    const ReviewPack::Opened o = ReviewPack::open(zip, ReviewPack::defaultDir(zip));
    {
        SessionReader r;
        CHECK(r.open(o.dlrs.first()), "the .dlr opens");
        QList<qint64> times;
        while (r.next() && times.size() < 4) times << r.arrivalMs();
        CHECK(times.size() == 4 && times.at(0) <= times.at(1) && times.at(1) <= times.at(2)
                  && times.at(2) < in.fromMs && times.at(3) >= in.fromMs,
              "the key frames come first, before From, then the span");
        CHECK(QDateTime::fromMSecsSinceEpoch(times.at(2)).toString(QStringLiteral("HH:mm:ss")) == QLatin1String("11:12:05"),
              "the newest @rand_num (11:12:05), not the first");
    }
    {
        SessionKeyStore live;   // the live window's: nothing from this run
        SessionWindow sw(&rules, &names, Theme::Light);
        sw.setAttribute(Qt::WA_DeleteOnClose, false);
        sw.setSessionKeys(&live);
        sw.loadFiles(o.dlrs);
        CHECK(sw.keys() && sw.keys() != &live && !sw.keys()->snapshots().isEmpty(),
              "the window derives the session key from the recording, not the live one");
        CHECK(sw.keys()->verifyMac(QStringLiteral("slrp"), slrp->rawBytes, 1) == atSite,
              "and the span's SLRP gets the MAC result the whole run gives it");
    }
    {
        SessionKeyStore live;
        SessionWindow sw(&rules, &names, Theme::Light);
        sw.setAttribute(Qt::WA_DeleteOnClose, false);
        sw.setSessionKeys(&live);
        sw.loadFiles(ReviewPack::open(plainZip, ReviewPack::defaultDir(plainZip)).dlrs);
        CHECK(sw.keys() == &live, "a recording with no key of its own falls back to the live window's");
    }

    // ---- Everything: nothing doubled; only key frames: nothing to pack ------------------------
    ReviewPack::Input every = in;
    every.fromMs = every.toMs = 0;
    const ReviewPack::Written e = ReviewPack::write(tmp.filePath(QStringLiteral("All.zip")), every);
    CHECK(e.ok && e.keyFrames == 0 && e.records == radio->count(), "with Everything every frame is in once");
    ReviewPack::Input none = in;
    none.fromMs = at386("23:00:00");
    none.toMs = at386("23:01:00");
    CHECK(!ReviewPack::write(tmp.filePath(QStringLiteral("None.zip")), none).ok, "key frames alone are not a pack");

    // ---- the dialog --------------------------------------------------------------------------
    ReviewPackDialog dlg(in.captures, ReviewPack::Input());
    QCheckBox *box = nullptr;
    for (QCheckBox *c : dlg.findChildren<QCheckBox *>())
        if (c->objectName() == QLatin1String("reviewPackKeyFrames")) box = c;
    CHECK(box && !box->isChecked() && !box->isEnabled() && !dlg.input().keyFrames, "off by default, and greyed with Everything");
    dlg.setSpan(in.fromMs, in.toMs);
    CHECK(box && box->isEnabled() && !dlg.input().keyFrames, "a span offers it, still off");
    dlg.setKeyFrames(true);
    CHECK(dlg.input().keyFrames, "ticked, the pack takes them");
}
