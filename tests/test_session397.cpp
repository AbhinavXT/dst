#include "testutil.h"

#include "locosim.h"
#include "locosimwindow.h"
#include "rfidtag.h"
#include "routestrip.h"
#include "settings.h"
#include "simpreview.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QSettings>
#include <QTableWidget>
#include <QToolButton>

#include <cmath>
#include <limits>

// =============================================================================
//  Session 397 — the Loco Simulator's RFID tab draws its route: the whole
//  route and a 500 m stretch that follows the loco (RouteStrip, the Tag
//  Builder's picture), the loco and where speed sensor 1's pulses put it,
//  each tag's readers, reader 2's window. On the simulator's own
//  Configuration1.xml.
// =============================================================================

TEST_SUITE(session397)
{
    const QVector<RfidTag::Route> routes =
        RfidTag::readFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
    CHECK(!routes.isEmpty(), "the simulator's routes");
    if (routes.isEmpty()) return;
    const RfidTag::Route &route = routes.at(0);
    const double nan = std::numeric_limits<double>::quiet_NaN();

    // ---- the strip ---------------------------------------------------------------------
    {
        RouteStrip strip;
        strip.resize(648, 150);
        strip.setRoute(route);
        CHECK(strip.sizeHint().height() == 100 && !strip.hasView(), "the Tag Builder's strip: as it was until the overlay is used");
        const double lo = strip.viewLow(), hi = strip.viewHigh();
        CHECK(std::abs(strip.xAt(lo) - 24) < 1e-9 && std::abs(strip.xAt(hi) - 624) < 1e-9, "the whole route across the width");
        strip.setView(lo + 100, lo + 600);
        CHECK(strip.hasView() && std::abs(strip.xAt(lo + 100) - 24) < 1e-9 && std::abs(strip.xAt(lo + 350) - 324) < 1e-9,
              "a view: 500 m across the width");
        strip.clearView();
        CHECK(!strip.hasView() && strip.viewLow() == lo, "and back to the whole route");
        strip.setLoco(lo + 50, nan);
        strip.setTagStates({ { 0, RouteStrip::SentOn1 | RouteStrip::SentOn2 }, { 2, RouteStrip::Skipped } });
        strip.setReader2Window(lo + 20, lo + 24);
        CHECK(strip.sizeHint().height() == 150 && strip.height() == 150 && strip.tagState(0) == 3 && strip.tagState(2) == 4,
              "the overlay: taller, the loco and the tags' states kept");
        QImage image(strip.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        strip.render(&image);
        bool locoDrawn = false;
        const int x = int(strip.xAt(lo + 50));
        for (int y = 115; y < 135 && !locoDrawn; ++y)
            for (int dx = -30; dx <= 30 && !locoDrawn; ++dx)
                locoDrawn = image.pixelColor(qBound(0, x + dx, image.width() - 1), y).alpha() > 0;
        CHECK(locoDrawn, "the loco is drawn under the line at its location");
    }

    // ---- the window ------------------------------------------------------------------
    {
        {
            QSettings settings(Settings::iniPath(), QSettings::IniFormat);
            settings.remove(QStringLiteral("locosim"));
        }
        ThemeUtil::apply(Theme::Light);
        LocoSimWindow window(nullptr);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.resize(1120, 720);
        window.setSendSink([](const QByteArray &) {});
        window.setTagSink([](int, const QByteArray &) {});
        CHECK(window.wholeRoute() && window.followView(), "the RFID tab: the whole route and the view near the loco");
        CHECK(window.loadRouteFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml")), "a route file");
        window.setRoute(0);
        const QVector<SimPreview::Row> rows = SimPreview::rowsOf(route);
        CHECK(window.wholeRoute()->loco() == rows.at(0).absLoc && window.followView()->hasView(),
              "the loco drawn at the start tag, the view following it");
        auto *toggle = window.findChild<QToolButton *>(QStringLiteral("locoSimTagsToggle"));
        auto *tags = window.findChild<QTableWidget *>(QStringLiteral("locoSimTags"));
        CHECK(toggle && tags && toggle->text() == QStringLiteral("Tags (%1)").arg(route.tags.size()) && tags->isHidden(),
              "the tag table folded away, its count on the button");
        toggle->click();
        CHECK(!tags->isHidden(), "and opened by it");

        // Clicking a tag on the whole route starts there.
        emit window.wholeRoute()->rowClicked(3);
        CHECK(window.routePosition() == rows.at(3).absLoc && window.findChild<QComboBox *>(QStringLiteral("locoSimStartTag"))->currentIndex() == 3,
              "a tag clicked on the whole route: the loco starts there");
        window.setStartRow(0);

        // Running: the view keeps the loco a third in; the pulses keep with it.
        window.findChild<QDoubleSpinBox *>(QStringLiteral("locoSimSpeed"))->setValue(60);
        window.start();
        for (int i = 0; i < 200; ++i) window.advance(0.1);
        const double sign = route.dir == RfidTag::DirReverse ? -1 : 1;
        RouteStrip *follow = window.followView();
        const double fromLow = (window.routePosition() - follow->viewLow()) / (follow->viewHigh() - follow->viewLow());
        CHECK(std::abs(follow->viewHigh() - follow->viewLow() - 500) < 1e-6
                  && std::abs(fromLow - (sign > 0 ? 1.0 / 3 : 2.0 / 3)) < 1e-6,
              QByteArray("500 m, the loco a third from where it came: ") + QByteArray::number(fromLow, 'f', 3));
        const double perMetre = LocoSim::pulsesPerMetre(window.motion().wheel());
        // Session 408: each message truncated, as the simulator: under a pulse lost per message.
        CHECK(std::abs(window.pulsePosition() - window.routePosition()) <= 200.0 / perMetre + 1e-9,
              QByteArray("no slip: the pulses put the loco where it is, within a pulse a message: ")
                  + QByteArray::number(window.pulsePosition() - window.routePosition(), 'f', 6) + " m");
        CHECK(window.wholeRoute()->tagState(0) & RouteStrip::SentOn1, "the first tag shows as sent on reader 1");

        // A skid on speed sensor 1 channel 1: the pulses fall behind.
        window.findChild<QDoubleSpinBox *>(QStringLiteral("locoSimChannel11"))->setValue(30);
        const double before = window.routePosition() - window.pulsePosition();
        for (int i = 0; i < 100; ++i) window.advance(0.1);
        const double behind = (window.routePosition() - window.pulsePosition()) * sign - before * sign;
        CHECK(std::abs(behind - (60 - 30) / 3.6 * 10) < 0.05,
              QByteArray("channel 1 at 30 km/h for 10 s: the pulses' loco 83.3 m behind: ") + QByteArray::number(behind, 'f', 2));
        CHECK(window.followView()->pulsesLoco() == window.pulsePosition(), "and drawn there");

        // A skipped tag shows on the strip.
        window.setSkipped(rows.at(5).id, true);
        CHECK(window.wholeRoute()->tagState(5) & RouteStrip::Skipped, "a skipped tag is marked on the route");
        window.stop();

        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
}
