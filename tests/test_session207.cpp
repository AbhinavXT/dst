#include "testutil.h"
#include "layoutaudit.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "theme.h"
#include "trackdiagram.h"
#include "trackdiagramwindow.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>
#include <QSlider>

// =============================================================================
//  Session 207 — the SLRP look-ahead profile on the Track diagram.
//
//  The profile in force at the cursor (carried the way the loco holds it),
//  placed from LAST_REF_RFID's abs_loc + travel * DIST_PKT_START: the
//  schema decoder's geometry. Real captures: replay/loco_1_1_27062026_170217.cap
//  (295 @slrp, profiles issued against tags read in the same run) and
//  replay/loco_1_1_29062026_134128.cap (29 @slrp, no @rfid read: not placed).
// =============================================================================

namespace {

void loadCapture(LogModel *m, const QString &name)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/") + name);
    if (!f.open(QIODevice::ReadOnly)) return;
    QVector<LogEntryPtr> v;
    while (!f.atEnd()) {
        const QString l = QString::fromUtf8(f.readLine()).trimmed();
        if (!l.startsWith(QLatin1Char('@'))) continue;
        auto e = QSharedPointer<LogEntry>::create();
        e->text = l;
        e->epochMs = QDateTime::fromString(l.section(QLatin1Char(' '), 1, 1), Qt::ISODate).toMSecsSinceEpoch();
        e->cacheDerived();
        v << e;
    }
    m->appendEntries(v);
}

bool hasHit(const TrackDiagramCanvas &c, const QString &prefix)
{
    for (const TrackDiagramCanvas::Hit &h : c.hits()) if (h.text.startsWith(prefix)) return true;
    return false;
}

}  // namespace

TEST_SUITE(session207)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    LogModel run(nullptr, 200000);
    loadCapture(&run, QStringLiteral("loco_1_1_27062026_170217.cap"));
    const TrackDiagram::Diagram d = TrackDiagram::build(&run, QStringLiteral("1_1"), QStringLiteral("L1"));

    int slrp = 0;
    for (int i = 0; i < run.count(); ++i)
        if (CaptureDecoder::parseLine(run.entryAt(i)->text).type == CapType::SLRP) ++slrp;
    CHECK(slrp > 0, "slrp > 0");
    CHECK(d.profiles.size() == slrp, "one profile state per @slrp frame");

    // A placed profile that carries lanes: its start signal follows the
    // schema decoder's rule, recomputed here from the frame that issued it.
    const TrackDiagram::Profile *withLanes = nullptr;
    for (const TrackDiagram::Profile &p : d.profiles) if (p.placed && p.hasLanes()) { withLanes = &p; break; }
    CHECK(withLanes != nullptr, "withLanes != nullptr");
    if (withLanes) {
        double refLoc = -1;
        for (const TrackDiagram::RfidMark &t : d.tags) if (t.uniqueId == withLanes->refRfid) refLoc = t.locM;
        CHECK(refLoc > 0, "refLoc > 0");
        SlrpProfile issued;
        for (int i = withLanes->row; i >= 0; --i) {
            const CaptureLine c = CaptureDecoder::parseLine(run.entryAt(i)->text);
            if (c.type != CapType::SLRP) continue;
            const SlrpProfile q = CaptureDecoder::profileOf(c);
            if (q.carriesLanes()) { issued = q; break; }
        }
        CHECK(issued.valid, "issued.valid");
        const double travel = issued.pktDir == 2 ? -1.0 : 1.0;
        CHECK((qRound(withLanes->startSignalM)) == (qRound(refLoc + travel * issued.distPktStart)), "qRound(withLanes->startSignalM) == qRound(refLoc + travel * issued.distPktStart)");
        // SSP segments chain from the start signal, one after another.
        if (!withLanes->ssp.isEmpty()) {
            CHECK((qRound(withLanes->ssp.first().fromM)) == (qRound(withLanes->startSignalM)), "qRound(withLanes->ssp.first().fromM) == qRound(withLanes->startSignalM)");
            for (int i = 1; i < withLanes->ssp.size(); ++i)
                CHECK((qRound(withLanes->ssp.at(i).fromM)) == (qRound(withLanes->ssp.at(i - 1).toM)), "qRound(withLanes->ssp.at(i).fromM) == qRound(withLanes->ssp.at(i - 1).toM)");
            CHECK((withLanes->ssp.size()) == (issued.ssp.size()), "withLanes->ssp.size() == issued.ssp.size()");
        }
        CHECK((withLanes->tagLinks.size()) == (issued.tags.size()), "withLanes->tagLinks.size() == issued.tags.size()");
    }

    // Independent check of the placement: every linked tag that was also
    // read lands on the read tag's own abs_loc (two different sources).
    {
        QHash<qint64, double> read;
        for (const TrackDiagram::RfidMark &t : d.tags) read.insert(t.uniqueId, t.locM);
        int compared = 0, off = 0;
        for (const TrackDiagram::Profile &p : d.profiles) {
            if (!p.placed) continue;
            for (const TrackDiagram::ProfileMark &m : p.tagLinks) {
                const qint64 id = m.label.toLongLong();
                if (!read.contains(id)) continue;
                ++compared;
                if (std::abs(read.value(id) - m.locM) > 5.0) ++off;
            }
        }
        CHECK(compared > 0, "fixture: linked tags that were also read");
        CHECK(off == 0, QByteArray("linked tags land on their read location (") + QByteArray::number(off)
                        + " of " + QByteArray::number(compared) + " more than 5 m off)");
    }

    // An MA-only frame after the issue keeps the lanes (carryProfile), with
    // the MA end from the frame itself.
    {
        bool carried = false;
        for (int i = 0; i + 1 < d.profiles.size(); ++i) {
            const TrackDiagram::Profile &a = d.profiles.at(i), &b = d.profiles.at(i + 1);
            const SlrpProfile fb = CaptureDecoder::profileOf(CaptureDecoder::parseLine(run.entryAt(b.row)->text));
            if (a.hasLanes() && !fb.carriesLanes() && b.hasLanes() && b.refProfId == a.refProfId) { carried = true; break; }
        }
        CHECK(carried, "carried");
    }

    // profileAt: the last at or before a moment; nothing before the first.
    CHECK(d.profileAt(d.profiles.first().epochMs - 1) == nullptr, "d.profileAt(d.profiles.first().epochMs - 1) == nullptr");
    CHECK(d.profileAt(d.profiles.first().epochMs) != nullptr, "d.profileAt(d.profiles.first().epochMs) != nullptr");
    CHECK(d.profileAt(d.profiles.last().epochMs) == &d.profiles.last(), "d.profileAt(d.profiles.last().epochMs) == &d.profiles.last()");

    // ---- the canvas -------------------------------------------------------------
    TrackDiagramWindow w(&run, QStringLiteral("1_1"), QStringLiteral("L1"));
    w.resize(1100, 700);
    w.show();
    // Cursor on the sample nearest after the profile with lanes.
    int idx = -1;
    if (withLanes)
        for (int i = 0; i < d.trace.samples.size(); ++i)
            if (d.trace.samples.at(i).epochMs >= withLanes->epochMs) { idx = i; break; }
    CHECK(idx >= 0, "idx >= 0");
    w.findChild<QSlider *>()->setValue(idx);
    w.canvas()->grab();
    CHECK(hasHit(*w.canvas(), QStringLiteral("Profile start signal at")), "hasHit(*w.canvas(), QStringLiteral(\"Profile start signal at\"))");
    if (withLanes && !withLanes->ssp.isEmpty()) CHECK(hasHit(*w.canvas(), QStringLiteral("Static speed")), "hasHit(*w.canvas(), QStringLiteral(\"Static speed\"))");
    CHECK(w.canvas()->minimumSizeHint().height() > 330, "w.canvas()->minimumSizeHint().height() > 330");
    CHECK(w.canvas()->minimumSizeHint().height() <= 700, "w.canvas()->minimumSizeHint().height() <= 700");
    CHECK(LayoutAudit::orphans(&w).isEmpty(), "LayoutAudit::orphans(&w).isEmpty()");

    // ---- a tab whose reference tag was never read: counted, not placed -----------
    LogModel noTags(nullptr, 200000);
    loadCapture(&noTags, QStringLiteral("loco_1_1_29062026_134128.cap"));
    const TrackDiagram::Diagram n = TrackDiagram::build(&noTags, QStringLiteral("1_1"), QStringLiteral("L1"));
    CHECK(!n.profiles.isEmpty(), "!n.profiles.isEmpty()");
    bool anyPlaced = false;
    for (const TrackDiagram::Profile &p : n.profiles) anyPlaced = anyPlaced || p.placed || p.hasLanes();
    CHECK(!anyPlaced, "!anyPlaced");

    // ---- no @slrp at all: no lanes, the canvas keeps its old minimum ------------
    LogModel none(nullptr, 200000);
    loadCapture(&none, QStringLiteral("loco_2_1_29062026_134509.cap"));
    TrackDiagramCanvas plain;
    plain.setDiagram(TrackDiagram::build(&none, QStringLiteral("2_1"), QStringLiteral("L2")));
    CHECK(plain.diagram().profiles.isEmpty(), "plain.diagram().profiles.isEmpty()");
    CHECK((plain.minimumSizeHint().height()) == (330), "plain.minimumSizeHint().height() == 330");
}
