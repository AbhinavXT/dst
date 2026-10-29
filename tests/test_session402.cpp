#include "testutil.h"

#include "locosimwindow.h"
#include "rfidtag.h"
#include "settings.h"
#include "tagbuilderwindow.h"
#include "tageditdialog.h"

#include <QPushButton>
#include <QSettings>

#include <cmath>
#include <memory>

// =============================================================================
//  Session 402 — the Loco Simulator's RFID tab runs the route open in the
//  RFID Tag Builder, as it is now, saved or not. Each edit there comes here;
//  the loco stays where it is, the tags ahead go out as edited. The route is
//  LocoTcasSimulator's own Configuration1.xml (the scenario library's copy).
// =============================================================================

TEST_SUITE(session402)
{
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
    const QVector<RfidTag::Route> routes
        = RfidTag::readFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml"));
    CHECK(!routes.isEmpty() && routes.first().tags.size() >= 6
              && (routes.first().dir == RfidTag::DirNominal || routes.first().dir == RfidTag::DirReverse),
          "the simulator's Configuration1.xml: a route of 6 tags or more, its direction set");
    if (routes.isEmpty() || routes.first().tags.size() < 6) return;
    const RfidTag::Route route = routes.first();
    const double sign = route.dir == RfidTag::DirReverse ? -1 : 1;

    auto builder = std::make_unique<TagBuilderWindow>(nullptr);
    builder->setAttribute(Qt::WA_DeleteOnClose, false);
    builder->setRoute(route);
    builder->show();   // the button follows the builders on screen

    LocoSimWindow window(nullptr);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    qint64 t = 1782558147000LL;
    window.setClock([&t]() { return t; });
    window.setSendSink([](const QByteArray &) {});
    QVector<QByteArray> tagsOut;
    window.setTagSink([&tagsOut](int, const QByteArray &frame) { tagsOut.append(frame.mid(4, 16)); });
    QList<TagBuilderWindow *> open;
    window.setTagBuilders([&open]() { return open; });
    auto *button = window.findChild<QPushButton *>(QStringLiteral("locoSimFromBuilder"));
    CHECK(button, "the RFID tab has a Tag Builder button");
    if (!button) return;

    button->click();
    CHECK(!window.followsBuilder() && window.routeCount() == 0, "no Tag Builder open: nothing to follow (and the status line says so)");
    open = { builder.get() };
    button->click();
    CHECK(window.followsBuilder() && window.routeCount() == 1, "one open: the RFID tab follows its route");
    const double start = window.routePosition();
    CHECK(std::fabs(start - RfidTag::summary(route.tags.first().bytes).absLoc) < 1e-6, "the loco at the route's first tag");

    // Run 30 m along the route.
    window.motion().setSpeed(36);   // 10 m/s
    CHECK(window.start(), "starts");
    const auto run = [&](int ticks) {
        for (int i = 0; i < ticks; ++i) {
            t += 100;
            window.tick();
        }
    };
    run(30);
    const double here = window.routePosition();
    CHECK(std::fabs(here - (start + sign * 30)) < 0.01, "30 m on");

    // A new tag 25 m ahead, added on the builder's route drawing (session 401).
    const qint64 ahead = qint64(std::llround(here + sign * 25));
    TagEditDialog *dialog = builder->newTagInDialog(ahead);
    CHECK(dialog, "the builder makes a new tag 25 m ahead");
    if (!dialog) return;
    const QByteArray added = dialog->tag();
    dialog->accept();
    CHECK(builder->route().tags.size() == route.tags.size() + 1, "added in the builder");
    CHECK(std::fabs(window.routePosition() - here) < 1e-9, "the loco stays where it was");
    tagsOut.clear();
    run(40);
    CHECK(tagsOut.contains(added), "when the loco gets there, the new tag goes out: the edit came across, unsaved");

    // A tag ahead changed in the builder: it goes out as changed.
    int aheadRow = -1;
    for (int i = 0; i < builder->route().tags.size() && aheadRow < 0; ++i) {
        const RfidTag::Summary s = RfidTag::summary(builder->route().tags.at(i).bytes);
        if (!s.duplicate && (s.absLoc - window.routePosition()) * sign > 30) aheadRow = i;
    }
    CHECK(aheadRow >= 0, "a main tag more than 30 m ahead");
    if (aheadRow < 0) return;
    dialog = builder->editRowInDialog(aheadRow);
    const QByteArray original = builder->route().tags.at(aheadRow).bytes;
    const RfidTag::Summary os = RfidTag::summary(original);
    dialog->setField(os.type == 12 ? QStringLiteral("abs_loc_1") : QStringLiteral("abs_loc"), os.absLoc + qint64(sign) * 2);
    const QByteArray edited = dialog->tag();
    dialog->accept();
    tagsOut.clear();
    for (int i = 0; i < 3000 && !tagsOut.contains(edited) && !window.routeEnded(); ++i) run(1);
    CHECK(tagsOut.contains(edited) && !tagsOut.contains(original), "the edited tag goes out, not the file's");

    // The route moved away from the loco: back to the start tag.
    window.stop();
    CHECK(builder->shiftAll(500000), "every tag moved 500 km in the builder");
    CHECK(std::fabs(window.routePosition() - RfidTag::summary(builder->route().tags.first().bytes).absLoc) < 1e-6,
          "the loco is not on the edited route: back at its first tag");

    // When the builder closes, the route stays.
    builder.reset();
    CHECK(!window.followsBuilder() && window.routeCount() == 1, "the builder closed: the last route stays");

    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("locosim"));
}
