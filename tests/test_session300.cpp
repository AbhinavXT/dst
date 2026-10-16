#include "testutil.h"

#include "flashercore.h"
#include "flasherqueuepage.h"
#include "vcclistedit.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>

// =============================================================================
//  Session 300 — the Firmware Flasher: the 2.6 updater's loco addresses.
//
//  Updater 2.6.0 puts each VCC controller on its application's address, per
//  loco (enet_user.c, kLocoIpLastOctet). With the 2.6 updater the VCC list
//  gets "Loco ▾" (one controller, or all four of a loco), labels each known
//  address, and the pre-flight refuses a card image sent to anything but VCC 1
//  controller 1 and any image sent to .167 (a strap fault), and warns about a
//  192.168.25.x address that is not in the table (the old .168).
// =============================================================================

namespace {

Flasher::PreflightInput inputFor(const QString &ip, int cardType, kflash::Updater updater = kflash::Updater::V26)
{
    Flasher::PreflightInput input;
    input.vccIp = ip;
    input.port = 50001;
    input.adapterOnSubnet = true;
    input.subnetText = QStringLiteral("192.168.25.x");
    input.updater = updater;
    Flasher::PreflightRow row;
    row.cardType = cardType;
    row.ticked = true;
    row.hasPath = true;
    row.loaded = true;
    row.sizeBytes = 100000;
    row.fileName = cardType == Flasher::CardVcc ? QStringLiteral("LKAVACH_v1.2.9.appimage")
                                                : QStringLiteral("KAVACH_Input_Card_v5.appimage");
    row.nameCheck = Flasher::checkImageName(row.fileName, cardType);
    input.rows.append(row);
    return input;
}

bool hasItem(const Flasher::PreflightReport &report, const QString &text, bool *blocks = nullptr)
{
    for (const Flasher::PreflightItem &item : report.items) {
        if (item.text.contains(text)) {
            if (blocks) *blocks = item.blocksFlash;
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_SUITE(session300)
{
    // ---- the table ---------------------------------------------------------------------
    const QVector<Flasher::LocoController> all = Flasher::locoControllers();
    CHECK(all.size() == 12 && Flasher::locoCount() == 3, "3 locos x 4 controllers");
    QStringList loco1, loco2, loco3;
    for (const Flasher::LocoController &c : all) {
        (c.loco == 1 ? loco1 : c.loco == 2 ? loco2 : loco3) << c.ip.mid(11);
    }
    CHECK(loco1.join(QLatin1Char(' ')) == QLatin1String("16 102 103 104")
              && loco2.join(QLatin1Char(' ')) == QLatin1String("77 105 106 107")
              && loco3.join(QLatin1Char(' ')) == QLatin1String("87 88 96 97"),
          QByteArray("the guide's table: ") + loco1.join(QLatin1Char(' ')).toUtf8() + " / "
              + loco2.join(QLatin1Char(' ')).toUtf8() + " / " + loco3.join(QLatin1Char(' ')).toUtf8());
    const Flasher::LocoController c = Flasher::locoControllerAt(QStringLiteral("192.168.25.106:50001"));
    CHECK(c.loco == 2 && c.vcc == 2 && c.controller == 1 && c.name() == QStringLiteral("Loco 2 · VCC 2 controller 1"),
          "an address (port ignored) names its controller");
    CHECK(Flasher::locoControllerAt(QStringLiteral("192.168.25.168")).loco == 0, ".168 is not one");
    CHECK(Flasher::locoRelayIp(1) == QLatin1String("192.168.25.16") && Flasher::locoRelayIp(3) == QLatin1String("192.168.25.87"),
          "each loco's card relay is VCC 1 controller 1");

    // ---- pre-flight ----------------------------------------------------------------------
    {
        bool blocks = true;
        const Flasher::PreflightReport vcc = Flasher::evaluatePreflight(inputFor(QStringLiteral("192.168.25.102"), Flasher::CardVcc));
        CHECK(vcc.ready && hasItem(vcc, QStringLiteral("Target: Loco 1 · VCC 1 controller 2"), &blocks) && !blocks,
              "2.6: a VCC image to a known controller names it");

        const Flasher::PreflightReport wrongRelay = Flasher::evaluatePreflight(
            inputFor(QStringLiteral("192.168.25.105"), Flasher::CardInput));
        CHECK(!wrongRelay.ready && wrongRelay.firstBlocker == QStringLiteral(
                  "Input card images go to VCC 1 controller 1 of the loco (192.168.25.77); 192.168.25.105 is "
                  "Loco 2 · VCC 1 controller 2, which would refuse them"),
              QByteArray("2.6: a card image to another controller is refused: ") + wrongRelay.firstBlocker.toUtf8());

        const Flasher::PreflightReport relay = Flasher::evaluatePreflight(
            inputFor(QStringLiteral("192.168.25.87"), Flasher::CardInput));
        CHECK(relay.ready && hasItem(relay, QStringLiteral("Target: Loco 3 · VCC 1 controller 1")),
              "2.6: a card image to its loco's VCC 1 controller 1 is ready");

        const Flasher::PreflightReport strap = Flasher::evaluatePreflight(
            inputFor(QStringLiteral("192.168.25.167"), Flasher::CardVcc));
        CHECK(!strap.ready && strap.firstBlocker.contains(QLatin1String("ID is unknown (a strap fault)")),
              "2.6: .167 refuses every image");

        const Flasher::PreflightReport old = Flasher::evaluatePreflight(
            inputFor(QStringLiteral("192.168.25.168"), Flasher::CardVcc));
        CHECK(old.ready && hasItem(old, QStringLiteral("192.168.25.168 is not a 2.6 controller address"), &blocks) && !blocks,
              "2.6: the old .168 is a warning, not a block");

        const Flasher::PreflightReport classic = Flasher::evaluatePreflight(
            inputFor(QStringLiteral("192.168.25.105"), Flasher::CardInput, kflash::Updater::Classic));
        CHECK(classic.ready && !hasItem(classic, QStringLiteral("Loco")) && !hasItem(classic, QStringLiteral("2.6")),
              "Classic: no loco checks");

        const Flasher::PreflightReport loopback = Flasher::evaluatePreflight(
            inputFor(QStringLiteral("127.0.0.1"), Flasher::CardVcc));
        CHECK(loopback.ready && !hasItem(loopback, QStringLiteral("2.6 controller")), "the simulator's address: no warning");

        const Flasher::PreflightReport four = Flasher::evaluatePreflight(
            inputFor(QStringLiteral("192.168.25.16, 192.168.25.102, 192.168.25.103, 192.168.25.104"), Flasher::CardVcc));
        CHECK(four.ready && !hasItem(four, QStringLiteral("Target:")), "a loco's four VCCs: ready, no per-target lines");
    }

    // ---- the VCC list ---------------------------------------------------------------------
    {
        VccListEdit list;
        CHECK(list.locoButton() && !list.locoPickerShown() && list.locoButton()->isHidden(), "Classic: no Loco button");
        list.setText(QStringLiteral("192.168.25.16"));
        CHECK(list.rowLabel(0).isEmpty(), "Classic: no labels");

        list.setLocoPicker(true);
        CHECK(list.locoPickerShown() && list.rowLabel(0) == QStringLiteral("Loco 1 · VCC 1 controller 1"),
              "2.6: the row is labelled with its controller");
        QMenu *menu = list.locoButton()->menu();
        CHECK(menu && menu->actions().size() == 3, "the menu: a submenu per loco");
        if (menu && menu->actions().size() == 3) {
            QMenu *loco2 = menu->actions().at(1)->menu();
            CHECK(loco2 && loco2->actions().size() == 6
                      && loco2->actions().first()->text().contains(QLatin1String(".77"))
                      && loco2->actions().first()->text().contains(QLatin1String("card images"))
                      && loco2->actions().last()->text().startsWith(QLatin1String("All 4 controllers")),
                  "Loco 2: four controllers (VCC 1 controller 1 marked for card images), then all four");
            if (loco2 && loco2->actions().size() == 6) {
                loco2->actions().at(2)->trigger();   // VCC 2 controller 1, .106
                CHECK(list.rowCount() == 1 && list.text() == QLatin1String("192.168.25.106"),
                      "one row: a controller replaces it");
                loco2->actions().last()->trigger();
                CHECK(list.rowCount() == 4 && list.text() == QLatin1String(
                          "192.168.25.77, 192.168.25.105, 192.168.25.106, 192.168.25.107")
                          && list.rowLabel(3) == QStringLiteral("Loco 2 · VCC 2 controller 2"),
                      QByteArray("All 4 controllers: the loco's four rows, labelled: ") + list.text().toUtf8());
            }
        }
        list.pickAddress(QStringLiteral("192.168.25.16"));
        CHECK(list.rowCount() == 5 && list.text().endsWith(QLatin1String("192.168.25.16")),
              "with several rows, a controller is added");
        list.pickAddress(QStringLiteral("192.168.25.16"));
        CHECK(list.rowCount() == 5, "and not twice");
        list.edit(0)->setText(QStringLiteral("192.168.25.168"));
        CHECK(list.rowLabel(0).isEmpty(), "an address outside the table has no label");
    }

    // ---- the queue page follows the updater -------------------------------------------
    {
        FlasherQueuePage page;
        Flasher::FlashProfile profile;
        profile.vccIp = QStringLiteral("192.168.25.16");
        page.applyProfile(profile);
        CHECK(!page.vccList()->locoPickerShown(), "a Classic profile: no Loco button");
        page.setUpdater(kflash::Updater::V26);
        CHECK(page.vccList()->locoPickerShown() && page.vccList()->rowLabel(0) == QStringLiteral("Loco 1 · VCC 1 controller 1"),
              "switching to 2.6 shows it and labels the row");
        profile.updater = kflash::Updater::V26;
        page.applyProfile(profile);
        CHECK(page.vccList()->locoPickerShown(), "a 2.6 profile shows it from the start");
    }
}
