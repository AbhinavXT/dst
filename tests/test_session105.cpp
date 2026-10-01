#include "testutil.h"

#include "messagedispatcher.h"
#include "serialconsolewindow.h"
#include "serialmanager.h"
#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#ifdef Q_OS_UNIX
#  include <termios.h>
#  include <unistd.h>
#  if defined(Q_OS_MACOS)
#    include <util.h>
#  else
#    include <pty.h>
#  endif
#endif

// =============================================================================
//  Session 105 — named serial port profiles.
//
//  "IOA Input = COM5 115200 8N1, feed on, auto-open." Opening one sets
//  everything; Open all starts every IOA card in one click; a port opened
//  from a profile carries its name on the chip and the console tab.
//
//  Real @dop1 line: replay/loco_2_1_29062026_134128.cap.
// =============================================================================

namespace {

const QByteArray kDop1 =
    "@dop1_2_1 2026-06-29T13:41:29 14034 0C 17 17 17 17 0C 0C 0C 0C 17 17 17 17 17 17 17";

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

SerialProfile prof(const QString &name, const QString &port, qint32 baud, bool feed, bool autoOpen)
{
    SerialProfile p;
    p.name = name;
    p.config.portName = port;
    p.config.baud = baud;
    p.feed = feed;
    p.autoOpen = autoOpen;
    return p;
}

#ifdef Q_OS_UNIX
struct Pty {
    int master = -1;
    QString path;
    Pty()
    {
        int slave = -1;
        char name[256] = {};
        if (::openpty(&master, &slave, name, nullptr, nullptr) != 0) return;
        termios t{};
        tcgetattr(master, &t);
        cfmakeraw(&t);
        tcsetattr(master, TCSANOW, &t);
        ::close(slave);
        path = QString::fromLocal8Bit(name);
    }
    ~Pty() { if (master >= 0) ::close(master); }
    bool ok() const { return master >= 0; }
};
#endif

}  // namespace

TEST_SUITE(session105)
{
    // ---- the saved form --------------------------------------------------------
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath(QStringLiteral("t.ini")), QSettings::IniFormat);
        QVector<SerialProfile> all;
        SerialProfile::upsert(&all, prof(QStringLiteral("IOA Input"), QStringLiteral("COM5"), 115200, true, true));
        SerialProfile::upsert(&all, prof(QStringLiteral("IOA Output"), QStringLiteral("COM6"), 115200, true, true));
        SerialProfile::upsert(&all, prof(QStringLiteral("IOA Analog"), QStringLiteral("COM7"), 57600, false, false));
        SerialProfile::saveAll(s, all);
        const QVector<SerialProfile> back = SerialProfile::loadAll(s);
        CHECK(back.size() == 3, "three profiles saved and read back");
        CHECK(back.size() == 3 && back[0].name == QLatin1String("IOA Input")
                  && back[0].config.portName == QLatin1String("COM5")
                  && back[0].feed && back[0].autoOpen,
              "name, port, feed and auto-open survive");
        CHECK(back.size() == 3 && back[2].config.baud == 57600 && !back[2].feed && !back[2].autoOpen,
              "each keeps its own settings");

        SerialProfile::upsert(&all, prof(QStringLiteral("ioa input"), QStringLiteral("COM9"), 9600, true, false));
        CHECK(all.size() == 3 && all[0].config.portName == QLatin1String("COM9"),
              "saving under an existing name (any case) replaces it");
        CHECK(SerialProfile::remove(&all, QStringLiteral("IOA OUTPUT")) && all.size() == 2,
              "delete by name, any case");
        CHECK(!SerialProfile::remove(&all, QStringLiteral("nope")), "deleting a missing one says so");
        SerialProfile::saveAll(s, all);
        CHECK(SerialProfile::loadAll(s).size() == 2, "a shorter list leaves no stale entries behind");

        s.beginWriteArray(QStringLiteral("serial/profiles"), 1);
        s.setArrayIndex(0);
        s.setValue(QStringLiteral("name"), QString());
        s.endArray();
        CHECK(SerialProfile::loadAll(s).isEmpty(), "a profile with no name or port is not loaded");
    }

#ifdef Q_OS_UNIX
    // ---- Open all: three IOA cards in one call -------------------------------
    {
        Pty in, out, an;
        if (in.ok() && out.ok() && an.ok()) {
            MessageDispatcher disp;
            QSignalSpy tabs(&disp, &MessageDispatcher::tabRequested);
            SerialManager mgr(&disp);
            const QVector<SerialProfile> three = {
                prof(QStringLiteral("IOA Input"), in.path, 115200, true, true),
                prof(QStringLiteral("IOA Output"), out.path, 57600, true, true),
                prof(QStringLiteral("IOA Analog"), an.path, 9600, false, false),
            };
            CHECK(mgr.openProfiles(three).isEmpty() && mgr.openPorts().size() == 3,
                  "Open all opens every profile's port");
            CHECK(mgr.link(out.path)->config().baud == 57600 && mgr.link(an.path)->config().baud == 9600,
                  "each with its own settings");
            CHECK(mgr.label(in.path) == QLatin1String("IOA Input") && mgr.titleFor(in.path) == QLatin1String("IOA Input"),
                  "a profile's port carries its name");
            CHECK(mgr.feeds(in.path) && !mgr.feeds(an.path), "and its Feed console setting");

            const QByteArray b = kDop1 + "\r\n";
            (void)::write(in.master, b.constData(), size_t(b.size()));
            (void)::write(an.master, b.constData(), size_t(b.size()));
            settle(250);
            disp.drainNow();
            CHECK(tabs.count() == 1 && tabs.at(0).at(1).toString() == QLatin1String("IOA Input"),
                  "the console tab is titled with the profile name; the unfed one makes no tab");

            int opens = 0;
            for (const QString &p : mgr.openPorts())
                QObject::connect(mgr.link(p), &SerialLink::opened, [&]() { ++opens; });
            CHECK(mgr.openProfiles(three).isEmpty() && opens == 0,
                  "Open all again does not reopen (and so drop) running ports");

            mgr.closeAll();
            QVector<SerialProfile> withMissing = three;
            withMissing << prof(QStringLiteral("IOA Spare"), QStringLiteral("/dev/no-such-port-105"), 115200, true, true);
            const QStringList failed = mgr.openProfiles(withMissing);
            CHECK(failed.size() == 1 && failed[0].startsWith(QLatin1String("IOA Spare (/dev/no-such-port-105)")),
                  QByteArray("a missing adapter is reported by profile name and port: ") + failed.join(' ').toUtf8());
            CHECK(mgr.openPorts().size() == 3, "…and the others still open");
            mgr.closeAll();
        }
    }

    // ---- the terminal: save, select, open with its name, delete --------------
    {
        Pty card;
        if (card.ok()) {
            QSettings real(Settings::iniPath(), QSettings::IniFormat);
            const QVector<SerialProfile> was = SerialProfile::loadAll(real);
            SerialProfile::saveAll(real, {});
            real.sync();

            MessageDispatcher disp;
            SerialManager mgr(&disp);
            SerialConsoleWindow w(&mgr);
            w.setAttribute(Qt::WA_DeleteOnClose, false);
            SerialConfig c;
            c.portName = card.path;
            c.baud = 38400;
            w.setConfigToUi(c);
            w.findChild<QCheckBox *>(QStringLiteral("serialAutoOpen"))->setChecked(true);
            CHECK(w.saveProfile(QStringLiteral("IOA Input")) && w.currentProfile() == QLatin1String("IOA Input"),
                  "Save as profile stores the window's settings and selects it");
            QSettings check(Settings::iniPath(), QSettings::IniFormat);
            const QVector<SerialProfile> saved = SerialProfile::loadAll(check);
            CHECK(saved.size() == 1 && saved[0].config.baud == 38400 && saved[0].autoOpen,
                  "…with its baud and Open at start");

            // Change the window, then choose the profile: it comes back.
            c.baud = 9600;
            w.setConfigToUi(c);
            w.selectProfile(QStringLiteral("IOA Input"));
            CHECK(w.configFromUi().baud == 38400, "choosing the profile restores its settings");
            CHECK(w.openPort() && mgr.label(card.path) == QLatin1String("IOA Input"),
                  "opening with a profile chosen labels the port with its name");
            CHECK(w.statusText().contains(QLatin1String("IOA Input")), "the status names the tab it feeds");
            w.closePort();

            CHECK(w.deleteProfile(QStringLiteral("IOA Input")) && w.currentProfile().isEmpty(),
                  "Delete removes it");
            QSettings after(Settings::iniPath(), QSettings::IniFormat);
            CHECK(SerialProfile::loadAll(after).isEmpty(), "…from the settings too");
            CHECK(w.openPort() && mgr.label(card.path).isEmpty(),
                  "opened with no profile chosen: no label");
            w.closePort();

            SerialProfile::saveAll(real, was);
            real.sync();
        }
    }
#endif
}
