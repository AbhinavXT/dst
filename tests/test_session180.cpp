#include "testutil.h"

#include "laneband.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "runreport.h"
#include "tagcheck.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QVBoxLayout>

// =============================================================================
//  Session 180 — RFID tag check.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_105200.cap (81_1).
//  Expected, from schema/engine.py: 15 tags read (800 802 804 2 4 6 8 10 12
//  14 16 18 906 898 900), main tag each; the duplicate read only for 906,
//  898 and 900; NMS DUPLICATE_MISSING_RFID names tag 18 at 11:12:33; the
//  SLRP route lists name 12 14 16 18 906 898 900 902 904, so 7 of the read
//  tags were on a route; no route tag passed without a read (none all day
//  in 81_1 either, 183 distinct reads).
// =============================================================================

namespace {
qint64 at180(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session180)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    MessageDispatcher disp;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
    CHECK(f.open(QIODevice::ReadOnly), "fixture");
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        disp.ingestLocal(81, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    disp.drainNow();
    LogModel *model = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(model != nullptr, "fixture: the tab");
    if (!model) return;

    const TagCheck::Report r = TagCheck::build(model);
    QStringList order;
    bool allMain = true;
    for (const TagCheck::Tag &t : r.tags) { order << t.tag; allMain &= t.main; }
    CHECK(order == (QStringList{ "800", "802", "804", "2", "4", "6", "8", "10", "12", "14", "16", "18", "906", "898", "900" }) && allMain,
          QByteArray("15 tags, in order of first read, each main tag read (") + order.join(QLatin1Char(',')).toLatin1() + ")");
    QStringList dup, routed;
    for (const TagCheck::Tag &t : r.tags) { if (t.duplicate) dup << t.tag; if (t.onRoute) routed << t.tag; }
    CHECK(dup == (QStringList{ "906", "898", "900" }) && r.duplicatesRead() == 3, "the duplicate read only for 906, 898, 900");
    CHECK(routed == (QStringList{ "12", "14", "16", "18", "906", "898", "900" }), "7 of them on an SLRP route list");
    const TagCheck::Tag &t18 = r.tags.at(order.indexOf(QStringLiteral("18")));
    CHECK(t18.duplicateMissingMs == at180("11:12:33") && r.duplicatesReportedMissing() == 1,
          "the NMS reports tag 18's duplicate missing, at 11:12:33");
    CHECK(r.skipped.isEmpty(), "no route tag passed without a read");

    // The rule for a skipped tag, on the same real frames with some left out:
    // tag 18's two reads (11:12:28, 11:12:33) and the SLRP frames from 11:12:38 on,
    // so the latest route list is 11:12:14's (... 16 18 906 ...) when 906 is read
    // after 16. Nothing is altered, only left out.
    {
        LogModel cut(nullptr, 20000);
        QVector<LogEntryPtr> v;
        for (int i = 0; i < model->count(); ++i) {
            const LogEntryPtr e = model->entryAt(i);
            if (e->text.startsWith(QLatin1String("@rfid_")) && (e->text.contains(QLatin1String("T11:12:28")) || e->text.contains(QLatin1String("T11:12:33"))))
                continue;
            if (e->text.startsWith(QLatin1String("@slrp_")) && e->epochMs >= at180("11:12:38")) continue;
            v << e;
        }
        cut.appendEntries(v);
        const TagCheck::Report c = TagCheck::build(&cut);
        CHECK(c.skipped.size() == 1 && c.skipped.first().from == QLatin1String("16") && c.skipped.first().to == QLatin1String("906")
                  && c.skipped.first().passed == QStringList{ QStringLiteral("18") } && c.skipped.first().ms == at180("11:12:49"),
              "16 then 906 on a route holding 16 18 906: 18 passed without a read, at 11:12:49");
    }

    const RunReport::Summary s = RunReport::summarise(model, QStringLiteral("81_1"), QString());
    const QString html = RunReport::toHtml(s);
    CHECK(html.contains(QStringLiteral("RFID tags: 15 read")) && html.contains(QStringLiteral("Duplicate tag read for 3 of them; the NMS reported a duplicate missing for 1")),
          "the run summary carries the tag check");

    QWidget host;
    auto *lay = new QVBoxLayout(&host);
    auto *band = new LaneBand;
    lay->addWidget(band);
    host.resize(1200, 300);
    host.show();
    band->setModel(model, QStringLiteral("81_1"), QStringLiteral("81_1"));
    QCoreApplication::processEvents();
    const QRect lr = band->laneRect(2);
    const double t = double(at180("11:12:33") - band->fromMs()) / double(qMax<qint64>(1, band->toMs() - band->fromMs()));
    const QString tip = band->describeAt(QPoint(lr.left() + int(t * lr.width()), lr.top() + 2));
    CHECK(tip.contains(QStringLiteral("duplicate of tag 18 missing")), QByteArray("the RFID lane marks it: ") + tip.toUtf8());
}
