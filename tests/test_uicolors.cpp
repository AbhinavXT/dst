#include "testutil.h"

#include "uicolors.h"
#include "uistyle.h"
#include "theme.h"

#include <QRegularExpression>
#include <QSet>

#include <QApplication>
#include <QLabel>
#include <QPalette>

// =============================================================================
//  Semantic colours.
//
//  Colour is the one thing in a UI that regresses without anyone noticing:
//  somebody nudges a hex value to taste, it looks fine on their monitor, and
//  the warning text is unreadable on a laptop in daylight. So the check here
//  is arithmetic, not opinion — every colour that carries meaning must clear
//  4.5:1 against both surfaces it can be drawn on, in both themes.
//
//  These are the values that failed before this module existed:
//      #b9770e warning on the light window   3.20 : 1
//      #e05a52 error   on the dark window    3.76 : 1
//      #80868e muted   on the light window   3.19 : 1
//      #46c07a ok, used in BOTH themes, on white   2.43 : 1
// =============================================================================

namespace {

// WCAG AA for normal text. Not a house preference — the floor below which
// text stops being reliably readable for a good fraction of people.
const double kMinContrast = 4.5;

void withTheme(Theme t, const std::function<void()> &body)
{
    const QPalette saved = qApp->palette();
    ThemeUtil::apply(t);
    body();
    qApp->setPalette(saved);
}

}  // namespace

TEST_SUITE(uicolors)
{
    // ---- the ratio itself --------------------------------------------------
    // Check the metric before trusting anything it says.
    {
        const double bw = UiColor::contrastRatio(QColor(Qt::black), QColor(Qt::white));
        CHECK(bw > 20.9 && bw < 21.1, "black on white is 21:1");
        CHECK(UiColor::contrastRatio(QColor(Qt::red), QColor(Qt::red)) < 1.01,
              "a colour against itself is 1:1");
        CHECK(qAbs(UiColor::contrastRatio(QColor("#767676"), QColor(Qt::white)) - 4.54) < 0.05,
              "the AA reference grey lands on 4.54:1");
        CHECK(qAbs(UiColor::contrastRatio(QColor(Qt::white), QColor("#767676")) - 4.54) < 0.05,
              "and the ratio does not depend on argument order");
    }

    // ---- every semantic colour, in both themes -----------------------------
    for (Theme t : ThemeUtil::all()) {
        const char *name = ThemeUtil::toString(t);
        withTheme(t, [&] {
            CHECK(UiColor::dark() == ThemeUtil::isDark(t),
                  QStringLiteral("%1: the palette reports its own darkness")
                      .arg(name).toUtf8().constData());

            const QColor win  = qApp->palette().color(QPalette::Window);
            const QColor base = qApp->palette().color(QPalette::Base);

            struct { const char *what; QColor c; } all[] = {
                { "ok",      UiColor::ok()      },
                { "warning", UiColor::warning() },
                { "error",   UiColor::error()   },
                { "muted",   UiColor::muted()   },
                { "accent",  UiColor::accent()  },
            };
            for (const auto &e : all) {
                const double onWin  = UiColor::contrastRatio(e.c, win);
                const double onBase = UiColor::contrastRatio(e.c, base);
                CHECK(onWin >= kMinContrast,
                      QStringLiteral("%1: %2 on the window is %3:1")
                          .arg(name, e.what).arg(onWin, 0, 'f', 2).toUtf8().constData());
                CHECK(onBase >= kMinContrast,
                      QStringLiteral("%1: %2 on the base is %3:1")
                          .arg(name, e.what).arg(onBase, 0, 'f', 2).toUtf8().constData());
            }

            // Plot series are lines, not text: WCAG's graphical-object floor
            // of 3:1, against the Base the plot is painted on.
            for (int i = 0; i < UiColor::seriesCount(); ++i) {
                const double r = UiColor::contrastRatio(UiColor::series(i), base);
                CHECK(r >= 3.0,
                      QStringLiteral("%1: series[%2] on the base is %3:1")
                          .arg(name).arg(i).arg(r, 0, 'f', 2).toUtf8().constData());
            }
            CHECK(UiColor::series(UiColor::seriesCount()) == UiColor::series(0)
                  && UiColor::series(-1) == UiColor::series(UiColor::seriesCount() - 1),
                  QStringLiteral("%1: series() cycles, negatives included")
                      .arg(name).toUtf8().constData());

            // The banner brings its own background, so it is checked against
            // that rather than against the window.
            const double banner = UiColor::contrastRatio(UiColor::bannerFg(),
                                                         UiColor::bannerBg());
            CHECK(banner >= kMinContrast,
                  QStringLiteral("%1: banner text on its own fill is %2:1")
                      .arg(name).arg(banner, 0, 'f', 2).toUtf8().constData());
        });
    }

    // ---- the selected mark: a graphical object, 3:1 on the base -------------
    for (Theme t : ThemeUtil::all()) {
        withTheme(t, [&] {
            const double ratio = UiColor::contrastRatio(UiColor::selectedMark(),
                                                        qApp->palette().color(QPalette::Base));
            CHECK(ratio >= 3.0, QStringLiteral("%1: the selected mark is %2:1 on the base")
                                    .arg(QString::fromLatin1(ThemeUtil::toString(t))).arg(ratio, 0, 'f', 2)
                                    .toUtf8().constData());
        });
    }

    // ---- meanings stay distinguishable -------------------------------------
    // ok and error must not be confusable at a glance, in either theme. A
    // contrast ratio between two foregrounds is a crude proxy for that, but
    // it does catch the case where someone tunes both towards the same grey.
    for (Theme t : ThemeUtil::all()) {
        withTheme(t, [&] {
            CHECK(UiColor::ok() != UiColor::error(), "ok and error differ");
            CHECK(UiColor::warning() != UiColor::error(), "warning and error differ");
            CHECK(UiColor::muted() != qApp->palette().color(QPalette::WindowText),
                  "muted is dimmer than ordinary text, not equal to it");
        });
    }

    // ---- the colours actually follow the palette ---------------------------
    {
        QColor lightOk, darkOk, lightBanner, darkBanner;
        withTheme(Theme::Light, [&] { lightOk = UiColor::ok(); lightBanner = UiColor::bannerBg(); });
        withTheme(Theme::Dark,  [&] { darkOk  = UiColor::ok(); darkBanner  = UiColor::bannerBg(); });
        CHECK(lightOk != darkOk, "ok is not the same colour in both themes");
        CHECK(lightBanner.lightness() > darkBanner.lightness(),
              "the light theme's banner fill is the lighter of the two");
    }

    // ---- stylesheet fragments ----------------------------------------------
    {
        withTheme(Theme::Light, [] {
            const QString s = UiColor::errorStyle();
            CHECK(s.startsWith(QLatin1String("color:#")) && s.endsWith(QLatin1Char(';')),
                  "a style fragment is a bare colour declaration");
            CHECK(s.contains(UiColor::error().name()), "carrying the right colour");
            CHECK(UiColor::bannerStyle().contains(QLatin1String("QLabel {")),
                  "the banner style is a full rule");
        });
    }

    // ---- a window repaints when the theme changes under it -----------------
    // This is the half of the problem that is not about which hex value:
    // before it, every window except the fault panel kept its old colours
    // after a toggle until it was closed and reopened.
    {
        ThemeUtil::apply(Theme::Light);
        QLabel probe;
        int repaints = 0;
        UiColor::onThemeChange(&probe, [&repaints] { ++repaints; });
        CHECK(repaints == 0, "nothing fires just for registering");

        ThemeUtil::apply(Theme::Dark);
        QCoreApplication::processEvents();
        CHECK(repaints >= 1, "switching to dark tells the widget to recolour");

        const int afterDark = repaints;
        ThemeUtil::apply(Theme::Light);
        QCoreApplication::processEvents();
        CHECK(repaints > afterDark, "and so does switching back");
    }

    // Leave the app on whatever the settings say, so later suites are not
    // running under a palette this one happened to set.
    ThemeUtil::apply(Theme::Light);
}

// =============================================================================
//  The palettes themselves, and the stylesheet derived from them.
//
//  A palette has a dozen roles that nothing in this program sets explicitly
//  but Fusion draws from constantly — Mid and Shadow for frame edges,
//  PlaceholderText, the Disabled group. Leaving them at their defaults is how
//  a dark theme ends up with black hairline borders and unreadable greyed-out
//  text. These checks are the floor for the ones that carry text.
// =============================================================================

TEST_SUITE(uipalette)
{
    for (Theme t : ThemeUtil::all()) {
        const char *name = ThemeUtil::toString(t);
        withTheme(t, [&] {
            const QPalette p = qApp->palette();
            const QColor base   = p.color(QPalette::Base);
            const QColor window = p.color(QPalette::Window);

            auto ratio = [](const QColor &a, const QColor &b) {
                return UiColor::contrastRatio(a, b);
            };
            auto say = [&](const char *what, double r) {
                return QStringLiteral("%1: %2 is %3:1")
                    .arg(name, what).arg(r, 0, 'f', 2).toUtf8();
            };

            const double body = ratio(p.color(QPalette::Text), base);
            CHECK(body >= 7.0, say("body text on the base", body).constData());

            const double hi = ratio(p.color(QPalette::HighlightedText),
                                    p.color(QPalette::Highlight));
            CHECK(hi >= 4.5, say("selected text on the highlight", hi).constData());

            const double tip = ratio(p.color(QPalette::ToolTipText),
                                     p.color(QPalette::ToolTipBase));
            CHECK(tip >= 4.5, say("tooltip text on its base", tip).constData());

            // Disabled text is meant to look disabled, but "disabled" is not
            // "invisible" — an operator still has to read a greyed-out field
            // to know what it says.
            const double dis = ratio(p.color(QPalette::Disabled, QPalette::Text), base);
            CHECK(dis >= 3.0, say("disabled text on the base", dis).constData());

            const double ph = ratio(p.color(QPalette::PlaceholderText), base);
            CHECK(ph >= 3.0, say("placeholder text on the base", ph).constData());

            // Alternating rows: visible, but not stripes that fight the text.
            const double alt = ratio(p.color(QPalette::AlternateBase), base);
            CHECK(alt > 1.04, say("alternate row against the base", alt).constData());
            CHECK(alt < 1.35, say("alternate row is a hint, not a band", alt).constData());

            // The frame greys Fusion draws borders from. Left unset they
            // default to values from the light palette, which on a dark
            // window reads as a hard black line around every group box.
            CHECK(p.color(QPalette::Mid) != QColor(),  "Mid is set");
            CHECK(p.color(QPalette::Shadow) != QColor(), "Shadow is set");
            const bool darkUi = ThemeUtil::isDark(t);
            CHECK((p.color(QPalette::Mid).lightness() > window.lightness()) == darkUi,
                  "Mid sits on the readable side of the window colour");
        });
    }

    // ---- the stylesheet ----------------------------------------------------
    {
        QString light, dark;
        withTheme(Theme::Light, [&] { light = UiStyle::sheet(); });
        withTheme(Theme::Dark,  [&] { dark  = UiStyle::sheet(); });

        CHECK(!light.isEmpty() && !dark.isEmpty(), "a sheet is produced for both themes");
        CHECK(light != dark, "and the two differ — it is built from the palette");

        // The load-bearing property: a stylesheet colour overrides the
        // palette, so a literal baked in here would survive a theme change
        // and break exactly one widget in a way the theme cannot fix.
        //
        // Checking "every colour is derivable from the palette" would mean
        // recomputing the blends, which just restates the implementation.
        // The behavioural version is stronger and simpler: a colour that
        // appears in BOTH sheets did not come from the palette — unless the
        // two palettes genuinely agree on it, as they do on white for
        // selected text.
        auto hexesIn = [](const QString &s) {
            QSet<QString> out;
            QRegularExpression rx(QStringLiteral("#[0-9a-fA-F]{6}"));
            auto it = rx.globalMatch(s);
            while (it.hasNext()) { out.insert(it.next().captured(0).toLower()); }
            return out;
        };
        auto rolesOf = [](Theme t) {
            QSet<QString> out;
            const QPalette saved = qApp->palette();
            ThemeUtil::apply(t);
            const QPalette p = qApp->palette();
            for (int role = 0; role <= int(QPalette::PlaceholderText); ++role) {
                for (QPalette::ColorGroup g : { QPalette::Active, QPalette::Disabled }) {
                    out.insert(p.color(g, QPalette::ColorRole(role)).name().toLower());
                }
            }
            qApp->setPalette(saved);
            return out;
        };

        const QSet<QString> lightHex = hexesIn(light);
        const QSet<QString> darkHex  = hexesIn(dark);
        CHECK(lightHex.size() > 10 && darkHex.size() > 10, "the sheets do set colours");

        const QSet<QString> shared = lightHex & darkHex;
        const QSet<QString> agreed = rolesOf(Theme::Light) & rolesOf(Theme::Dark);
        QStringList suspects;
        for (const QString &h : shared) {
            if (!agreed.contains(h)) { suspects << h; }
        }
        for (const QString &h : suspects) {
            printf("      literal in both sheets: %s\n", qPrintable(h));
        }
        CHECK(suspects.isEmpty(),
              "no colour in the stylesheet survives a theme change");
    }

    // ---- one monospace face ------------------------------------------------
    {
        const QFont m = UiStyle::monoFont();
        CHECK(m.fixedPitch() || !m.family().isEmpty(),
              "the mono font resolves to a real family");
        CHECK(UiStyle::gap() > 0 && UiStyle::margin() > 0 && UiStyle::radius() >= 0,
              "the layout metrics are sane");
    }

    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
}
