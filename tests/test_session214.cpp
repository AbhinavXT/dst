#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "radiohealthwindow.h"
#include "radiolinks.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QTableWidget>

// =============================================================================
//  Session 214 — radio frame loss, per link.
//  Real frames: replay/2026-10-08/loco_1_1_radiolinks.log, the @slrp, @lsrp
//  and @arp lines of 81_1.001.log, 04:55-05:03 UTC, as DLConsole logged them.
//  Expected, from schema/engine.py with the same rules (scratch oracle):
//    SLRP from station 527: 115 frames, cycle 2 s, 122 expected in contact,
//      7 lost, 8 steps of 4+; outages after 04:56:30.199 (FRAME_NUM +111,
//      110.0 s) and 04:59:00.195 (+129, 130.0 s), the longest gap 130 019 ms;
//      age on arrival (114 with an own frame in the 10 s before): median 2 s,
//      up to 5 s, 1 over 4 s old.
//    LSRP sent: 126, none lost, 2 outages (+108, +124), with the SLRP's.
//    ARP sent: 131, none lost, 2 outages (+37, +77).
// =============================================================================

namespace {

qint64 at214(const char *iso) { return QDateTime::fromString(QLatin1String(iso), Qt::ISODateWithMs).toMSecsSinceEpoch(); }

LogModel *loadLinks(MessageDispatcher &d)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_radiolinks.log"));
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const int at = l.indexOf('@');
        if (l.startsWith('#') || at < 0) continue;
        d.ingestLocal(81, 1, l.mid(at), at214(l.left(24).constData()), QString());
    }
    d.drainNow();
    return d.modelForKey(QStringLiteral("81_1"));
}

}  // namespace

TEST_SUITE(session214)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    MessageDispatcher disp;
    LogModel *model = loadLinks(disp);
    CHECK(model != nullptr, "fixture: the tab");
    if (!model) return;

    const RadioLinks::Report r = RadioLinks::build(model);
    CHECK(r.links.size() == 3, QByteArray("SLRP from 527, LSRP, ARP (") + QByteArray::number(r.links.size()) + ")");
    if (r.links.size() != 3) return;
    const RadioLinks::Link &s = r.links.at(0);
    CHECK(s.type == QLatin1String("slrp") && s.station == 527 && s.name == QStringLiteral("SLRP from station 527"), "SLRP keyed by station");
    CHECK(s.frames == 115 && s.repeats == 0 && s.cycleS == 2, "115 frames on a 2 s cycle");
    CHECK(s.expected == 122 && s.lost == 7 && s.gaps == 8,
          QByteArray("122 expected in contact, 7 lost, 8 steps of 4+ (") + QByteArray::number(s.expected) + " "
              + QByteArray::number(s.lost) + " " + QByteArray::number(s.gaps) + ")");
    CHECK(s.outages.size() == 2 && s.outages.at(0).fromMs == at214("2026-10-08T04:56:30.199Z") && s.outages.at(0).seconds == 111
              && s.outages.at(0).toMs - s.outages.at(0).fromMs == 109997 && s.outages.at(1).fromMs == at214("2026-10-08T04:59:00.195Z")
              && s.outages.at(1).seconds == 129 && s.longestGapMs == 130019,
          "two outages, 111 s and 129 s by FRAME_NUM; the longest gap 130.019 s");
    CHECK(s.ages.size() == 114 && s.ageMedian == 2 && s.ageMax == 5 && s.stale == 1 && s.ahead == 0,
          QByteArray("age on arrival: 114, median 2 s, up to 5, one stale (") + QByteArray::number(s.ages.size()) + " "
              + QByteArray::number(s.ageMedian) + " " + QByteArray::number(s.ageMax) + " " + QByteArray::number(s.stale) + ")");
    const RadioLinks::Link &l = r.links.at(1), &a = r.links.at(2);
    CHECK(l.type == QLatin1String("lsrp") && l.frames == 126 && l.lost == 0 && l.outages.size() == 2
              && l.outages.at(0).seconds == 108 && l.outages.at(1).seconds == 124,
          "LSRP sent: 126, none lost, silent across the same two outages");
    CHECK(a.type == QLatin1String("arp") && a.frames == 131 && a.lost == 0 && a.outages.size() == 2
              && a.outages.at(0).seconds == 37 && a.outages.at(1).seconds == 77,
          "ARP sent: 131, none lost, two outages of its own");
    const QString text = RadioLinks::summaryText(r);
    CHECK(text.contains(QStringLiteral("115 received, 7 lost in contact (5.7 %), 2 outage(s)"))
              && text.contains(QStringLiteral("2 s (median), up to 5 s; 1 the loco would not process")),
          QByteArray("the summary: ") + text.toUtf8());

    // The counting rules on frames set by hand.
    {
        RadioLinks::Link h;
        // 1/3 jitter is no loss; a 4 is one lost; a 6 is two; a 40 is an outage;
        // a repeat is counted as such; across midnight (86399 -> 1) is a step of 2.
        const QVector<QPair<qint64, qint64>> f{ { 0, 100 }, { 2000, 101 }, { 4000, 104 }, { 6000, 106 }, { 8000, 110 },
                                                { 10000, 116 }, { 12000, 116 }, { 52000, 156 }, { 54000, 158 },
                                                { 60000, 86399 }, { 62000, 1 } };
        RadioLinks::count(h, f);
        CHECK(h.cycleS == 2 && h.repeats == 1 && h.outages.size() == 2 && h.lost == 3,
              QByteArray("jitter, losses, a repeat, an outage, midnight (cycle ") + QByteArray::number(h.cycleS) + ", lost "
                  + QByteArray::number(h.lost) + ", outages " + QByteArray::number(h.outages.size()) + ")");
    }

    // In the Radio and GPS health window, under the strips.
    RadioHealthWindow w(model, QStringLiteral("81_1"), QStringLiteral("81_1"));
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1000, 700);
    w.show();
    QTableWidget *t = w.linksTable();
    CHECK(t && t->rowCount() == 3 && t->item(0, 0)->text() == QStringLiteral("SLRP from station 527")
              && t->item(0, 4)->text() == QStringLiteral("7") && t->item(2, 0)->text() == QStringLiteral("ARP (sent)"),
          "the links table: one row per link");
    CHECK(w.linksSummary() == text, "the links summary over it");
    if (!qgetenv("DL_SHOTS").isEmpty()) w.grab().save(QString::fromLocal8Bit(qgetenv("DL_SHOTS")) + QStringLiteral("/radio_links.png"));
    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700, "still fits a laptop");
}
