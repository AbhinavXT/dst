#include "testutil.h"

#include "brakebenchwindow.h"
#include "settings.h"
#include "statusline.h"
#include "theme.h"

#include <QSettings>

// =============================================================================
//  Session 390 — the Brake Test Bench listens on UDP 51235, not the log port.
//  On 51234 it shared a port with the live log capture: the OS gave each
//  datagram to one socket, so the capture counted the loco's BIU heartbeats
//  (4 bytes) as malformed and the bench never saw the loco.
// =============================================================================

TEST_SUITE(session390)
{
    {
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        s.remove(QStringLiteral("brakebench"));
    }
    ThemeUtil::apply(Theme::Light);

    BrakeBenchWindow window(nullptr);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(window.listenPort() == 51235, "the bench opens on UDP 51235, the firmware's BIU_SIM_PORT");
    CHECK(window.listenPort() != Settings::udpPort(), "not the log port");

    window.setListenPort(Settings::udpPort());
    CHECK(!window.startBiu() && !window.biuRunning(), "Start BIU on the log port is refused");
    StatusLine *status = window.findChild<StatusLine *>();
    CHECK(status && status->message().contains(QStringLiteral("log port"))
              && status->message().contains(QStringLiteral("51235")),
          "the status line says it is the log port and names 51235");

    window.setListenPort(51235);
    {
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        s.remove(QStringLiteral("brakebench"));
    }
}
