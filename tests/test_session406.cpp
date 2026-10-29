#include "testutil.h"

#include "benchrecord.h"
#include "locosimwindow.h"
#include "rfidtag.h"
#include "settings.h"
#include "simpreview.h"

#include <QComboBox>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>

// =============================================================================
//  Session 406 — route chaining in the Loco Simulator's RFID tab: at a
//  route's end the loco runs on into the next route, found by location (same
//  direction, its first tag nearest ahead of the last one, in the same file or
//  folder), or the routes set in an order; or it stops. The routes are
//  LocoTcasSimulator's own Configuration1.xml: 1_DN (dir 1) ends at 164414 m,
//  124_DN_LOOP1_HFZ and 125_DN_LOOP2_HFZ both start at 164610 m.
// =============================================================================

namespace {

const QString kFile406 = QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml");

int index406(const QVector<RfidTag::Route> &routes, const QString &name)
{
    for (int i = 0; i < routes.size(); ++i)
        if (routes.at(i).name == name) return i;
    return -1;
}

// From a few tags before the route's end, at 120 km/h, until the loco is
// on `until` or the route has ended; the names of the tags sent.
QSet<QString> run406(LocoSimWindow &window, qint64 &t, const QString &until, int maxTicks = 3000)
{
    QSet<QString> sent;
    window.setTagSink([&sent](int, const QByteArray &frame) { sent.insert(RfidTag::nameOf(frame.mid(4, 16))); });
    window.motion().setSpeed(120);
    window.start();
    int onIt = 0;   // ticks on `until`: 30 s there, past its first tags
    for (int i = 0; i < maxTicks && !window.routeEnded(); ++i) {
        t += 100;
        window.tick();
        if (!until.isEmpty() && window.routeName() == until && ++onIt >= 300) break;
    }
    window.stop();
    window.setTagSink({});
    return sent;
}

}  // namespace

TEST_SUITE(session406)
{
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
    const QVector<RfidTag::Route> routes = RfidTag::readFile(kFile406);
    const int dn = index406(routes, QStringLiteral("1_DN")), hfz1 = index406(routes, QStringLiteral("124_DN_LOOP1_HFZ")),
              hfz2 = index406(routes, QStringLiteral("125_DN_LOOP2_HFZ")), rev = index406(routes, QStringLiteral("2_REV_DN"));
    CHECK(dn >= 0 && hfz1 >= 0 && hfz2 >= 0 && rev >= 0, "the simulator's file: 1_DN, 124_DN_LOOP1_HFZ, 125_DN_LOOP2_HFZ, 2_REV_DN");
    if (dn < 0 || hfz1 < 0 || hfz2 < 0 || rev < 0) return;

    // ---- the rule --------------------------------------------------------------------------------
    double ahead = 0;
    QVector<int> tied;
    CHECK(SimPreview::nextRoute(routes, routes.at(dn), &ahead, &tied) == hfz1 && std::fabs(ahead - 196) < 1e-6,
          qPrintable(QStringLiteral("after 1_DN: 124_DN_LOOP1_HFZ, its first tag 196 m on (%1)").arg(ahead)));
    CHECK(tied == QVector<int>{ hfz2 }, "125_DN_LOOP2_HFZ named as just as near");
    CHECK(SimPreview::nextRoute(routes, routes.at(rev), &ahead) == -1,
          "after 2_REV_DN (dir 2, ending at 159500 m): no dir 2 route starts within 5 km ahead");
    CHECK(SimPreview::nextRoute({ routes.at(dn), routes.at(rev) }, routes.at(dn)) == -1,
          "not itself, and not its REV twin (the other direction)");

    LocoSimWindow window(nullptr);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    qint64 t = 1782558147000LL;
    window.setClock([&t]() { return t; });
    window.setSendSink([](const QByteArray &) {});
    CHECK(window.loadRouteFile(kFile406), "the file opened");
    const auto nearEnd = [&]() {
        window.setRoute(dn);
        auto *start = window.findChild<QComboBox *>(QStringLiteral("locoSimStartTag"));
        window.setStartRow(start ? start->count() - 6 : 0);
    };

    // ---- stop (the default) --------------------------------------------------------------------
    nearEnd();
    CHECK(window.chain() == LocoSimWindow::Chain::Stop && window.chainNote() == QLatin1String("then: stop"), "by default: stop at the end");
    run406(window, t, QString());
    CHECK(window.routeEnded() && window.routeName() == QLatin1String("1_DN") && window.chainedCount() == 0,
          "stop: the route ends, nothing more sent");

    // ---- the next route, by location --------------------------------------------------------------
    window.setChain(LocoSimWindow::Chain::Auto);
    nearEnd();
    CHECK(window.chainNote().startsWith(QLatin1String("then: 124_DN_LOOP1_HFZ, 196 m on"))
              && window.chainNote().contains(QLatin1String("125_DN_LOOP2_HFZ")),
          qPrintable(QStringLiteral("the note: %1").arg(window.chainNote())));
    QTemporaryDir dir;
    BenchRecord::Recorder recorder;
    window.setRecorder(&recorder);
    recorder.start(dir.filePath(QStringLiteral("c.benchrec")), t);
    const QSet<QString> auto1 = run406(window, t, QStringLiteral("124_DN_LOOP1_HFZ"));
    recorder.stop(t);
    window.setRecorder(nullptr);
    CHECK(window.routeName() == QLatin1String("124_DN_LOOP1_HFZ") && window.chainedCount() == 1 && !window.routeEnded(),
          "at 1_DN's end the loco ran on into 124_DN_LOOP1_HFZ");
    QSet<QString> onlyNext;
    for (const RfidTag::Tag &tag : routes.at(hfz1).tags) onlyNext.insert(RfidTag::nameOf(tag.bytes));
    for (const RfidTag::Tag &tag : routes.at(dn).tags) onlyNext.remove(RfidTag::nameOf(tag.bytes));
    CHECK(auto1.intersects(onlyNext), "and its own tags went out");
    BenchRecord::File f;
    BenchRecord::read(dir.filePath(QStringLiteral("c.benchrec")), &f);
    bool marked = false;
    for (const BenchRecord::Item &i : f.items)
        marked = marked || (i.kind == QLatin1String("event") && i.says == QLatin1String("Route 1_DN ended: on to route 124_DN_LOOP1_HFZ"));
    CHECK(marked, "the recording marks the change of route");

    // ---- routes in order ------------------------------------------------------------------------
    window.setChainList({ { kFile406, dn, QStringLiteral("1_DN") }, { kFile406, hfz2, QStringLiteral("125_DN_LOOP2_HFZ") } });
    nearEnd();
    CHECK(window.chain() == LocoSimWindow::Chain::List && window.chainNote().startsWith(QLatin1String("then: 125_DN_LOOP2_HFZ")),
          "in order: 1_DN, then 125_DN_LOOP2_HFZ (the one by location would be 124)");
    run406(window, t, QString(), 6000);
    CHECK(window.routeName() == QLatin1String("125_DN_LOOP2_HFZ") && window.routeEnded(),
          "it ran into 125, and stopped at 125's end: the order's end");
    CHECK(window.chainNote() == QLatin1String("then: the order's end"), "the note says so");

    // ---- nothing continues ------------------------------------------------------------------------
    window.setChain(LocoSimWindow::Chain::Auto);
    window.setRoute(rev);
    CHECK(window.chainNote().startsWith(QLatin1String("then: no route continues")), qPrintable(window.chainNote()));

    // The candidates: this file's routes (the library folder has no other file beside it).
    const QVector<LocoSimWindow::ChainRef> refs = window.chainCandidates();
    CHECK(refs.size() >= routes.size() && refs.first().file == kFile406, "the candidates: this file's routes first");

    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
}
