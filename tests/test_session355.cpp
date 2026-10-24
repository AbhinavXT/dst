#include "testutil.h"
#include "layoutaudit.h"

#include "theme.h"
#include "uistyle.h"

#ifdef DL_HAVE_SERIAL
#include "cabiosimwindow.h"
#endif

#include <QCoreApplication>

// =============================================================================
//  Session 355 — the Cab I/O Simulator fits a 1366 x 700 laptop. The Inputs
//  tab's controls were one row, 1133 px on the Mac and about 1440 with
//  Linux's fonts; they are two rows now.
// =============================================================================

TEST_SUITE(session355)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
#ifdef DL_HAVE_SERIAL
    for (const CabIo::Cab cab : { CabIo::Cab::Sifa, CabIo::Cab::Old }) {
        CabIoSimWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.setCab(cab);
        w.show();
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        const QSize m = w.minimumSizeHint();
        CHECK(m.width() <= 1366 && m.height() <= 700,
              QByteArray(cab == CabIo::Cab::Sifa ? "SIFA" : "old") + " cab: the window fits a laptop ("
                  + QByteArray::number(m.width()) + " x " + QByteArray::number(m.height()) + ")");
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
    }
#endif
}
