#include "testutil.h"
#include <QGuiApplication>

#include "theme.h"
#include "uicolors.h"

#ifdef DL_HAVE_SERIAL
#include "cabio.h"
#include "cabiosimwindow.h"
#endif

#include <QApplication>

// =============================================================================
//  Session 328 — the Cab I/O Simulator in every theme.
//
//  Abhinav: "The new windows should work with all themes". Most of what the
//  window draws is a palette role or a UiColor, which test_contrastaudit
//  already holds in every theme. What it mixes itself is checked here, in
//  each theme (eleven then; fourteen since session 434), with the colour-blind-safe colours off and on:
//    - a tile's text and icons on its own fill (plain, On = tinted with the
//      accent, Fault = tinted with the warning colour): 4.5 : 1, 7 : 1 in
//      High Contrast;
//    - a lit lamp's rim on the card, for every lamp colour: 3 : 1 (a
//      graphical object), 7 : 1 in High Contrast. Without it a yellow or
//      white lamp on a light theme is a faint disc;
//    - the window follows a theme change while open: its stylesheet carries
//      the new theme's Highlight (session 394: the filled buttons
//      are the theme's own colour, not the shared accent).
// =============================================================================

TEST_SUITE(session328)
{
#ifdef DL_HAVE_SERIAL
    using namespace CabIoColors;
    const QPalette saved = qApp->palette();
    const int savedTheme = UiColor::activeTheme();

    int themes = 0, inkShort = 0, rimShort = 0;
    QStringList shortWhere;
    for (bool safe : { false, true }) {
        UiColor::setColorBlindSafe(safe);
        for (Theme t : ThemeUtil::all()) {
            ThemeUtil::apply(t);
            ++themes;
            const QString where = QString::fromLatin1(ThemeUtil::toString(t)) + (safe ? QStringLiteral(" (safe)") : QString());
            const QColor text = qApp->palette().color(QPalette::Text);
            for (Tile tile : { Tile::Plain, Tile::On, Tile::Fault }) {
                QVector<QColor> inks{ text, UiColor::muted() };
                if (tile == Tile::Fault) inks << UiColor::warning();
                for (const QColor &ink : inks) {
                    if (UiColor::contrastRatio(tileInk(ink, tile), tileFill(tile)) + 1e-9 < textFloor()) {
                        ++inkShort;
                        shortWhere << where;
                    }
                }
            }
            const QColor base = qApp->palette().color(QPalette::Base);
            for (const char *lamp : { "red", "yellow", "green", "white" }) {
                if (UiColor::contrastRatio(lampRim(QLatin1String(lamp)), base) + 1e-9 < graphicFloor()) {
                    ++rimShort;
                    shortWhere << where + QLatin1Char(' ') + QLatin1String(lamp);
                }
            }
        }
    }
    UiColor::setColorBlindSafe(false);
    CHECK(themes == 2 * ThemeUtil::all().size(), "fixture: every theme (14 since session 434), colour-blind-safe off and on");
    CHECK(inkShort == 0, qPrintable(QStringLiteral("tile text and icons reach 4.5 : 1 (7 : 1 High Contrast) on their own fill "
                                                   "in every theme; short in: %1").arg(shortWhere.join(QStringLiteral(", ")))));
    CHECK(rimShort == 0, qPrintable(QStringLiteral("a lit lamp's rim reaches 3 : 1 (7 : 1 High Contrast) on the card for every "
                                                   "lamp colour in every theme; short in: %1").arg(shortWhere.join(QStringLiteral(", ")))));

    // ---- a theme change reaches an open window ---------------------------------------------
    {
        ThemeUtil::apply(Theme::Light);
        CabIoSimWindow w(nullptr);
        w.setWriteSink([](const QByteArray &) {});
        const QString lightFill = QGuiApplication::palette().color(QPalette::Highlight).name();
        CHECK(w.styleSheet().contains(lightFill), "built in Light: its stylesheet carries Light's Highlight");
        ThemeUtil::apply(Theme::Mocha);
        QApplication::processEvents();
        const QString mochaFill = QGuiApplication::palette().color(QPalette::Highlight).name();
        CHECK(mochaFill != lightFill && w.styleSheet().contains(mochaFill),
              "switched to Mocha while open: restyled with Mocha's Highlight");
        ThemeUtil::apply(Theme::HighContrast);
        QApplication::processEvents();
        CHECK(w.styleSheet().contains(QGuiApplication::palette().color(QPalette::Highlight).name()), "and to High Contrast");
        w.close();
    }

    if (savedTheme >= 0) ThemeUtil::apply(Theme(savedTheme));
    qApp->setPalette(saved);
#endif
}
