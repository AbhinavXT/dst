#include "testutil.h"

#include "locosim.h"
#include "locosimwindow.h"
#include "rfidtag.h"
#include "settings.h"
#include "simpreview.h"
#include "theme.h"

#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

#include <algorithm>

// =============================================================================
//  Session 395 — Loco Simulator, step 2: the RFID readers over serial.
//  The routes are LocoTcasSimulator's own Configuration1.xml (the scenario
//  library's copy), so the rows are the file's. No real tag frame has been
//  captured off the serial line: the 22 bytes are laid out from the
//  simulator's LPIM_LTACS_NORMAL_TAG_PKT and its buildTagPacket().
// =============================================================================

namespace {

QVector<RfidTag::Route> simulatorRoutes()
{
    return RfidTag::readFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
}

struct Sent { int port = 0; int reader = 0; QString tag; };

// Drives the readers from the start row at a constant step; what they send, in order.
QVector<SimPreview::Readers::Send> drive(const RfidTag::Route &route, double step, double extra)
{
    SimPreview::Readers readers(SimPreview::rowsOf(route));
    const double sign = route.dir == RfidTag::DirReverse ? -1 : 1;
    double d = readers.rows().value(0).absLoc;
    QVector<SimPreview::Readers::Send> all;
    for (int i = 0; i < 1000000; ++i) {
        d += sign * step;
        if (readers.step(d, extra, &all) < 0) break;
    }
    return all;
}

}  // namespace

TEST_SUITE(session395)
{
    const QVector<RfidTag::Route> routes = simulatorRoutes();
    CHECK(routes.size() >= 2, "the simulator's Configuration1.xml: its routes");
    if (routes.size() < 2) return;
    const RfidTag::Route &route = routes.at(0);

    // ---- the frame -------------------------------------------------------------------
    {
        const RfidTag::Tag &tag = route.tags.at(0);
        const QByteArray frame = LocoSim::tagFrame(2, tag.bytes);
        CHECK(frame.size() == 22 && frame.left(4) == QByteArray::fromHex("aaaa1602") && frame.mid(4, 16) == tag.bytes
                  && frame.right(2) == QByteArray::fromHex("bbbb"),
              QByteArray("AA AA, length 22, reader 2, the 16 tag bytes, BB BB: ") + frame.toHex(' '));
        // The simulator's buildTagPacket(): page_y + page_x from the file's row, as hex, reversed.
        QByteArray simulator = QByteArray::fromHex((tag.file.pageY + tag.file.pageX).toLatin1());
        std::reverse(simulator.begin(), simulator.end());
        CHECK(tag.file.present && simulator == frame.mid(4, 16),
              QByteArray("the same 16 bytes the simulator builds from the row's page_x / page_y (") + tag.file.rfidId.toUtf8() + ")");
        CHECK(LocoSim::tagFrame(1, QByteArray(15, '\0')).isEmpty(), "not 16 bytes: no frame");
    }

    // ---- the readers, a location at a time ----------------------------------------------------
    {
        SimPreview::Options o;
        o.speedKmh = 80;
        const SimPreview::Result preview = SimPreview::run(route, o);
        const QVector<SimPreview::Readers::Send> stepped = drive(route, preview.metresPerTick, 0);
        bool same = preview.ok && stepped.size() == preview.events.size();
        for (int i = 0; same && i < stepped.size(); ++i)
            same = stepped.at(i).reader == preview.events.at(i).reader && stepped.at(i).row == preview.events.at(i).row;
        CHECK(same, QByteArray("stepped at the preview's tick, the readers send what the preview lists: ")
                         + QByteArray::number(stepped.size()) + " sends");

        // 160 km/h: 4.4 m a tick, longer than reader 2's 20-24 m window.
        const double step = 160 / 3.6 * 0.1;
        const auto mainsOn2 = [](const QVector<SimPreview::Readers::Send> &sends) {
            return int(std::count_if(sends.begin(), sends.end(), [](const SimPreview::Readers::Send &s) {
                return s.reader == 2 && !RfidTag::summary(s.tag).duplicate;
            }));
        };
        const int fixed = mainsOn2(drive(route, step, 0));
        const int widened = mainsOn2(drive(route, step, step));
        CHECK(widened > fixed, QByteArray("the window widened by a tick's travel: reader 2 sends ")
                                   + QByteArray::number(widened) + " main tags, not " + QByteArray::number(fixed));
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
        window.setSendSink([](const QByteArray &) {});
        CHECK(window.routeCount() == 0 && window.routeRow() < 0, "no route until one is opened");

        QLabel *portState = nullptr;
        for (QLabel *label : window.findChildren<QLabel *>())
            if (label->text() == QLatin1String("No serial ports in this build")) portState = label;
        CHECK(portState != nullptr, "no SerialManager: the tag ports say so");

        CHECK(window.loadRouteFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"))
                  && window.routeCount() == routes.size(),
              "opens the simulator's Configuration1.xml: every route in it");
        window.setRoute(0);
        const QVector<SimPreview::Row> rows = SimPreview::rowsOf(route);
        CHECK(window.routeRow() == 0 && window.routePosition() == rows.at(0).absLoc,
              "a route: the loco at its first tag");
        QTableWidget *tags = window.findChild<QTableWidget *>(QStringLiteral("locoSimTags"));
        CHECK(tags && tags->rowCount() == route.tags.size() && tags->item(0, 1)->text() == rows.at(0).id,
              "the tag table: the route's rows");

        // Without a port: the loco runs, nothing is sent, and the cell says why.
        window.findChild<QDoubleSpinBox *>(QStringLiteral("locoSimSpeed"))->setValue(120);
        window.start();
        window.advance(0.1);
        CHECK(window.tagsSent() == 0 && tags->item(0, 6)->text().contains(QLatin1String("port A closed")),
              QByteArray("no port open: reader 1's first tag not sent, and said: ") + tags->item(0, 6)->text().toUtf8());

        // Reader 2 on its own port: frames to port B.
        QVector<Sent> sent;
        window.setTagSink([&sent](int port, const QByteArray &frame) {
            sent.append({ port, frame.at(3), RfidTag::nameOf(frame.mid(4, 16)) });
        });
        window.setReader2OwnPort(true);
        window.placeAtStart();
        for (int i = 0; i < 100000 && !window.routeEnded(); ++i) window.advance(0.1);
        CHECK(window.routeEnded(), "runs to the end of the route");
        SimPreview::Options o;
        o.speedKmh = 120;
        const SimPreview::Result preview = SimPreview::run(route, o);
        QStringList expected1, expected2, got1, got2;
        for (const SimPreview::Event &e : preview.events) (e.reader == 1 ? expected1 : expected2) << e.tag;
        bool portsRight = true;
        for (const Sent &s : sent) {
            (s.reader == 1 ? got1 : got2) << s.tag;
            portsRight = portsRight && s.port == (s.reader == 2 ? 1 : 0);
        }
        CHECK(!got1.isEmpty() && got1 == expected1,
              QByteArray("reader 1 sends the route's tags in the preview's order: ") + got1.join(QLatin1Char(' ')).toUtf8());
        CHECK(!got2.isEmpty() && got2 == expected2,
              QByteArray("reader 2 the same as the preview: ") + got2.join(QLatin1Char(' ')).toUtf8());
        CHECK(portsRight, "reader 1 on port A, reader 2 on port B");
        CHECK(window.tagsSent() == sent.size() && !got1.contains(route.tags.last().file.rfidId),
              "every frame counted; the route's last tag is never sent, as in the simulator");

        // Reader 1 off, a tag skipped, reader 2 back on port A.
        const QString skipped = rows.at(2).id;
        window.setReaderOn(1, false);
        window.setReader2OwnPort(false);
        window.setSkipped(skipped, true);
        window.placeAtStart();
        sent.clear();
        for (int i = 0; i < 100000 && !window.routeEnded(); ++i) window.advance(0.1);
        const bool noReader1 = std::none_of(sent.begin(), sent.end(), [](const Sent &s) { return s.reader == 1; });
        const bool allOnA = std::all_of(sent.begin(), sent.end(), [](const Sent &s) { return s.port == 0; });
        CHECK(!sent.isEmpty() && noReader1 && allOnA, "reader 1 off: only reader 2, now on port A");
        SimPreview::Options skip = o;
        skip.reader1 = false;
        skip.missing = QStringList{ skipped };
        QStringList expectedSkip, gotSkip;
        for (const SimPreview::Event &e : SimPreview::run(route, skip).events) expectedSkip << e.tag;
        for (const Sent &s : sent) gotSkip << s.tag;
        CHECK(gotSkip == expectedSkip, QByteArray("tag ") + skipped.toUtf8() + " skipped: what the preview says reader 2 then sends");
        window.setReaderOn(1, true);
        window.setSkipped(skipped, false);

        // A reverse route: the location counts down; Forward / Reverse is only the sensors' bit.
        int reverse = -1;
        for (int i = 0; i < routes.size() && reverse < 0; ++i)
            if (routes.at(i).dir == RfidTag::DirReverse) reverse = i;
        CHECK(reverse >= 0, "the file holds a dir 2 route");
        if (reverse >= 0) {
            window.setRoute(reverse);
            const double from = window.routePosition();
            window.advance(0.1);
            CHECK(window.routePosition() < from && std::abs(from - window.routePosition() - 120 / 3.6 * 0.1) < 1e-6,
                  "dir 2: the loco's location counts down by its travel");
        }
        window.stop();

        // A route with no direction: refused.
        QTemporaryDir dir;
        RfidTag::Route none = route;
        none.dir = RfidTag::DirUnset;
        const QString path = dir.filePath(QStringLiteral("none.tagroute.xml"));
        CHECK(RfidTag::writeFile(path, RfidTag::toTagRouteXml(none)) && window.loadRouteFile(path), "a route file with no direction opens");
        window.setRoute(0);
        CHECK(!window.placeAtStart() && window.routeRow() < 0, "but the loco is not put on it: it moves by the direction");

        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
}
