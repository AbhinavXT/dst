#include "testutil.h"
#include "brakebenchwindow.h"
#include "locosimwindow.h"
#include "settings.h"
#include "simwidgets.h"
#include "theme.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QGuiApplication>
#include <QPushButton>
#include <QSettings>

// =============================================================================
//  Session 394 — the simulators fill in the theme's own colour. Their filled
//  buttons, lit chips and tiles, and dial arcs were the shared accent, which is
//  sea-blue in Sage (a green theme); now the palette's Highlight, as the rest
//  of DLConsole selects in. Held to the floors in every theme.
// =============================================================================

TEST_SUITE(session394)
{
    const int savedTheme = UiColor::activeTheme();
    QStringList shortFill, shortMark;
    for (Theme theme : ThemeUtil::all()) {
        ThemeUtil::apply(theme);
        const QColor base = QGuiApplication::palette().color(QPalette::Base);
        if (UiColor::contrastRatio(SimUi::fillText(), SimUi::fill()) < CabIoColors::textFloor())
            shortFill << ThemeUtil::label(theme);
        if (UiColor::contrastRatio(SimUi::mark(), base) < CabIoColors::graphicFloor() - 0.01)
            shortMark << ThemeUtil::label(theme);
    }
    CHECK(shortFill.isEmpty(), QStringLiteral("a filled button's text on its fill clears the text floor "
                                                         "in every theme; short in: %1").arg(shortFill.join(QStringLiteral(", "))).toUtf8());
    CHECK(shortMark.isEmpty(), QStringLiteral("a dial's arc on the card clears the graphical floor in every "
                                                         "theme; short in: %1").arg(shortMark.join(QStringLiteral(", "))).toUtf8());

    // ---- Sage: green, not the accent's sea-blue ----------------------------------------------
    {
        ThemeUtil::apply(Theme::Sage);
        UiStyle::apply();
        const QString green = QGuiApplication::palette().color(QPalette::Highlight).name();
        const QString blue = UiColor::accent().name();
        CHECK(green != blue, "Sage's Highlight is not its accent");

        {
            QSettings settings(Settings::iniPath(), QSettings::IniFormat);
            settings.remove(QStringLiteral("locosim"));
        }
        LocoSimWindow loco(nullptr);
        loco.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(loco.styleSheet().contains(QStringLiteral("QPushButton#primary { background: %1;").arg(green)),
              "Loco Simulator: Start is Sage's green");
        BrakeBenchWindow bench(nullptr);
        bench.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(bench.styleSheet().contains(QStringLiteral("QPushButton#primary { background: %1;").arg(green)),
              "Brake Test Bench: Start BIU is Sage's green");
        CHECK(!loco.styleSheet().contains(QStringLiteral("background: %1").arg(blue))
                  && !bench.styleSheet().contains(QStringLiteral("background: %1").arg(blue)),
              "and nothing in either is filled with the sea-blue accent");
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }

    if (savedTheme >= 0) ThemeUtil::apply(Theme(savedTheme));
    UiStyle::apply();
}
