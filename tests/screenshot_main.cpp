// Throwaway: render the main window to PNG in every theme so the look can be
// looked at. Not part of the suite.
#include <QApplication>
#include <QPixmap>
#include <QTimer>
#include "mainwindow.h"
#include "theme.h"
#include "uistyle.h"
#include "settings.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    for (Theme t : ThemeUtil::all()) {
        Settings::setTheme(ThemeUtil::toString(t));
        ThemeUtil::apply(t);
        UiStyle::apply();
        MainWindow w;
        w.resize(1280, 800);
        w.show();
        for (int i = 0; i < 40; ++i) { QCoreApplication::processEvents(); }
        w.grab().save(QString("/tmp/shot_%1.png").arg(ThemeUtil::toString(t)));
    }
    return 0;
}
