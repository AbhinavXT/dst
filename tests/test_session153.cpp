#include "testutil.h"
#include "layoutaudit.h"

#include "packetmakerdialog.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QGroupBox>

#ifdef DL_HAVE_SERIAL
#include "messagedispatcher.h"
#include "serialconsolewindow.h"
#include "serialmanager.h"
#endif

// =============================================================================
//  Session 153 — the review pass after the revamp: every window shot again in
//  dark and light. Two findings, both fixed:
//
//  * The serial terminal was 1480 px wide at its minimum, over a 1366-px
//    laptop: one row held port, baud, data, parity, stop, flow, the three
//    line switches and Open. The line settings have a row of their own.
//  * A checkable group box's switch (Packet Maker's "Also send to", "Extra
//    header fields", "Fill from buffer") had no stylesheet rule and was drawn
//    dark on dark when off. It shares the checkbox's rules now.
// =============================================================================

TEST_SUITE(session153)
{
    // ---- group-box switches are styled like checkboxes, every theme ----------------
    QStringList missing;
    for (Theme t : ThemeUtil::all()) {
        ThemeUtil::apply(t);
        UiStyle::apply();
        const QString sheet = UiStyle::sheet();
        if (!sheet.contains(QLatin1String("QGroupBox::indicator {")) && !sheet.contains(QLatin1String(", QGroupBox::indicator {")))
            missing << ThemeUtil::toString(t);
        if (!sheet.contains(QLatin1String("QGroupBox::indicator:checked")))
            missing << ThemeUtil::toString(t) + QStringLiteral(" (checked)");
    }
    CHECK(missing.isEmpty(), QByteArray("a group box's switch has the checkbox's rules (") + missing.join(QLatin1String(", ")).toUtf8() + ")");

    ThemeUtil::apply(Theme::Dark);
    UiStyle::apply();
    {
        PacketMakerDialog d;
        d.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        int checkable = 0;
        for (QGroupBox *g : d.findChildren<QGroupBox *>())
            if (g->isCheckable() && g->isVisible()) ++checkable;
        CHECK(checkable >= 2, "fixture: Packet Maker has visible switched sections");
    }

#ifdef DL_HAVE_SERIAL
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    {
        MessageDispatcher disp;
        SerialManager mgr(&disp);
        SerialConsoleWindow w(&mgr);
        w.resize(1100, 720);
        w.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700,
              QByteArray("the serial terminal fits a laptop (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
                  + QByteArray::number(w.minimumSizeHint().height()) + "; was 1480 wide)");
        const QStringList loose = LayoutAudit::orphans(&w);
        CHECK(loose.isEmpty(), QByteArray("serial terminal: layout audit (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
    }
#endif
}
