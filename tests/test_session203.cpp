#include "testutil.h"
#include "layoutaudit.h"

#include "rfidtag.h"
#include "simpreview.h"
#include "statusline.h"
#include "tagbuilderwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>

#include <cmath>

// =============================================================================
//  Session 203 — what the RFID simulator (LocoTcasSimulator) would send for a
//  route. The expectations are the simulator's own rules (LocoDialog::
//  calculateNextDistance / sendingRfidData) worked by hand on built routes,
//  then the preview run on a route of the simulator's own Configuration1.xml
//  (built in since session 202). The simulator itself was not run.
// =============================================================================

namespace {

QByteArray normal(int unique, qint64 loc, bool dup = false)
{
    return RfidTag::build({ { QStringLiteral("type"), 9 },       { QStringLiteral("version"), 1 },
                            { QStringLiteral("unique"), unique }, { QStringLiteral("abs_loc"), loc },
                            { QStringLiteral("tin_nom"), 84 },   { QStringLiteral("tin_rev"), 84 },
                            { QStringLiteral("duplication"), dup ? 1 : 0 } });
}

// 100 / 100D / 101 / 101D / 102 / 102D, 300 m apart, duplicates 4 m on.
RfidTag::Route pairs(int dir)
{
    RfidTag::Route r;
    r.dir = dir;
    for (int i = 0; i < 3; ++i) {
        r.tags.append({ QString(), normal(100 + i, 1000 + 300 * i) });
        r.tags.append({ QString(), normal(100 + i, 1004 + 300 * i, true) });
    }
    if (dir == RfidTag::DirReverse) std::reverse(r.tags.begin(), r.tags.end());
    return r;
}

QString sequence(const SimPreview::Result &r)
{
    QStringList out;
    for (const SimPreview::Event &e : r.events) out << QStringLiteral("%1:%2").arg(e.reader).arg(e.tag);
    return out.join(QLatin1Char(' '));
}

}  // namespace

TEST_SUITE(session203)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- 1. the simulator's arithmetic --------------------------------------------------------
    CHECK(SimPreview::pulsesPerTick(60) == 16746 && std::fabs(SimPreview::metresPerTick(60) - 1.66663) < 0.0001,
          QByteArray("60 km/h: 16746 pulses (x1000) and 1.66663 m a tick, its PI 3.1428571 and 950 mm wheel: ")
              + QByteArray::number(SimPreview::pulsesPerTick(60)) + " / " + QByteArray::number(SimPreview::metresPerTick(60), 'f', 5));
    CHECK(SimPreview::pulsesPerTick(0) == 0, "0 km/h: no pulses");

    // ---- 2. reader 1 and reader 2 -------------------------------------------------------------
    {
        SimPreview::Options o;
        o.speedKmh = 60;
        const SimPreview::Result r = SimPreview::run(pairs(RfidTag::DirNominal), o);
        CHECK(r.ok && !r.fromFileRows, "a built route: locations as route.xml would write them");
        CHECK(sequence(r) == QLatin1String("1:100 1:100D 2:100 2:100D 1:101 1:101D 2:101 2:101D 1:102"),
              QByteArray("reader 1 on entering each row; reader 2 the main 20-24 m past the duplicate, then the duplicate: ")
                  + sequence(r).toUtf8());
        const SimPreview::Event &main2 = r.events.at(2);
        CHECK(main2.distance - 1004 >= 20 && main2.distance - 1004 < 24 && r.events.at(3).distance - 1004 >= 24,
              "reader 2's main at 20-24 m past the row it is in (100D), its duplicate at 24 m or more");
        CHECK(r.neverOnReader1 == QStringList({ QStringLiteral("102D") }) && r.mainsMissedOnReader2 == QStringList({ QStringLiteral("102") }),
              QByteArray("the last row is never sent (its next location is its own), so 102 never reaches reader 2 either: ")
                  + r.neverOnReader1.join(QLatin1Char(',')).toUtf8() + " / " + r.mainsMissedOnReader2.join(QLatin1Char(',')).toUtf8());
        CHECK(r.endReason.contains(QLatin1String("past the route's last row")) && r.endDistance >= 1604,
              "it stops past the last row");

        const SimPreview::Result rev = SimPreview::run(pairs(RfidTag::DirReverse), o);
        CHECK(sequence(rev).startsWith(QLatin1String("1:102D 1:102 ")),
              QByteArray("a reverse route runs down the locations: ") + sequence(rev).toUtf8());

        SimPreview::Options m = o;
        m.missing = QStringList{ QStringLiteral("101") };
        const SimPreview::Result mr = SimPreview::run(pairs(RfidTag::DirNominal), m);
        CHECK(!sequence(mr).contains(QLatin1String(":101 ")) && sequence(mr).contains(QLatin1String("1:101D"))
                  && mr.neverOnReader1.contains(QLatin1String("101")),
              QByteArray("a missing tag is not sent on either reader: ") + sequence(mr).toUtf8());

        SimPreview::Options only1 = o;
        only1.reader2 = false;
        CHECK(!sequence(SimPreview::run(pairs(RfidTag::DirNominal), only1)).contains(QLatin1String("2:")), "reader 2 off");

        SimPreview::Options fast = o;
        fast.speedKmh = 160;
        const SimPreview::Result f = SimPreview::run(pairs(RfidTag::DirNominal), fast);
        CHECK(f.metresPerTick > 4, "160 km/h: a tick is longer than reader 2's 4 m window");

        RfidTag::Route none = pairs(RfidTag::DirNominal);
        none.dir = RfidTag::DirUnset;
        CHECK(!SimPreview::run(none, o).ok, "no direction: no preview");
    }

    // ---- 3. a route of the simulator's own file -----------------------------------------------
    {
        const QVector<RfidTag::Route> routes =
            RfidTag::readFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
        const RfidTag::Route r = routes.value(0);
        SimPreview::Options o;
        o.speedKmh = 80;
        const SimPreview::Result s = SimPreview::run(r, o);
        CHECK(s.ok && s.fromFileRows && !s.events.isEmpty() && s.events.first().reader == 1
                  && s.events.first().tag == RfidTag::nameOf(r.tags.first().bytes),
              QByteArray("the simulator's first route (") + r.name.toUtf8() + "): its own rows, starting with its first tag");
        CHECK(s.neverOnReader1.contains(r.tags.last().file.rfidId),
              QByteArray("and its last tag is never sent: ") + r.tags.last().file.rfidId.toUtf8());
        RfidTag::Route edited = r;
        QHash<QString, qint64> v = RfidTag::values(edited.tags[3].bytes);
        v.insert(QStringLiteral("tin_nom"), v.value(QStringLiteral("tin_nom")) + 1);
        edited.tags[3].bytes = RfidTag::build(v);
        CHECK(!SimPreview::run(edited, o).fromFileRows, "an edited tag: its row no longer the file's, so route.xml's locations are used");
    }

    // ---- 4. the window ------------------------------------------------------------------------
    {
        TagBuilderWindow w;
        w.resize(1100, 680);
        w.show();
        w.setRoute(pairs(RfidTag::DirNominal));
        CHECK(w.tabs()->count() == 5 && w.tabs()->tabText(4) == QLatin1String("Simulator"), "a Simulator tab");
        const SimPreview::Result r = w.previewSimulator(w.simulatorOptions());
        CHECK(w.tabs()->currentIndex() == 4 && w.simTable()->rowCount() == r.events.size()
                  && w.simTable()->item(2, 2)->text() == QLatin1String("2")
                  && w.simSummary()->text().contains(QLatin1String("16746 pulses"))
                  && w.simSummary()->text().contains(QLatin1String("Never sent on reader 1: 102D")),
              QByteArray("Preview: the sends, and a summary: ") + w.simSummary()->text().toUtf8());
        SimPreview::Options o80;
        o80.speedKmh = 80;
        o80.startRow = 2;
        o80.reader2 = false;
        w.previewSimulator(o80);
        const SimPreview::Options shown = w.simulatorOptions();
        CHECK(shown.speedKmh == 80 && shown.startRow == 2 && !shown.reader2, "the controls show what was previewed");
        w.previewSimulator(w.simulatorOptions());
        CHECK(w.simTable()->item(0, 3)->text() == QLatin1String("101"), "starting from row 3: tag 101 first");
        w.previewSimulator(SimPreview::Options());
        emit w.simTable()->cellDoubleClicked(4, 0);
        CHECK(w.tabs()->currentIndex() == 0 && w.routeTable()->currentRow() == 2, "double-click: the tag in the route");
        RfidTag::Route noDir = pairs(RfidTag::DirNominal);
        noDir.dir = RfidTag::DirUnset;
        w.setRoute(noDir);
        CHECK(!w.previewSimulator(w.simulatorOptions()).ok && w.status()->kind() == StatusLine::Kind::Warn
                  && w.status()->text().contains(QLatin1String("direction")),
              "no direction: it says so");
        CHECK(w.minimumSizeHint().width() <= 1100 && LayoutAudit::orphans(&w).isEmpty(),
              QByteArray("fits, no orphan widgets (minimum width ") + QByteArray::number(w.minimumSizeHint().width()) + ")");
        w.setRoute(RfidTag::Route());
    }
}
