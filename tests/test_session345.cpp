#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "radiochart.h"
#include "radiochartdialog.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QTemporaryDir>

// =============================================================================
//  Session 345 — the radio chart: SLRP, LSRP and ARP over a stretch of a log
//  as a sequence diagram, PNG or SVG.
//  Real frames: replay/2026-10-08/loco_1_1_radiolinks.log (session 214's
//  oracle: 115 SLRP from 527 with 8 steps of 4+ s and 2 outages, one 5 s
//  old; 126 LSRP and 131 ARP, 2 outages each), and
//  replay/loco_1_1_27062026_140226.cap, whose SLRPs carry AUTHORITY_TYPE 0
//  (clause 31.26.1, session 338).
// =============================================================================

namespace {

qint64 iso345(const QString &s) { return QDateTime::fromString(s, Qt::ISODateWithMs).toMSecsSinceEpoch(); }

LogModel *loadLinks345(MessageDispatcher &d)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_radiolinks.log"));
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const int at = l.indexOf('@');
        if (l.startsWith('#') || at < 0) continue;
        d.ingestLocal(81, 1, l.mid(at), iso345(QString::fromLatin1(l.left(24))), QString());
    }
    d.drainNow();
    return d.modelForKey(QStringLiteral("81_1"));
}

// The 27 June capture, 14:02-14:05, without the SLRPs whose receipt time is in `skip`.
LogModel *loadCap345(MessageDispatcher &d, const QStringList &skip = {})
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        const QString t = QString::fromLatin1(tok.at(1));
        if (t >= QLatin1String("2026-06-27T14:05:00")) break;
        if (l.startsWith("@slrp_") && skip.contains(t)) continue;
        d.ingestLocal(21, 1, l, iso345(t), QString());
    }
    d.drainNow();
    return d.modelForKey(QStringLiteral("21_1"));
}

int count345(const RadioChart::Chart &c, const QString &type)
{
    int n = 0;
    for (const RadioChart::Message &m : c.messages) n += m.type == type ? 1 : 0;
    return n;
}

}  // namespace

TEST_SUITE(session345)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- the radio links log: counts, silences, the stale SLRP -----------------------------------
    {
        MessageDispatcher disp;
        LogModel *model = loadLinks345(disp);
        CHECK(model != nullptr, "fixture: the radio links tab");
        if (!model) return;
        const qint64 from = iso345(QStringLiteral("2026-10-08T04:55:00.000Z"));
        const RadioChart::Chart c = RadioChart::build(model, QStringLiteral("81_1"), from, from + 8 * 60 * 1000);
        CHECK(c.error.isEmpty(), "built: " + c.error.toUtf8());
        CHECK(count345(c, QStringLiteral("slrp")) == 115 && count345(c, QStringLiteral("lsrp")) == 126
                  && count345(c, QStringLiteral("arp")) == 131,
              QByteArray("115 SLRP, 126 LSRP, 131 ARP (") + c.summary().toUtf8() + ")");
        bool arrivalOrder = true;
        for (int i = 1; i < c.messages.size(); ++i) arrivalOrder &= c.messages.at(i).row > c.messages.at(i - 1).row;
        CHECK(arrivalOrder, "messages in the order logged");
        int slrpGaps = 0, slrpOut = 0, lsrpOut = 0, arpOut = 0, others = 0;
        for (const RadioChart::Silence &s : c.silences) {
            if (s.link == QLatin1String("SLRP from station 527")) (s.outage ? slrpOut : slrpGaps) += 1;
            else if (s.link == QLatin1String("LSRP") && s.outage) ++lsrpOut;
            else if (s.link == QLatin1String("ARP") && s.outage) ++arpOut;
            else ++others;
        }
        CHECK(slrpGaps == 8 && slrpOut == 2 && lsrpOut == 2 && arpOut == 2 && others == 0,
              QByteArray("silences as session 214 counts them: SLRP 8 gaps + 2 outages, LSRP 2, ARP 2 (")
                  + QByteArray::number(slrpGaps) + " " + QByteArray::number(slrpOut) + " " + QByteArray::number(lsrpOut)
                  + " " + QByteArray::number(arpOut) + " " + QByteArray::number(others) + ")");
        bool firstOutage = false;
        for (const RadioChart::Silence &s : c.silences)
            firstOutage |= s.link.startsWith(QLatin1String("SLRP")) && s.outage && s.seconds == 111
                           && s.fromMs == iso345(QStringLiteral("2026-10-08T04:56:30.199Z"))
                           && c.messages.at(s.afterIndex).ms == s.fromMs;
        CHECK(firstOutage, "the SLRP outage after 04:56:30.199, FRAME_NUM +111, placed where it fell");
        int ages = 0, stale = 0;
        for (const RadioChart::Message &m : c.messages) {
            ages += m.hasAge ? 1 : 0;
            for (const QString &d : m.drops) stale += d.contains(QLatin1String("s old (over 4 s)")) ? 1 : 0;
        }
        CHECK(ages == 114 && stale == 1, QByteArray("114 SLRP aged, one over 4 s old (") + QByteArray::number(ages)
                                             + " " + QByteArray::number(stale) + ")");

        // Drawn: one row per message and silence; PNG and SVG agree on the steps.
        const RadioChart::Drawing d = RadioChart::draw(c);
        CHECK(d.size.height() > (c.messages.size() + c.silences.size()) * 24 && d.size.height() < 32767,
              "one 24 px row per frame and silence, under the PNG height limit");
        QTemporaryDir tmp;
        QString err;
        CHECK(RadioChart::save(c, tmp.filePath(QStringLiteral("r.png")), &err), "PNG written: " + err.toUtf8());
        const QImage back(tmp.filePath(QStringLiteral("r.png")));
        CHECK(back.size() == d.size, "the PNG reads back at the drawing's size");
        CHECK(RadioChart::save(c, tmp.filePath(QStringLiteral("r.svg")), &err), "SVG written: " + err.toUtf8());
        QFile svg(tmp.filePath(QStringLiteral("r.svg")));
        svg.open(QIODevice::ReadOnly);
        const QByteArray s = svg.readAll();
        int lines = 0, texts = 0;
        for (const RadioChart::Step &st : d.steps) (st.kind == RadioChart::Step::Line ? lines : texts) += st.kind == RadioChart::Step::Rect ? 0 : 1;
        CHECK(s.startsWith("<?xml") && s.count("<line ") == lines && s.count("<text ") == texts,
              "the SVG has every line and text of the drawing");
        CHECK(s.contains("no SLRP from station 527 for 111 s: no contact") && s.contains("✕ 5 s old (over 4 s)"),
              "the SVG says the outage and the stale frame");
        if (!qEnvironmentVariableIsEmpty("RADIO_CHART_OUT")) {
            RadioChart::save(c, qEnvironmentVariable("RADIO_CHART_OUT") + QStringLiteral("/radio_links.png"));
            RadioChart::save(c, qEnvironmentVariable("RADIO_CHART_OUT") + QStringLiteral("/radio_links.svg"));
        }
    }

    // ---- 27 June: AUTHORITY_TYPE 0 frames; a missing SLRP is a silence ---------------------------
    {
        MessageDispatcher disp;
        LogModel *model = loadCap345(disp);
        CHECK(model != nullptr, "fixture: the 27 June tab");
        if (!model) return;
        const qint64 from = iso345(QStringLiteral("2026-06-27T14:03:00")), to = iso345(QStringLiteral("2026-06-27T14:04:00"));
        const RadioChart::Chart c = RadioChart::build(model, QStringLiteral("21_1"), from, to);
        int auth0 = 0;
        for (const RadioChart::Message &m : c.messages)
            auth0 += m.drops.join(QLatin1Char(' ')).contains(QLatin1String("31.26.1 AUTHORITY_TYPE = 0")) ? 1 : 0;
        CHECK(c.error.isEmpty() && count345(c, QStringLiteral("slrp")) > 10 && auth0 > 0,
              QByteArray("SLRPs with AUTHORITY_TYPE 0 marked 31.26.1 (") + QByteArray::number(auth0) + " of "
                  + c.summary().toUtf8() + ")");
        CHECK(c.summary().contains(QLatin1String("would not be processed")), "the summary says so: " + c.summary().toUtf8());
        CHECK(c.silences.isEmpty(), "no silence in that minute");

        // One SLRP taken out: its link has a silence there, one frame expected.
        QStringList slrpTimes;
        for (const RadioChart::Message &m : c.messages)
            if (m.type == QLatin1String("slrp")) slrpTimes << QDateTime::fromMSecsSinceEpoch(m.ms).toString(Qt::ISODate);
        MessageDispatcher disp2;
        LogModel *cut = loadCap345(disp2, { slrpTimes.value(5) });
        const RadioChart::Chart c2 = RadioChart::build(cut, QStringLiteral("21_1"), from, to);
        CHECK(c2.silences.size() == 1 && c2.silences.first().missing == 1 && !c2.silences.first().outage
                  && c2.silences.first().link.startsWith(QLatin1String("SLRP from station")),
              QByteArray("one SLRP cut: one silence, one frame expected (") + QByteArray::number(c2.silences.size()) + ")");
        CHECK(count345(c2, QStringLiteral("slrp")) == count345(c, QStringLiteral("slrp")) - 1, "and one SLRP fewer");

        // Refusals.
        CHECK(!RadioChart::build(model, QStringLiteral("21_1"), from, from + 11 * 60 * 1000).error.isEmpty(),
              "over 10 minutes refused");
        CHECK(RadioChart::build(model, QStringLiteral("21_1"), to + 3600 * 1000, to + 3660 * 1000).error
                  == QLatin1String("No SLRP, LSRP or ARP in that window."),
              "an empty window says so");

        // The dialog: ±30 s, the summary, the picture, Export writes.
        RadioChartDialog dlg(model, QStringLiteral("21_1"), iso345(QStringLiteral("2026-06-27T14:03:30")));
        CHECK(dlg.chart().fromMs == from && dlg.chart().toMs == to, "the dialog opens on the moment ±30 s");
        CHECK(dlg.summaryLabel()->text() == c.summary(), "it says what the chart holds");
        CHECK(!dlg.picture()->pixmap(Qt::ReturnByValue).isNull(), "and shows it");
        QTemporaryDir tmp;
        CHECK(dlg.exportTo(tmp.filePath(QStringLiteral("x.svg"))) && QFile::exists(tmp.filePath(QStringLiteral("x.svg"))),
              "Export writes the SVG");
        CHECK(dlg.status()->message().contains(QLatin1String("would not process")), "and says what it wrote");
        dlg.setRange(from, from + 20 * 60 * 1000);
        CHECK(!dlg.exportTo(tmp.filePath(QStringLiteral("y.png"))) && !QFile::exists(tmp.filePath(QStringLiteral("y.png"))),
              "a window too long is not exported");
    }
}
