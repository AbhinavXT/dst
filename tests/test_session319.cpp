#include "testutil.h"

#include "flasherwindow.h"

#include <QTemporaryDir>
#include <QToolButton>

// =============================================================================
//  Session 319 — the Flasher's updater switch reads "Classic | Signed".
//
//  Abhinav: "instead of 2.6 write signed there". The mode is unchanged (HOLD,
//  REJECTED, BOOT, the signature checks); only its button's name. Profiles
//  and history still store "2.6", so older files read the same.
// =============================================================================

TEST_SUITE(session319)
{
    QTemporaryDir temp;
    CHECK(temp.isValid(), "fixture: a temporary data directory");
    FlasherWindow w(nullptr, temp.path());
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    QToolButton *signedButton = w.updaterButton(kflash::Updater::V26);
    QToolButton *classic = w.updaterButton(kflash::Updater::Classic);
    CHECK(signedButton && signedButton->text() == QStringLiteral("Signed"), "the second choice reads Signed");
    CHECK(signedButton && signedButton->toolTip().contains(QStringLiteral("2.7")), "its tooltip names the updaters it covers");
    CHECK(classic && classic->text() == QStringLiteral("Classic"), "Classic unchanged");
    CHECK(QString::fromLatin1(kflash::updater_name(kflash::Updater::V26)) == QLatin1String("2.6"),
          "the stored name is still 2.6");
}
