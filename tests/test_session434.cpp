#include "testutil.h"

#include "theme.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QApplication>
#include <QSet>

// =============================================================================
//  Session 434 — three more dark themes: One Dark, Solarized Dark, Gruvbox
//  Dark. Each lifted where its published scheme falls under 4.5 : 1 (the
//  contrast audit holds them to it on every surface); each with its own
//  ok / warning / error / muted / accent.
// =============================================================================

TEST_SUITE(session434)
{
    const QPalette saved = qApp->palette();
    const Theme added[] = { Theme::OneDark, Theme::SolarizedDark, Theme::GruvboxDark };
    const char *keys[] = { "one-dark", "solarized-dark", "gruvbox-dark" };
    const char *labels[] = { "One Dark", "Solarized Dark", "Gruvbox Dark" };
    const QColor windows[] = { QColor(0x28, 0x2C, 0x34), QColor(0x00, 0x2B, 0x36), QColor(0x28, 0x28, 0x28) };
    QSet<QString> errors;
    for (int i = 0; i < 3; ++i) {
        const Theme t = added[i];
        CHECK(ThemeUtil::all().contains(t) && ThemeUtil::isDark(t), qPrintable(QStringLiteral("%1: listed, dark").arg(QLatin1String(labels[i]))));
        CHECK(QLatin1String(ThemeUtil::toString(t)) == QLatin1String(keys[i]) && ThemeUtil::fromString(QLatin1String(keys[i])) == t
                  && ThemeUtil::label(t) == QLatin1String(labels[i]),
              qPrintable(QStringLiteral("%1: saved as \"%2\" and read back").arg(QLatin1String(labels[i]), QLatin1String(keys[i]))));
        ThemeUtil::apply(t);
        UiStyle::apply();
        CHECK(qApp->palette().color(QPalette::Window) == windows[i] && UiColor::dark(),
              qPrintable(QStringLiteral("%1: applied, its window colour, dark").arg(QLatin1String(labels[i]))));
        CHECK(UiColor::contrastRatio(qApp->palette().color(QPalette::Text), qApp->palette().color(QPalette::Button)) >= 4.5
                  && UiColor::contrastRatio(UiColor::error(), qApp->palette().color(QPalette::Button)) >= 4.5,
              qPrintable(QStringLiteral("%1: text and error on a button, 4.5 : 1 or more").arg(QLatin1String(labels[i]))));
        errors.insert(UiColor::error().name());
    }
    CHECK(errors.size() == 3 && !errors.contains(QStringLiteral("#f38ba8")), "each its own error colour (not Mocha's)");
    // The dark ones after the light ones, as the Theme menu lists them.
    const QVector<Theme> all = ThemeUtil::all();
    CHECK(all.indexOf(Theme::OneDark) > all.indexOf(Theme::Mocha) && all.last() == Theme::GruvboxDark, "listed after Mocha");
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    qApp->setPalette(saved);
}
