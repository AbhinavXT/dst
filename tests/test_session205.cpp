#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "planrun.h"
#include "rfidtag.h"
#include "simpreview.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QSet>
#include <QTableWidget>

#include <cmath>

// =============================================================================
//  Session 205 — the Simulator tab's preview against a loco log: each send
//  matched to a read on its reader, with the lag. First on reads made from a
//  preview and then changed one way at a time; then on the REAL @rfid frames
//  of replay/loco_1_1_27062026_140226.cap (reader bytes 01 and 02), against a
//  route made from that run. That capture was not made with the simulator,
//  so its lags are not checked, only that sends are matched.
// =============================================================================

namespace {

QByteArray normal(int unique, qint64 loc, bool dup = false)
{
    return RfidTag::build({ { QStringLiteral("type"), 9 },       { QStringLiteral("version"), 1 },
                            { QStringLiteral("unique"), unique }, { QStringLiteral("abs_loc"), loc },
                            { QStringLiteral("tin_nom"), 84 },   { QStringLiteral("tin_rev"), 84 },
                            { QStringLiteral("duplication"), dup ? 1 : 0 } });
}

RfidTag::Route pairs()
{
    RfidTag::Route r;
    r.dir = RfidTag::DirNominal;
    for (int i = 0; i < 3; ++i) {
        r.tags.append({ QString(), normal(100 + i, 1000 + 300 * i) });
        r.tags.append({ QString(), normal(100 + i, 1004 + 300 * i, true) });
    }
    return r;
}

// What a loco would log for a preview: every send, `offsetMs` after it.
QVector<PlanRun::Read> readsFor(const RfidTag::Route &route, const SimPreview::Result &p, qint64 offsetMs)
{
    QVector<PlanRun::Read> out;
    const qint64 base = 1780000000000;
    for (const SimPreview::Event &e : p.events) {
        const QByteArray tag = route.tags.at(e.row).bytes;
        const RfidTag::Summary s = RfidTag::summary(tag);
        out.append({ base + qint64(e.timeS * 1000) + offsetMs, tag, s.unique, s.duplicate, e.reader });
    }
    return out;
}

LogModel *load(MessageDispatcher &d)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        d.ingestLocal(1, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    d.drainNow();
    return d.modelForKey(QStringLiteral("1_1"));
}

}  // namespace

TEST_SUITE(session205)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- 1. reads made from the preview -------------------------------------------------------
    const RfidTag::Route route = pairs();
    SimPreview::Options o;
    const SimPreview::Result p = SimPreview::run(route, o);
    {
        const QVector<PlanRun::Read> reads = readsFor(route, p, 250);
        const SimPreview::Against a = SimPreview::against(p, reads);
        CHECK(a.matched == p.events.size() && a.maxLagS < 0.001 && a.unpredicted.isEmpty(),
              "every send read, as predicted: lined up on the first, no lag, nothing unpredicted");

        QVector<PlanRun::Read> late = reads;
        late[3].ms += 1500;
        late[4].ms += 1500;               // still in order
        const SimPreview::Against la = SimPreview::against(p, late);
        CHECK(std::fabs(la.lagS.at(3) - 1.5) < 0.001 && std::fabs(la.maxLagS - 1.5) < 0.001 && std::fabs(la.lagS.at(2)) < 0.001,
              QByteArray("a read 1.5 s late: its lag: ") + QByteArray::number(la.lagS.at(3)));

        QVector<PlanRun::Read> lost = reads;
        lost.remove(2);                   // reader 2's 100
        const SimPreview::Against lo = SimPreview::against(p, lost);
        CHECK(lo.readOf.at(2) == -1 && lo.matched == p.events.size() - 1, "a send the log does not have: not read");

        QVector<PlanRun::Read> wrongReader = reads;
        wrongReader[2].reader = 1;
        const SimPreview::Against wr = SimPreview::against(p, wrongReader);
        CHECK(wr.readOf.at(2) == -1 && wr.unpredicted == QVector<int>({ 2 }),
              "the same tag on the other reader: not this send, and not predicted");

        QVector<PlanRun::Read> extra = reads;
        extra.insert(4, extra.at(1));     // 100D read again on reader 1
        extra[4].ms = extra.at(3).ms;
        const SimPreview::Against ex = SimPreview::against(p, extra);
        CHECK(ex.matched == p.events.size() && ex.unpredicted == QVector<int>({ 4 }), "an extra read inside the run: not predicted");

        CHECK(SimPreview::against(p, {}).matched == 0 && SimPreview::against(p, {}).unpredicted.isEmpty(), "no reads: nothing matched");
    }

    // ---- 2. a real log ------------------------------------------------------------------------
    MessageDispatcher d;
    LogModel *m = load(d);
    CHECK(m != nullptr, "the real capture loads");
    if (!m) return;
    const QVector<PlanRun::Read> reads = PlanRun::readsOf(m);
    QSet<int> readers;
    for (const PlanRun::Read &r : reads) readers.insert(r.reader);
    CHECK(readers == QSet<int>({ 1, 2 }), "its reads carry the reader byte: 1 and 2");
    const RfidTag::Route run = PlanRun::routeFromRun(reads);
    const SimPreview::Result rp = SimPreview::run(run, o);
    const SimPreview::Against ra = SimPreview::against(rp, reads);
    CHECK(rp.ok && ra.matched > rp.events.size() / 2,
          QByteArray("a route made from the run, previewed and compared: ") + QByteArray::number(ra.matched) + " of "
              + QByteArray::number(rp.events.size()) + " sends read, largest lag " + QByteArray::number(ra.maxLagS, 'f', 1) + " s");

    // ---- 3. the window ------------------------------------------------------------------------
    {
        TagBuilderWindow w(&d);
        w.resize(1100, 680);
        w.show();
        CHECK(!w.previewAgainstRun() && w.status()->kind() == StatusLine::Kind::Warn, "no log picked: it says so");
        w.setRunSource(QStringLiteral("1_1"));
        w.setRoute(run);
        SimPreview::Against a;
        CHECK(w.previewAgainstRun(&a) && a.matched == ra.matched, "Against the run: the same matches");
        int filled = 0;
        for (int i = 0; i < w.simTable()->rowCount(); ++i)
            filled += w.simTable()->item(i, 6) && !w.simTable()->item(i, 6)->text().isEmpty() ? 1 : 0;
        CHECK(w.simTable()->columnCount() == 8 && filled == w.simTable()->rowCount()
                  && w.simSummary()->text().contains(QStringLiteral("sends read on their reader")),
              QByteArray("each send's read and lag, and a summary: ") + w.simSummary()->text().toUtf8());
        w.previewSimulator(w.simulatorOptions());
        CHECK(w.simTable()->item(0, 6) == nullptr || w.simTable()->item(0, 6)->text().isEmpty(),
              "a plain preview after it: no stale reads");
        w.setRoute(RfidTag::Route());
    }
}
