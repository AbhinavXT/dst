#include "testutil.h"

#include "messagedispatcher.h"
#include "serialconsolewindow.h"
#include "serialmanager.h"
#include "settings.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>

#ifdef Q_OS_UNIX
#  include <fcntl.h>
#  include <termios.h>
#  include <unistd.h>
#  if defined(Q_OS_MACOS)
#    include <util.h>
#  else
#    include <pty.h>
#  endif
#endif

// =============================================================================
//  Session 109 — macro buttons.
//
//  A row of user-defined commands (STATUS?, DIAG?, RESET), saved per
//  profile, with a confirm step on the ones the card acts on.
// =============================================================================

namespace {

void settle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

SerialMacro mac(const QString &label, const QString &text, bool confirm = false,
                bool hex = false, const QByteArray &ending = "\r\n")
{
    SerialMacro m;
    m.label = label; m.text = text; m.confirm = confirm; m.hex = hex; m.ending = ending;
    return m;
}

}  // namespace

TEST_SUITE(session109)
{
    // ---- what a macro sends ---------------------------------------------------------
    bool ok = false;
    CHECK(mac(QStringLiteral("Status"), QStringLiteral("STATUS?")).bytes(&ok) == "STATUS?\r\n" && ok,
          "text plus its line end");
    CHECK(mac(QStringLiteral("Raw"), QStringLiteral("A\\x01B"), false, false, QByteArray()).bytes() == QByteArray("A\x01" "B"),
          "the send box's escapes, no line end");
    CHECK(mac(QStringLiteral("Poll"), QStringLiteral("AA 55 01"), false, true, QByteArray()).bytes() == QByteArray("\xAA\x55\x01", 3),
          "hex bytes");
    CHECK(mac(QStringLiteral("Bad"), QStringLiteral("AA 5"), false, true).bytes(&ok).isEmpty() && !ok,
          "hex that is not hex is refused, nothing sent");

    for (const char *w : { "RESET", "reboot now", "ERASE ALL", "FORMAT", "flash fw", "FACTORY" })
        CHECK(SerialMacro::looksDangerous(QString::fromLatin1(w)), QByteArray("dangerous: ") + w);
    for (const char *w : { "STATUS?", "DIAG?", "VERSION", "presets" })
        CHECK(!SerialMacro::looksDangerous(QString::fromLatin1(w)), QByteArray("harmless: ") + w);

    // ---- saved with a profile, line ends intact ---------------------------------------
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath(QStringLiteral("t.ini")), QSettings::IniFormat);
        SerialProfile a, b;
        a.name = QStringLiteral("IOA Input");  a.config.portName = QStringLiteral("COM5");
        b.name = QStringLiteral("IOA Output"); b.config.portName = QStringLiteral("COM6");
        a.macros = { mac(QStringLiteral("Status"), QStringLiteral("STATUS?")),
                     mac(QStringLiteral("Reset"), QStringLiteral("RESET"), true, false, "\r") };
        b.macros = { mac(QStringLiteral("Diag"), QStringLiteral("DIAG?"), false, false, "\n") };
        SerialProfile::saveAll(s, { a, b });
        const QVector<SerialProfile> back = SerialProfile::loadAll(s);
        CHECK(back.size() == 2 && back[0].macros.size() == 2 && back[1].macros.size() == 1,
              "each profile keeps its own macros");
        CHECK(back.size() == 2 && back[0].macros[1].confirm && back[0].macros[1].ending == "\r"
                  && back[1].macros[0].ending == "\n",
              "confirm and the exact line end survive the ini");
    }

#ifdef Q_OS_UNIX
    int master = -1, slave = -1;
    char name[256] = {};
    if (::openpty(&master, &slave, name, nullptr, nullptr) != 0) { CHECK(false, "openpty"); return; }
    termios t{};
    tcgetattr(master, &t);
    cfmakeraw(&t);
    tcsetattr(master, TCSANOW, &t);
    ::close(slave);
    ::fcntl(master, F_SETFL, ::fcntl(master, F_GETFL) | O_NONBLOCK);
    auto readCard = [master]() {
        settle(80);
        QByteArray got;
        char buf[512];
        ssize_t n;
        while ((n = ::read(master, buf, sizeof buf)) > 0) got.append(buf, int(n));
        return got;
    };

    QSettings real(Settings::iniPath(), QSettings::IniFormat);
    const QVector<SerialProfile> profilesWas = SerialProfile::loadAll(real);
    const QVector<SerialMacro> macrosWas = SerialMacro::loadList(real, QStringLiteral("serial/macros"));
    SerialProfile::saveAll(real, {});
    SerialMacro::saveList(real, QStringLiteral("serial/macros"), {});
    real.sync();
    {
        MessageDispatcher disp;
        SerialManager mgr(&disp);
        SerialConsoleWindow w(&mgr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        SerialConfig c;
        c.portName = QString::fromLocal8Bit(name);
        w.setConfigToUi(c);
        CHECK(w.macros().isEmpty(), "no macros to start with");
        w.setMacros({ mac(QStringLiteral("Status"), QStringLiteral("STATUS?")),
                      mac(QStringLiteral("Reset"), QStringLiteral("RESET"), true) });
        const QList<QToolButton *> buttons = w.findChildren<QToolButton *>(QStringLiteral("serialMacro"));
        CHECK(buttons.size() == 2 && buttons[1]->text().contains(QChar(0x26A0)),
              "a button each; the confirm one marked on its face");
        CHECK(!w.runMacro(0), "with the port closed nothing goes");
        CHECK(w.openPort(), "opens");

        CHECK(w.runMacro(0) && readCard() == "STATUS?\r\n", "a click sends the macro");
        int asked = 0;
        w.setMacroConfirmer([&](const SerialMacro &m) { ++asked; return m.label != QLatin1String("Reset"); });
        CHECK(!w.runMacro(1) && asked == 1 && readCard().isEmpty(), "RESET asks first; No sends nothing");
        w.setMacroConfirmer([&](const SerialMacro &) { ++asked; return true; });
        CHECK(w.runMacro(1) && asked == 2 && readCard() == "RESET\r\n", "Yes sends it");
        CHECK(w.runMacro(0) && asked == 2, "a macro without confirm never asks");
        readCard();

        // Saved per profile: switching profiles switches the row.
        CHECK(w.saveProfile(QStringLiteral("IOA Input")), "save as a profile, with the macro row");
        w.selectProfile(QString());
        CHECK(w.macros().size() == 2, "no profile: the default set (the two saved before the profile existed)");
        w.setMacros({ mac(QStringLiteral("Ver"), QStringLiteral("VER?")) });
        w.selectProfile(QStringLiteral("IOA Input"));
        CHECK(w.macros().size() == 2 && w.macros()[1].label == QLatin1String("Reset"),
              "choosing the profile brings back its own macros");
        w.selectProfile(QString());
        CHECK(w.macros().size() == 1 && w.macros()[0].label == QLatin1String("Ver"),
              "and no profile brings back the default set");
        w.closePort();
    }
    SerialProfile::saveAll(real, profilesWas);
    SerialMacro::saveList(real, QStringLiteral("serial/macros"), macrosWas);
    real.sync();
    ::close(master);
#endif
}
