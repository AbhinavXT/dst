#include "dialogfocus.h"
#include "logentry.h"
#include "textzoom.h"
#include "mainwindow.h"
#include "settings.h"
#include "theme.h"
#include "uistyle.h"
#include "uicolors.h"
#include "udpcommunication.h"

#include <QApplication>
#include <QMetaType>

int main(int argc, char *argv[])
{
    QCoreApplication::setApplicationName("DLConsole");
    QCoreApplication::setApplicationVersion("1.0");
    QCoreApplication::setOrganizationName("CRL-GAD");

    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/myapp_icon.png"));

    // Apply the saved theme BEFORE MainWindow is constructed so its
    // widgets render with the right palette from frame 0 — no flash of
    // light theme on a dark-mode startup.
    // Colour-blind-safe status colours are read before the theme is applied,
    // so nothing paints a single frame in the other set.
    UiColor::setColorBlindSafe(Settings::colorBlindSafe());
    ThemeUtil::apply(ThemeUtil::fromString(Settings::theme()));
    UiStyle::apply();   // rebuilt from the palette above
    // The saved text size (View > Text size), on top of the style above.
    TextZoom::initialise();
    // Session 283: a closed dialog gives focus back to the window that
    // opened it, not to the main window.
    DialogFocus::install();

    qRegisterMetaType<ParsedMessagePtr>("ParsedMessagePtr");
    qRegisterMetaType<LogEntryPtr>     ("LogEntryPtr");

    MainWindow w;
    w.show();
    return a.exec();
}
