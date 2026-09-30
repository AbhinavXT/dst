#include "testutil.h"

#include "theme.h"
#include "uicolors.h"

#include <QApplication>
#include <QPalette>

#include <cmath>

// =============================================================================
//  Colour-blind-safe status colours (session 79).
//
//  "Distinguishable" is measured, not judged by eye: each colour is put
//  through the Machado, Oliveira & Fernandes (2009) full-severity simulation
//  of protanopia, deuteranopia and tritanopia, and the distance between two
//  colours is CIELAB ΔE*76. About 2 is the smallest difference anyone
//  notices; 20 is two colours nobody would call the same.
//
//  What is held, in every theme:
//    - ok, warning and error at least 20 apart, with normal vision and under
//      each of the three simulations;
//    - accent at least 12 from each of them, so "emphasis" is not read as a
//      verdict;
//    - every one still clears the theme's contrast floor (4.5 : 1, High
//      Contrast 7 : 1) on window, base, alternate row and button.
//  And, so the mode is shown to be needed rather than assumed: with it off,
//  some theme's ok and error collapse to within 10 under deuteranopia.
// =============================================================================

namespace {

double lin(int v)
{
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

int unlin(double v)
{
    v = qBound(0.0, v, 1.0);
    const double c = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
    return qBound(0, int(std::lround(c * 255.0)), 255);
}

enum class Vision { Normal, Protan, Deutan, Tritan };

QColor simulate(const QColor &c, Vision v)
{
    static const double prot[3][3] = { {  0.152286, 1.052583, -0.204868 },
                                       {  0.114503, 0.786281,  0.099216 },
                                       { -0.003882,-0.048116,  1.051998 } };
    static const double deut[3][3] = { {  0.367322, 0.860646, -0.227968 },
                                       {  0.280085, 0.672501,  0.047413 },
                                       { -0.011820, 0.042940,  0.968881 } };
    static const double trit[3][3] = { {  1.255528,-0.076749, -0.178779 },
                                       { -0.078411, 0.930809,  0.147602 },
                                       {  0.004733, 0.691367,  0.303900 } };
    const double (*m)[3] = nullptr;
    switch (v) {
    case Vision::Normal: return c;
    case Vision::Protan: m = prot; break;
    case Vision::Deutan: m = deut; break;
    case Vision::Tritan: m = trit; break;
    }
    const double in[3] = { lin(c.red()), lin(c.green()), lin(c.blue()) };
    int out[3];
    for (int i = 0; i < 3; ++i) {
        out[i] = unlin(m[i][0] * in[0] + m[i][1] * in[1] + m[i][2] * in[2]);
    }
    return QColor(out[0], out[1], out[2]);
}

void toLab(const QColor &c, double lab[3])
{
    const double r = lin(c.red()), g = lin(c.green()), b = lin(c.blue());
    const double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
    const double y =  0.2126 * r + 0.7152 * g + 0.0722 * b;
    const double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    lab[0] = 116.0 * f(y) - 16.0;
    lab[1] = 500.0 * (f(x) - f(y));
    lab[2] = 200.0 * (f(y) - f(z));
}

double deltaE(const QColor &a, const QColor &b)
{
    double la[3], lb[3];
    toLab(a, la);
    toLab(b, lb);
    return std::sqrt((la[0] - lb[0]) * (la[0] - lb[0]) + (la[1] - lb[1]) * (la[1] - lb[1])
                     + (la[2] - lb[2]) * (la[2] - lb[2]));
}

// Smallest distance between two colours over the four kinds of vision.
double worstDelta(const QColor &a, const QColor &b)
{
    double worst = 1e9;
    for (Vision v : { Vision::Normal, Vision::Protan, Vision::Deutan, Vision::Tritan }) {
        worst = qMin(worst, deltaE(simulate(a, v), simulate(b, v)));
    }
    return worst;
}

QByteArray say(const QString &text) { return text.toUtf8(); }

}  // namespace

TEST_SUITE(colorblind)
{
    const QPalette savedPalette = qApp->palette();
    const int      savedTheme   = UiColor::activeTheme();
    const bool     savedMode    = UiColor::colorBlindSafe();

    // ---- the simulation itself -------------------------------------------
    {
        // Pure red and pure green are the textbook deuteranope confusion.
        const double normal = deltaE(QColor(0xCC, 0x33, 0x33), QColor(0x33, 0x99, 0x33));
        const double deut   = deltaE(simulate(QColor(0xCC, 0x33, 0x33), Vision::Deutan),
                                     simulate(QColor(0x33, 0x99, 0x33), Vision::Deutan));
        CHECK(normal > 60.0, "red and green are far apart with normal vision");
        CHECK(deut < normal / 2.0, "and much closer under simulated deuteranopia");
        CHECK(simulate(QColor(0x80, 0x80, 0x80), Vision::Protan) == QColor(0x80, 0x80, 0x80)
                  || deltaE(simulate(QColor(0x80, 0x80, 0x80), Vision::Protan), QColor(0x80, 0x80, 0x80)) < 1.0,
              "a neutral grey is unchanged by the simulation");
    }

    // ---- off: the problem this mode exists for ---------------------------
    {
        UiColor::setColorBlindSafe(false);
        double worstOkError = 1e9;
        for (Theme t : ThemeUtil::all()) {
            ThemeUtil::apply(t);
            worstOkError = qMin(worstOkError, deltaE(simulate(UiColor::ok(), Vision::Deutan),
                                                     simulate(UiColor::error(), Vision::Deutan)));
        }
        CHECK(worstOkError < 10.0,
              say(QStringLiteral("without the mode, some theme's ok and error are %1 apart under "
                                 "deuteranopia (why the mode exists)").arg(worstOkError, 0, 'f', 1)));
    }

    // ---- on: every theme -------------------------------------------------
    UiColor::setColorBlindSafe(true);
    CHECK(UiColor::colorBlindSafe(), "the mode reports itself on");
    for (Theme t : ThemeUtil::all()) {
        ThemeUtil::apply(t);
        const QString name = QString::fromLatin1(ThemeUtil::toString(t));
        const QPalette p = qApp->palette();
        const double floor = (t == Theme::HighContrast) ? 7.0 : 4.5;

        const QColor ok = UiColor::ok(), warn = UiColor::warning(), err = UiColor::error();
        const QColor accent = UiColor::accent();

        const struct { const char *what; QColor a, b; double atLeast; } pairs[] = {
            { "ok / error",       ok,     err,  20.0 },
            { "ok / warning",     ok,     warn, 20.0 },
            { "warning / error",  warn,   err,  20.0 },
            { "accent / ok",      accent, ok,   12.0 },
            { "accent / warning", accent, warn, 12.0 },
            { "accent / error",   accent, err,  12.0 },
        };
        for (const auto &pr : pairs) {
            const double d = worstDelta(pr.a, pr.b);
            CHECK(d >= pr.atLeast,
                  say(QStringLiteral("%1: %2 at least %3 apart in every kind of vision (worst %4)")
                          .arg(name, QLatin1String(pr.what)).arg(pr.atLeast).arg(d, 0, 'f', 1)));
        }

        double worstContrast = 1e9;
        for (const QColor &c : { ok, warn, err, accent }) {
            for (QPalette::ColorRole role : { QPalette::Window, QPalette::Base,
                                              QPalette::AlternateBase, QPalette::Button }) {
                worstContrast = qMin(worstContrast, UiColor::contrastRatio(c, p.color(role)));
            }
        }
        CHECK(worstContrast >= floor,
              say(QStringLiteral("%1: every status colour still clears %2 : 1 on every surface (worst %3)")
                      .arg(name).arg(floor).arg(worstContrast, 0, 'f', 2)));

        CHECK(ok.blue() > ok.green() && ok.blue() > ok.red(),
              say(name + QStringLiteral(": ok is on the blue side, not green")));
        CHECK(UiColor::okStyle() == UiColor::style(ok),
              say(name + QStringLiteral(": the stylesheet helpers follow the mode")));
    }

    // ---- switching back ----------------------------------------------------
    ThemeUtil::apply(Theme::Light);
    const QColor safeOk = UiColor::ok();
    UiColor::setColorBlindSafe(false);
    CHECK(UiColor::ok() != safeOk, "turning the mode off gives the theme's own ok back");

    UiColor::setColorBlindSafe(savedMode);
    qApp->setPalette(savedPalette);
    UiColor::setActiveTheme(savedTheme);
}
