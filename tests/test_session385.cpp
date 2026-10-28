#include "testutil.h"

#include "colorrules.h"
#include "dmiclipdialog.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "radiochartdialog.h"
#include "reviewpack.h"
#include "reviewpackdialog.h"
#include "sessionwindow.h"
#include "theme.h"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

// =============================================================================
//  Session 385 — what the reviewer makes in a pack's window (a DMI clip, a
//  radio chart) is already attached when they reply. Real frames: 10:52-10:55
//  of replay/2026-10-08/loco_1_1_08102026_105200.cap, @dmi in tab 81_1.
// =============================================================================

namespace {
qint64 at385(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session385)
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
            if (ms >= at385("10:55:00")) break;
            disp.ingestLocal(l.startsWith("@dmi_") ? 81 : 21, 1, l, ms, QString());
        }
        disp.drainNow();
    }
    LogModel *radio = disp.modelForKey(QStringLiteral("21_1"));
    LogModel *dmi = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(radio && dmi && dmi->count() > 20, "fixture: two tabs of real frames (28 @dmi)");
    if (!radio || !dmi) return;

    ReviewPack::Input in;
    in.site = QStringLiteral("Yard");
    in.captures = { { QStringLiteral("21_1"), QString(), radio->snapshot(), {} },
                    { QStringLiteral("81_1"), QString(), dmi->snapshot(), {} } };
    const QString zip = tmp.filePath(QStringLiteral("Pack.zip"));
    CHECK(ReviewPack::write(zip, in).ok, "fixture: the site's pack");
    const ReviewPack::Opened o = ReviewPack::open(zip, ReviewPack::defaultDir(zip));

    ColorRules rules;
    NameMap names;
    SessionWindow sw(&rules, &names, Theme::Light);
    sw.setAttribute(Qt::WA_DeleteOnClose, false);
    sw.setReviewPack(o);
    sw.loadFiles(o.dlrs);

    const LogEntryPtr row = dmi->entryAt(dmi->count() / 2);
    const QString gif = tmp.filePath(QStringLiteral("clip.gif")), svg = tmp.filePath(QStringLiteral("radio.svg"));
    {
        QString why;
        DmiClipDialog *clip = sw.makeClipDialog(QStringLiteral("81_1"), row, &why);
        CHECK(clip, "a DMI clip from the pack's window: " + why.toUtf8());
        if (!clip) return;
        clip->setRange(row->epochMs - 3000, row->epochMs + 3000);
        CHECK(clip->exportTo(gif), "fixture: the clip is written");
        CHECK(!clip->exportTo(tmp.filePath(QStringLiteral("no/such/dir/x.gif"))), "fixture: a clip that cannot be written");
        delete clip;
    }
    {
        RadioChartDialog *chart = sw.makeRadioChart(QStringLiteral("21_1"), row->epochMs);
        CHECK(chart && chart->exportTo(svg), "fixture: a radio chart is written");
        chart->exportTo(svg);   // the same file again
        delete chart;
    }
    CHECK(sw.madeHere() == QStringList({ gif, svg }), "the window keeps what it wrote, each once, nothing that failed");

    ReviewPackDialog *d = sw.makeReplyDialog();
    CHECK(d && d->input().attachments == QStringList({ gif, svg }), "the reply starts with both attached");
    delete d;
    QFile::remove(gif);
    d = sw.makeReplyDialog();
    CHECK(d && d->input().attachments == QStringList{ svg }, "a file deleted since is not attached");
    delete d;

    // A window that holds no pack has no reply, so it keeps nothing.
    SessionWindow plain(&rules, &names, Theme::Light);
    plain.setAttribute(Qt::WA_DeleteOnClose, false);
    plain.loadFiles(o.dlrs);
    RadioChartDialog *chart = plain.makeRadioChart(QStringLiteral("21_1"), row->epochMs);
    CHECK(chart && chart->exportTo(tmp.filePath(QStringLiteral("plain.svg"))) && plain.madeHere().isEmpty(),
          "outside a pack, nothing is collected");
    delete chart;
}
