#include "testutil.h"

#include "flasherprofiledialog.h"
#include "laneband.h"
#include "vcclistedit.h"

#include <QFontMetrics>
#include <QPushButton>
#include <QToolButton>

// =============================================================================
//  Session 315 — two fixes from the field PC (Windows, larger system font).
//
//  1. Edit profile holds a loco's four VCC controllers: the queue page's VCC
//     list, with Loco addresses ▾, replaces the one-line IP field.
//  2. The lane band's heights follow its font: "Lanes ▾" was clipped and the
//     lane names crowded at fixed 16/22 px.
// =============================================================================

TEST_SUITE(session315)
{
    // ---- the profile's VCC list ------------------------------------------------------
    {
        Flasher::FlashProfile profile;
        profile.name = QStringLiteral("loco");
        profile.vccIp = QStringLiteral("192.168.25.16");
        profile.updater = kflash::Updater::Classic;
        FlasherProfileDialog dialog(profile, Flasher::Mode::Engineer, { QStringLiteral("loco") }, false);
        auto *list = dialog.findChild<VccListEdit *>();
        CHECK(list && list->rowCount() == 1 && list->text() == QStringLiteral("192.168.25.16"),
              "the profile's address is the list's one row");
        CHECK(list && list->locoPickerShown(), "Loco addresses ▾ is offered, with either updater");
        if (list) {
            list->pickLoco(1);
            CHECK(list->rowCount() == 4, "All 4 controllers gives four rows");
        }
        QPushButton *save = nullptr;
        for (QPushButton *b : dialog.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Save profile")) save = b;
        CHECK(save, "the Save profile button");
        if (save) save->click();
        CHECK(dialog.result() == QDialog::Accepted, "four addresses save");
        CHECK(Flasher::parseVccTargets(dialog.profile().vccIp).targets.size() == 4
                  && dialog.profile().vccIp.startsWith(QStringLiteral("192.168.25.16")),
              QByteArray("the profile keeps all four (") + dialog.profile().vccIp.toUtf8() + ")");
    }

    // ---- lane heights from the font --------------------------------------------------
    {
        LaneBand band;
        band.resize(800, 200);
        const int smallHeight = band.sizeHint().height();
        const int smallLane = band.laneRect(0).height();
        QFont big = band.font();
        big.setPointSizeF(20);
        band.setFont(big);
        auto *button = band.findChild<QToolButton *>(QStringLiteral("laneMenuButton"));
        CHECK(button && button->height() >= button->sizeHint().height(), "Lanes ▾ is not clipped");
        CHECK(button && band.laneRect(0).top() >= button->geometry().bottom(), "the first lane starts below it");
        QFont small = big;
        small.setPointSizeF(big.pointSizeF() - 1.5);
        CHECK(band.laneRect(0).height() >= QFontMetrics(small).height(), "a lane is as tall as its name");
        CHECK(band.laneRect(0).height() > smallLane && band.sizeHint().height() > smallHeight,
              "a bigger font makes a taller band");
    }
}
