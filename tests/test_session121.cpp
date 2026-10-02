#include "testutil.h"

#include "laneband.h"
#include "logmodel.h"
#include "runreport.h"

#include <QFile>
#include <QTest>
#include <QSignalSpy>
#include <QVBoxLayout>

// =============================================================================
//  Session 121 — UI revamp step 5: lanes over the log.
//
//  The lanes draw RunReport::summarise() over a bounded window — the same
//  engine as the run summary and the incident pack — so they must say what
//  it says. Real traffic: replay/loco_1_1_27062026_140226.cap.
// =============================================================================

namespace {
QVector<LogEntryPtr> realEntries(int max)
{
    QVector<LogEntryPtr> out;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    qint64 ms = 1782558147000LL;
    while (!f.atEnd() && out.size() < max) {
        const QString l = QString::fromUtf8(f.readLine()).trimmed();
        if (!l.startsWith(QLatin1Char('@'))) continue;
        auto e = QSharedPointer<LogEntry>::create();
        e->text = l;
        e->epochMs = (ms += 250);
        e->header.source_id = 21;
        e->header.kvchId = 1;
        e->cacheDerived();
        out << e;
    }
    return out;
}
}  // namespace

TEST_SUITE(session121)
{
    QWidget host;                               // a shown parent: the band only works for a visible tab
    auto *lay = new QVBoxLayout(&host);
    auto *band = new LaneBand;
    lay->addWidget(band);
    host.resize(900, 200);
    host.show();

    LogModel model;
    const QVector<LogEntryPtr> rows = realEntries(2000);
    CHECK(rows.size() > 500, "fixture: real capture lines");
    model.appendEntries(rows);
    band->setModel(&model, QStringLiteral("21_1"), QStringLiteral("L1_V1"));
    band->rebuildNow();

    RunReport::Options o;
    o.hasWindow = true;
    o.fromMs = band->fromMs();
    o.toMs = band->toMs();
    const RunReport::Summary direct = RunReport::summarise(&model, QStringLiteral("21_1"), QStringLiteral("L1_V1"), o);
    CHECK(band->summary().firstMode == direct.firstMode && band->summary().modeChanges.size() == direct.modeChanges.size()
              && band->summary().tagReads.size() == direct.tagReads.size(),
          QByteArray("the lanes say what RunReport says (mode ") + direct.firstMode.toUtf8() + ", "
              + QByteArray::number(direct.modeChanges.size()) + " changes, "
              + QByteArray::number(direct.tagReads.size()) + " tag reads)");
    CHECK(band->hasContent() && band->isVisible(), "a loco's traffic gives a band, shown");
    CHECK(band->laneNames() == QStringList({ "Mode", "Safety", "RFID", "Link", "Faults" }), "five lanes");

    // Hover over the mode lane says the mode; a click jumps the log there.
    const QString said = band->describeAt(QPoint(band->width() / 2, 6 + 16 / 2));
    CHECK(said.startsWith(QLatin1String("Mode ")), QByteArray("hovering the mode lane names the mode: ") + said.toUtf8());
    QSignalSpy jump(band, &LaneBand::timeClicked);
    QTest::mouseClick(band, Qt::LeftButton, Qt::NoModifier, QPoint(band->width() / 2, 10));
    CHECK(jump.size() == 1 && jump.at(0).at(0).toLongLong() >= band->fromMs()
              && jump.at(0).at(0).toLongLong() <= band->toMs(),
          "a click asks the log to jump to that moment, inside the window");

    // The window is bounded: newest 15 minutes or 20,000 rows.
    CHECK(band->toMs() == model.newestMs() && band->toMs() - band->fromMs() <= LaneBand::kWindowMs,
          "the window ends at the newest row and spans at most 15 minutes");

    // Text-only traffic: nothing to draw, no band.
    LogModel text;
    auto t = QSharedPointer<LogEntry>::create();
    t->text = QStringLiteral("RAD IN Link 1 Error");
    t->epochMs = 1000;
    text.appendEntry(t);
    band->setModel(&text, QStringLiteral("33_1"), QStringLiteral("33_1"));
    band->rebuildNow();
    CHECK(!band->hasContent() && !band->isVisible(), "text diagnostics give no band");

    // View > Lanes over the log, off.
    band->setModel(&model, QStringLiteral("21_1"), QStringLiteral("L1_V1"));
    band->setEnabled2(false);
    CHECK(!band->isVisible(), "switched off: hidden, and not rebuilt");
    band->setEnabled2(true);
    CHECK(band->isVisible(), "switched on again: back");

    // A hidden tab's band does no work: switching models while hidden
    // leaves what was drawn until it is shown again.
    const qint64 drawnTo = band->toMs();
    host.hide();
    band->setModel(&text, QStringLiteral("33_1"), QStringLiteral("33_1"));
    CHECK(band->toMs() == drawnTo, "hidden: not rebuilt");
    host.show();
    band->rebuildNow();
    CHECK(!band->hasContent(), "shown again: rebuilt for what it now holds");
}
