#include "testutil.h"

#include "messagedispatcher.h"
#include "serialmanager.h"
#include "settingsbundle.h"

#include <QSettings>
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
//  Session 115 — serial settings that survive a renumber and a new laptop.
//
//  8. Per-port settings were keyed by COM name alone, so when Windows gave
//     the same adapter a new number they were lost; profiles likewise
//     pointed at a name. Settings are now also kept under the adapter's USB
//     serial number, and a profile remembers its adapter and opens it under
//     whatever name it has now.
//  9. Serial profiles, macros and port settings were not in File ▸ Export
//     settings; now they are a section of their own.
// =============================================================================

TEST_SUITE(session115)
{
    // ---- 8a. settings follow the adapter, not the COM number ----------------------
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath(QStringLiteral("t.ini")), QSettings::IniFormat);
        SerialConfig c;
        c.portName = QStringLiteral("COM5");
        c.baud = 57600;
        SerialManager::savePortSettings(s, c, false, QStringLiteral("FT1234AB"));
        SerialConfig got;
        bool feed = true;
        CHECK(SerialManager::loadPortSettings(s, QStringLiteral("COM7"), &got, &feed, QStringLiteral("FT1234AB"))
                  && got.baud == 57600 && !feed && got.portName == QLatin1String("COM7"),
              "the same adapter as COM7 gets the settings it had as COM5, under its new name");
        CHECK(!SerialManager::loadPortSettings(s, QStringLiteral("COM7"), &got, &feed, QStringLiteral("OTHER999")),
              "a different adapter on COM7 does not inherit them");
        CHECK(SerialManager::loadPortSettings(s, QStringLiteral("COM5"), &got, &feed, QString()) && got.baud == 57600,
              "with no serial number (a virtual port), by name as before");
        c.baud = 9600;
        SerialManager::savePortSettings(s, c, true, QString());
        CHECK(SerialManager::loadPortSettings(s, QStringLiteral("COM5"), &got, &feed, QStringLiteral("FT1234AB"))
                  && got.baud == 57600,
              "the adapter's own copy wins over the COM name's when the adapter is there");
    }

    // ---- 8b. a profile finds its adapter after a renumber -----------------------------
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath(QStringLiteral("t.ini")), QSettings::IniFormat);
        SerialProfile p;
        p.name = QStringLiteral("IOA Input");
        p.config.portName = QStringLiteral("COM5");
        p.usbSerial = QStringLiteral("FT1234AB");
        SerialProfile::saveAll(s, { p });
        CHECK(SerialProfile::loadAll(s).value(0).usbSerial == QLatin1String("FT1234AB"), "a profile keeps its adapter");
    }
#ifdef Q_OS_UNIX
    {
        int master = -1, slave = -1;
        char name[256] = {};
        if (::openpty(&master, &slave, name, nullptr, nullptr) == 0) {
            ::close(slave);
            const QString nowAt = QString::fromLocal8Bit(name);
            MessageDispatcher disp;
            SerialManager mgr(&disp);
            // The stand-in enumerator: adapter FT1234AB is at the pty now.
            mgr.setPortFinder([&](const QString &sn, const QString &) {
                return sn == QLatin1String("FT1234AB") ? nowAt : QString();
            });
            SerialProfile p;
            p.name = QStringLiteral("IOA Input");
            p.config.portName = QStringLiteral("COM5");          // where it was saved
            p.usbSerial = QStringLiteral("FT1234AB");
            CHECK(mgr.resolveProfilePort(p) == nowAt, "the profile resolves to where its adapter is now");
            CHECK(mgr.openProfiles({ p }).isEmpty() && mgr.openPorts() == QStringList{ nowAt }
                      && mgr.label(nowAt) == QLatin1String("IOA Input"),
                  "Open all opens it there, with its name, though it was saved as COM5");
            SerialProfile plain = p;
            plain.usbSerial.clear();
            CHECK(mgr.resolveProfilePort(plain) == QLatin1String("COM5"), "a profile without an adapter keeps its name");
            mgr.closeAll();
            ::close(master);
        }
    }
#endif

    // ---- 9. serial settings in the settings bundle -------------------------------------
    {
        QTemporaryDir from, to;
        const QString iniA = from.filePath(QStringLiteral("a.ini"));
        const QString iniB = to.filePath(QStringLiteral("b.ini"));
        {
            QSettings s(iniA, QSettings::IniFormat);
            SerialProfile p;
            p.name = QStringLiteral("IOA Output");
            p.config.portName = QStringLiteral("COM6");
            p.config.baud = 57600;
            p.autoOpen = true;
            SerialMacro m;
            m.label = QStringLiteral("Reset"); m.text = QStringLiteral("RESET"); m.confirm = true; m.ending = "\r";
            p.macros = { m };
            SerialProfile::saveAll(s, { p });
            SerialMacro d;
            d.label = QStringLiteral("Status"); d.text = QStringLiteral("STATUS?");
            SerialMacro::saveList(s, QStringLiteral("serial/macros"), { d });
            SerialConfig c; c.portName = QStringLiteral("COM6"); c.baud = 57600;
            SerialManager::savePortSettings(s, c, true, QStringLiteral("FT1234AB"));
            s.setValue(QStringLiteral("udp/port"), 50002);          // not serial's to carry
        }
        using SettingsBundle::Section;
        CHECK(SettingsBundle::allSections().contains(Section::Serial)
                  && SettingsBundle::id(Section::Serial) == QLatin1String("serial"),
              "a Serial section in File > Export settings");
        const SettingsBundle::Bundle b = SettingsBundle::capture({ Section::Serial }, from.path(), iniA);
        QString err;
        CHECK(SettingsBundle::apply(b, { Section::Serial }, to.path(), iniB, &err), QByteArray("imports: ") + err.toUtf8());
        QSettings s(iniB, QSettings::IniFormat);
        const QVector<SerialProfile> profiles = SerialProfile::loadAll(s);
        CHECK(profiles.size() == 1 && profiles[0].name == QLatin1String("IOA Output")
                  && profiles[0].config.baud == 57600 && profiles[0].autoOpen,
              "the profile arrives on the other machine");
        CHECK(profiles.size() == 1 && profiles[0].macros.size() == 1 && profiles[0].macros[0].confirm
                  && profiles[0].macros[0].ending == "\r",
              "with its macros, confirm and the exact line end intact");
        CHECK(SerialMacro::loadList(s, QStringLiteral("serial/macros")).size() == 1, "the default macro row too");
        SerialConfig got;
        bool feed = false;
        CHECK(SerialManager::loadPortSettings(s, QStringLiteral("COM9"), &got, &feed, QStringLiteral("FT1234AB"))
                  && got.baud == 57600,
              "and the adapter's settings");
        CHECK(!s.contains(QStringLiteral("udp/port")), "nothing outside serial/ is carried");
    }
}
