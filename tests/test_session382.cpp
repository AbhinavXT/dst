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
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>

// =============================================================================
//  Session 382 — attachments in the review pack: files the site adds (a DMI
//  clip, a photo of the cab) go into attachments/, listed in the manifest
//  with their CRC, and are shown in the Review pack panel. Real frames:
//  14:02-14:03 of replay/loco_1_1_27062026_140226.cap.
// =============================================================================

namespace {
qint64 at382(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-06-27T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
QByteArray bytes382(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
bool write382(const QString &path, const QByteArray &b)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(b) == b.size();
}
}  // namespace

TEST_SUITE(session382)
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
            if (ms < at382("14:02:00")) continue;
            if (ms >= at382("14:03:00")) break;
            disp.ingestLocal(21, 1, l, ms, QString());
        }
        disp.drainNow();
    }
    LogModel *radio = disp.modelForKey(QStringLiteral("21_1"));
    CHECK(radio && radio->count() > 100, "fixture: a tab of real frames");
    if (!radio) return;

    // Two files with one name from two folders, and a name a pack refuses.
    const QString png = QStringLiteral(DL_SRC_DIR "/myapp_icon.png");
    const QByteArray pngBytes = bytes382(png);
    CHECK(!pngBytes.isEmpty(), "fixture: a real image to attach");
    QDir(tmp.path()).mkpath(QStringLiteral("b"));
    const QString note = tmp.filePath(QStringLiteral("cab photo..notes (1).txt"));
    const QString sameName = tmp.filePath(QStringLiteral("b/myapp_icon.png"));
    CHECK(write382(note, "cab photo taken 14:02:40") && write382(sameName, "other"), "fixture: two more files");
    CHECK(ReviewPack::attachmentName(note) == QLatin1String("cab_photo.notes_1_.txt"),
          "a name is made safe for the pack: " + ReviewPack::attachmentName(note).toUtf8());

    ReviewPack::Input in;
    in.site = QStringLiteral("Yard");
    in.captures = { { QStringLiteral("21_1"), QString(), radio->snapshot(), {} } };
    in.attachments = QStringList{ png, sameName, note };
    in.createdMs = at382("15:00:00");
    const QString zip = tmp.filePath(QStringLiteral("Pack.zip"));
    const ReviewPack::Written w = ReviewPack::write(zip, in);
    CHECK(w.ok, "the pack is written: " + w.error.toUtf8());
    CHECK(w.files.mid(w.files.size() - 3)
              == QStringList({ QStringLiteral("attachments/myapp_icon.png"), QStringLiteral("attachments/myapp_icon_2.png"),
                               QStringLiteral("attachments/cab_photo.notes_1_.txt") }),
          "three attachments, the second name made unique: " + w.files.join(QLatin1Char(',')).toUtf8());
    const QHash<QString, QByteArray> raw = XlsxBook::unzipFiles(bytes382(zip));
    const QJsonObject m = QJsonDocument::fromJson(raw.value(QStringLiteral("manifest.json"))).object();
    CHECK(m.value(QStringLiteral("attachments")).toArray().size() == 3 && raw.value(QStringLiteral("attachments/myapp_icon.png")) == pngBytes,
          "listed in the manifest, the image byte for byte");
    CHECK(QString::fromUtf8(raw.value(QStringLiteral("README.txt"))).contains(QLatin1String("Attachments:\n  attachments/myapp_icon.png")),
          "and in README.txt");

    const ReviewPack::Opened o = ReviewPack::open(zip, ReviewPack::defaultDir(zip));
    CHECK(o.ok && o.attachments.size() == 3 && bytes382(o.attachments.first()) == pngBytes,
          "opened: the three attachments unpacked: " + o.error.toUtf8());
    CHECK(o.summary().endsWith(QLatin1String(", 3 attachment(s)")), "the summary counts them: " + o.summary().toUtf8());
    {
        ColorRules rules;
        NameMap names;
        SessionWindow sw(&rules, &names, Theme::Light);
        sw.setAttribute(Qt::WA_DeleteOnClose, false);
        sw.setReviewPack(o);
        QLabel *att = nullptr;
        for (QLabel *l : sw.findChildren<QLabel *>())
            if (l->objectName() == QLatin1String("reviewPackAttachments")) att = l;
        CHECK(att && att->text().contains(QLatin1String("myapp_icon_2.png")) && att->openExternalLinks(),
              "the panel links each attachment");
    }

    // Damaged in the zip: named, not offered.
    {
        QVector<QPair<QString, QByteArray>> entries;
        for (auto it = raw.constBegin(); it != raw.constEnd(); ++it)
            entries.append({ it.key(), it.key() == QLatin1String("attachments/myapp_icon_2.png") ? QByteArray("tampered") : it.value() });
        CHECK(write382(tmp.filePath(QStringLiteral("bad.zip")), XlsxBook::zipFiles(entries)), "fixture: a pack with a changed attachment");
        const ReviewPack::Opened b = ReviewPack::open(tmp.filePath(QStringLiteral("bad.zip")), tmp.filePath(QStringLiteral("bad")));
        CHECK(b.ok && b.attachments.size() == 2 && b.damaged == QStringList{ QStringLiteral("attachments/myapp_icon_2.png") },
              "a changed attachment is named as damaged and not offered");
    }

    // An attachment that cannot be read refuses the pack, by name.
    in.attachments = QStringList{ tmp.filePath(QStringLiteral("gone.gif")) };
    const ReviewPack::Written gone = ReviewPack::write(tmp.filePath(QStringLiteral("gone.zip")), in);
    CHECK(!gone.ok && gone.error.contains(QLatin1String("gone.gif")) && !QFile::exists(tmp.filePath(QStringLiteral("gone.zip"))),
          "a missing attachment refuses the pack: " + gone.error.toUtf8());

    // The dialog: Add files… per file, once each, in the input and the count.
    {
        QSettings ini(Settings::iniPath(), QSettings::IniFormat);
        const QVariant site = ini.value(QStringLiteral("reviewpack/site"));
        ReviewPackDialog dlg(in.captures, ReviewPack::Input());
        dlg.addAttachment(png);
        dlg.addAttachment(png);
        dlg.addAttachment(note);
        CHECK(dlg.input().attachments == QStringList({ png, note }), "the dialog's attachments, each once");
        CHECK(dlg.countLabel()->text().contains(QString::fromUtf8(", 2 attachment(s) — about")),
              "the count line says so: " + dlg.countLabel()->text().toUtf8());
        if (site.isValid()) ini.setValue(QStringLiteral("reviewpack/site"), site);
    }
}
