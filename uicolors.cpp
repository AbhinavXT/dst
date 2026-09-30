#include "uicolors.h"

#include <QApplication>
#include <QEvent>
#include <QPalette>
#include <QPointer>
#include <QVector>

#include <cmath>

namespace UiColor {
namespace {

// Every value below clears 4.5:1 against BOTH the window and the base of
// the palette it belongs to — checked in the uicolors test suite, not by
// eye. Where an existing colour already cleared it, it was kept; the ones
// that did not were darkened (light theme) or lightened (dark) until they
// did, which is why these are not quite the old literals.
struct Set {
    QColor ok, warning, error, muted, accent;
    QColor bannerBg, bannerFg, bannerBorder;
};

const Set &lightSet()
{
    static const Set s{
        // Ayu Light's hues, darkened until each clears the floor. The
        // published values are for syntax on a near-white editor background
        // and none of them survive as UI text: the string green is 2.4:1,
        // the accent orange 1.9:1. Keeping the hue and taking the lightness
        // down is what makes this read as the same scheme as the dark theme
        // rather than an unrelated set of primaries.
        // Ratios are the WORST case across every surface each colour can be
        // drawn on — window, base, button and the alternating row. Judging
        // them against the base alone passed values that failed on a striped
        // row, which is where half the text in this program sits.
        QColor(0x4F, 0x6B, 0x00),   // ok       4.98 : 1   ayu string green
        QColor(0x8F, 0x52, 0x00),   // warning  5.07 : 1   ayu keyword orange
        QColor(0xB5, 0x34, 0x34),   // error    4.88 : 1   ayu error red
        QColor(0x5F, 0x65, 0x6C),   // muted    4.90 : 1
        QColor(0x17, 0x66, 0x99),   // accent   5.04 : 1   ayu entity blue
        QColor(0xFF, 0xF3, 0xD9), QColor(0x6B, 0x4A, 0x00), QColor(0xE8, 0xCE, 0x9A)
    };
    return s;
}

const Set &darkSet()
{
    static const Set s{
        // Ayu Dark's own accents, against its #0F131A window. The near-black
        // background buys a lot of headroom — everything here clears the
        // floor by 4:1 or more, where the old dark set only just made it.
        QColor(0xAA, 0xD9, 0x4C),   // ok       11.29 : 1   ayu string green
        QColor(0xFF, 0xB4, 0x54),   // warning  10.55 : 1   ayu func orange
        QColor(0xE0, 0x60, 0x60),   // error     4.84 : 1   ayu error red, lifted
        QColor(0x8A, 0x91, 0x99),   // muted     5.84 : 1
        QColor(0x39, 0xBA, 0xE6),   // accent    8.28 : 1   ayu tag blue
        QColor(0x2A, 0x21, 0x13), QColor(0xFF, 0xB4, 0x54), QColor(0x6C, 0x54, 0x26)
    };
    return s;
}

// The themes after Ayu Light/Dark. Each set is tuned against its own
// palette in theme.h, and tests/test_contrastaudit.cpp checks every colour
// on every surface of every theme (4.5:1; 7:1 for High Contrast). The
// numbers are the worst of window / base / alternate row / button.
const Set &sepiaSet()
{
    static const Set s{
        QColor(0x3E, 0x5F, 0x12),   // ok        olive green
        QColor(0x87, 0x47, 0x00),   // warning   burnt orange
        QColor(0x9F, 0x2B, 0x2B),   // error     brick red
        QColor(0x65, 0x55, 0x3F),   // muted     umber
        QColor(0x28, 0x5E, 0x80),   // accent    ink blue      5.29 : 1
        QColor(0xF4, 0xDF, 0xAE), QColor(0x55, 0x3A, 0x00), QColor(0xD6, 0xB7, 0x7A)
    };
    return s;
}
const Set &sageSet()
{
    static const Set s{
        QColor(0x2D, 0x63, 0x25),   // ok
        QColor(0x83, 0x4F, 0x00),   // warning
        QColor(0xA3, 0x30, 0x30),   // error
        QColor(0x52, 0x60, 0x57),   // muted       5.29 : 1
        QColor(0x24, 0x5E, 0x78),   // accent
        QColor(0xF1, 0xE3, 0xBB), QColor(0x55, 0x3E, 0x00), QColor(0xCD, 0xB6, 0x7A)
    };
    return s;
}
const Set &nordSet()
{
    static const Set s{
        QColor(0xA3, 0xBE, 0x8C),   // ok        nord14        4.94 : 1
        QColor(0xEB, 0xCB, 0x8B),   // warning   nord13
        QColor(0xF2, 0xA5, 0xAB),   // error     nord11, lifted (the published #BF616A is 3.3 : 1 on a button)
        QColor(0xAE, 0xB6, 0xC6),   // muted
        QColor(0x88, 0xC0, 0xD0),   // accent    nord8
        QColor(0x3D, 0x35, 0x26), QColor(0xEB, 0xCB, 0x8B), QColor(0x6E, 0x5E, 0x3C)
    };
    return s;
}
const Set &mochaSet()
{
    static const Set s{
        QColor(0xA6, 0xE3, 0xA1),   // ok        catppuccin green
        QColor(0xFA, 0xB3, 0x87),   // warning   peach
        QColor(0xF3, 0x8B, 0xA8),   // error     red           5.43 : 1
        QColor(0xA6, 0xAD, 0xC8),   // muted     subtext0
        QColor(0x89, 0xB4, 0xFA),   // accent    blue
        QColor(0x3A, 0x2E, 0x26), QColor(0xFA, 0xB3, 0x87), QColor(0x6B, 0x52, 0x40)
    };
    return s;
}
const Set &highContrastSet()
{
    // 7 : 1 (WCAG AAA) on every surface, not 4.5.
    static const Set s{
        QColor(0x1A, 0x53, 0x19),   // ok                      7.47 : 1
        QColor(0x6B, 0x35, 0x00),   // warning
        QColor(0x9A, 0x00, 0x00),   // error                   7.22 : 1
        QColor(0x3A, 0x3A, 0x3A),   // muted
        QColor(0x00, 0x3E, 0x9C),   // accent
        QColor(0xFF, 0xE3, 0x8A), QColor(0x00, 0x00, 0x00), QColor(0x00, 0x00, 0x00)
    };
    return s;
}

// Session 91: the four themes on Sage's recipe (session 89). They used to
// fall through to Ayu Light's set, which is tuned for #FCFCFC; on their
// tinted surfaces its muted grey and badge inks fell just under 4.5:1
// (4.44-4.47). Sage's ok / warning / error clear 5:1 on all four as they
// are; muted is each theme's own tinted grey and accent its own link colour,
// darkened until the worst surface -- and the flasher's badge tint -- is at
// least 4.7:1.
const Set &oceanSet()
{
    static const Set s{
        QColor(0x2D, 0x63, 0x25),   // ok
        QColor(0x83, 0x4F, 0x00),   // warning
        QColor(0xA3, 0x30, 0x30),   // error
        QColor(0x58, 0x61, 0x70),   // muted     the theme's own grey
        QColor(0x1B, 0x5A, 0x8C),   // accent    the theme's link colour
        QColor(0xF1, 0xE3, 0xBB), QColor(0x55, 0x3E, 0x00), QColor(0xCD, 0xB6, 0x7A)
    };
    return s;
}
const Set &lavenderSet()
{
    static const Set s{
        QColor(0x2D, 0x63, 0x25),   // ok
        QColor(0x83, 0x4F, 0x00),   // warning
        QColor(0xA3, 0x30, 0x30),   // error
        QColor(0x62, 0x5C, 0x70),   // muted     the theme's own grey
        QColor(0x3E, 0x4C, 0x98),   // accent    the theme's link colour
        QColor(0xF1, 0xE3, 0xBB), QColor(0x55, 0x3E, 0x00), QColor(0xCD, 0xB6, 0x7A)
    };
    return s;
}
const Set &roseSet()
{
    static const Set s{
        QColor(0x2D, 0x63, 0x25),   // ok
        QColor(0x83, 0x4F, 0x00),   // warning
        QColor(0xA3, 0x30, 0x30),   // error
        QColor(0x6E, 0x5C, 0x61),   // muted     the theme's own grey
        QColor(0x8A, 0x2E, 0x4C),   // accent    the theme's link colour
        QColor(0xF1, 0xE3, 0xBB), QColor(0x55, 0x3E, 0x00), QColor(0xCD, 0xB6, 0x7A)
    };
    return s;
}
const Set &amberSet()
{
    static const Set s{
        QColor(0x2D, 0x63, 0x25),   // ok
        QColor(0x83, 0x4F, 0x00),   // warning
        QColor(0xA3, 0x30, 0x30),   // error
        QColor(0x68, 0x60, 0x56),   // muted     the theme's own grey
        QColor(0x8A, 0x4B, 0x06),   // accent    the theme's link colour
        QColor(0xF1, 0xE3, 0xBB), QColor(0x55, 0x3E, 0x00), QColor(0xCD, 0xB6, 0x7A)
    };
    return s;
}

// Set by ThemeUtil::apply(). -1 = never set (a test that only set a
// palette): fall back to light/dark by the window colour, as before.
int g_activeTheme = -1;

const Set &active()
{
    switch (g_activeTheme) {
    case 2:  return sepiaSet();
    case 3:  return sageSet();
    case 4:  return nordSet();
    case 5:  return mochaSet();
    case 6:  return highContrastSet();
    case 7:  return oceanSet();
    case 8:  return lavenderSet();
    case 9:  return roseSet();
    case 10: return amberSet();
    default: break;
    }
    if (dark()) {
        return darkSet();
    }
    return lightSet();
}

// Series cycles. Adjacent entries differ in hue on purpose — the operator's
// question is "which step / which target is this", and a gradient makes
// neighbours hardest to tell apart exactly where they meet.
//
// The dark set is the braking panel's original cycle (sessions 58–63); on
// #0B0E14 every entry clears 6:1. On Ayu Light's #FCFCFC five of those six
// fell below 3:1 (teal 2.12, green 2.25, amber 2.26), so the light set keeps
// each hue and lowers lightness until it clears 3.2:1.
const QColor kSeriesLight[] = {
    QColor(0x3E, 0x91, 0xE4),   // blue    3.21 : 1
    QColor(0x36, 0x9F, 0x63),   // green   3.25 : 1
    QColor(0xB9, 0x82, 0x25),   // amber   3.26 : 1
    QColor(0xAE, 0x76, 0xD8),   // violet  3.21 : 1
    QColor(0x38, 0x9B, 0x9B),   // teal    3.23 : 1
    QColor(0xDD, 0x6E, 0x43),   // orange  3.21 : 1
};
const QColor kSeriesDark[] = {
    QColor(0x4E, 0x9A, 0xE6),   // blue    6.52 : 1
    QColor(0x46, 0xC0, 0x7A),   // green   8.35 : 1
    QColor(0xD9, 0xA0, 0x40),   // amber   8.33 : 1
    QColor(0xB0, 0x7A, 0xD9),   // violet  6.10 : 1
    QColor(0x50, 0xC0, 0xC0),   // teal    8.87 : 1
    QColor(0xE0, 0x7A, 0x52),   // orange  6.51 : 1
};
constexpr int kSeriesN = int(sizeof(kSeriesDark) / sizeof(kSeriesDark[0]));
static_assert(sizeof(kSeriesLight) == sizeof(kSeriesDark),
              "light and dark series cycles must be the same length");

double channel(double v)
{
    v /= 255.0;
    return (v <= 0.03928) ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &c)
{
    return 0.2126 * channel(c.red())
         + 0.7152 * channel(c.green())
         + 0.0722 * channel(c.blue());
}

// Who wants telling when the palette changes.
//
// A registry rather than an event filter, for two reasons. Qt delivers
// PaletteChange to a widget only if its own effective palette changed and
// it has been polished, so a child of a viewport — which is exactly what
// the empty-state overlay is — could sit there unrepainted while every
// visible top-level widget updated around it. And the alternative, an
// application-wide filter, would see every mouse move in a window that
// already handles thousands of rows a second.
struct Entry {
    QPointer<QWidget>     owner;   // dead pointer = drop the entry
    std::function<void()> fn;
};

QVector<Entry> &registry()
{
    static QVector<Entry> r;
    return r;
}

bool g_notifying = false;

}  // namespace

bool dark()
{
    // Window rather than Base: Base is white in the light palette and very
    // nearly black in the dark one, but a mid-grey Base would make the
    // answer arbitrary. The window colour is the surface most of this text
    // is drawn on.
    return qApp && qApp->palette().color(QPalette::Window).lightness() < 128;
}

namespace {

// Colour-blind-safe status colours (session 79).
//
// The theme sets put ok in green and error in red, which is the pair a
// red-green colour-blind operator cannot tell apart: simulated deuteranopia
// takes Sepia's ok and error to within 4 CIELAB units of each other, and
// protanopia does much the same to High Contrast. The glyphs (✓ ! ✗) still
// carry the meaning, but a status line read at a glance is read by colour.
//
// So this mode moves the three verdicts onto the axis every common colour
// deficiency keeps: ok is blue, warning is amber (yellow on the dark themes)
// and error is a raspberry on the light themes and a vermillion on the dark
// ones. Each hue is then moved only as far as the theme's contrast floor
// needs, against every surface text sits on, exactly as withContrast() does
// for plot series. The hues were chosen by simulation, not by eye: every
// verdict pair stays at least 20 CIELAB units apart under protanopia,
// deuteranopia and tritanopia in all seven themes (the theme sets reach 4).
//
// Accent is the theme's blue, which is now ok's hue: in this mode it becomes
// the palette's own text colour, so "emphasis" can never be read as "ok".
// tests/test_colorblind.cpp simulates protanopia, deuteranopia and
// tritanopia and holds every pair apart in every theme.
bool g_colorBlindSafe = false;

enum class Verdict { Ok, Warning, Error };

double floorForTheme()
{
    return g_activeTheme == 6 ? 7.0 : 4.5;   // High Contrast promises AAA
}

QColor onEverySurface(const QColor &hue, double floor)
{
    if (!qApp) {
        return hue;
    }
    const QPalette p = qApp->palette();
    QColor c = hue;
    for (QPalette::ColorRole role : { QPalette::Window, QPalette::Base,
                                      QPalette::AlternateBase, QPalette::Button }) {
        c = withContrast(c, p.color(role), floor);
    }
    return c;
}

QColor safeVerdict(Verdict v)
{
    const double floor = floorForTheme();
    if (dark()) {
        switch (v) {
        case Verdict::Ok:      return onEverySurface(QColor(0x56, 0xB4, 0xE9), floor);   // sky blue
        case Verdict::Warning: return onEverySurface(QColor(0xF0, 0xE4, 0x42), floor);   // yellow
        case Verdict::Error:   return onEverySurface(QColor(0xE0, 0x5A, 0x00), floor);   // vermillion
        }
    }
    switch (v) {
    case Verdict::Ok:      return onEverySurface(QColor(0x00, 0x60, 0xE0), floor);   // blue
    case Verdict::Warning: return onEverySurface(QColor(0xD0, 0x9A, 0x00), floor);   // amber
    case Verdict::Error:   return onEverySurface(QColor(0xD0, 0x40, 0x80), floor);   // raspberry
    }
    return QColor();
}

}  // namespace

void setColorBlindSafe(bool on) { g_colorBlindSafe = on; }

QColor signalLamp(const QString &name)
{
    if (name == QLatin1String("red"))    return QColor(0xD7, 0x26, 0x26);
    if (name == QLatin1String("yellow")) return QColor(0xF2, 0xB7, 0x05);
    if (name == QLatin1String("green"))  return QColor(0x1E, 0xA0, 0x4A);
    return QColor(0xF4, 0xF4, 0xF4);
}
bool colorBlindSafe()           { return g_colorBlindSafe; }

QColor ok()           { return g_colorBlindSafe ? safeVerdict(Verdict::Ok)      : active().ok; }
QColor warning()      { return g_colorBlindSafe ? safeVerdict(Verdict::Warning) : active().warning; }
QColor error()        { return g_colorBlindSafe ? safeVerdict(Verdict::Error)   : active().error; }
QColor muted()        { return active().muted; }
QColor accent()
{
    if (g_colorBlindSafe && qApp) {
        return qApp->palette().color(QPalette::WindowText);
    }
    return active().accent;
}
QColor bannerBg()     { return active().bannerBg; }
QColor bannerFg()     { return active().bannerFg; }
QColor bannerBorder() { return active().bannerBorder; }

int seriesCount() { return kSeriesN; }

QColor series(int i)
{
    const int k = ((i % kSeriesN) + kSeriesN) % kSeriesN;
    QColor base = kSeriesLight[k];
    if (dark()) {
        base = kSeriesDark[k];
    }
    // A plot line has to clear 3:1 against the plot's background (WCAG's
    // graphical-object floor). The two Ayu themes already do, so their
    // colours come back unchanged; on any other theme the same hue is moved
    // just far enough away from the background to clear it.
    QColor background = QColor(0xFF, 0xFF, 0xFF);
    if (qApp) {
        background = qApp->palette().color(QPalette::Base);
    }
    return withContrast(base, background, 3.0);
}

QColor withContrast(const QColor &ink, const QColor &background, double floor)
{
    QColor adjusted = ink;
    if (contrastRatio(adjusted, background) >= floor) {
        return adjusted;
    }
    // Away from the background: darker ink on a light surface, lighter on
    // a dark one. Hue and saturation are kept, so a series stays "the blue".
    const bool darken = luminance(background) > 0.18;
    const QColor hsl = ink.toHsl();
    int lightness = hsl.lightness();
    for (int step = 0; step < 128 && contrastRatio(adjusted, background) < floor; ++step) {
        if (darken) {
            lightness = qMax(0, lightness - 2);
        } else {
            lightness = qMin(255, lightness + 2);
        }
        adjusted = QColor::fromHsl(hsl.hslHue(), hsl.hslSaturation(), lightness);
    }
    return adjusted;
}

QColor selectedMark()
{
    // A clear, saturated green; withContrast() darkens it on light themes and
    // lightens it on dark ones only as far as 3:1 needs.
    QColor background = QColor(0xFF, 0xFF, 0xFF);
    if (qApp) {
        background = qApp->palette().color(QPalette::Base);
    }
    QColor green(0x2E, 0xB8, 0x4B);
    if (dark()) {
        green = QColor(0x4A, 0xD6, 0x6D);
    }
    return withContrast(green, background, 3.0);
}

QColor tagColor(int index)
{
    // Hues for a light surface and for a dark one; withContrast() then
    // guarantees 3:1 against whichever of window / base is closer.
    static const QColor light[kTagCount] = {
        QColor(0xD3, 0x2F, 0x2F), QColor(0xE0, 0x6C, 0x00), QColor(0xB8, 0x8A, 0x00), QColor(0x2E, 0x9E, 0x44),
        QColor(0x00, 0x8C, 0x8C), QColor(0x1E, 0x6F, 0xD9), QColor(0x8E, 0x44, 0xC8), QColor(0x70, 0x78, 0x80)
    };
    static const QColor darkHues[kTagCount] = {
        QColor(0xFF, 0x6B, 0x6B), QColor(0xFF, 0xA0, 0x4D), QColor(0xF2, 0xCC, 0x4D), QColor(0x5C, 0xD6, 0x7A),
        QColor(0x4D, 0xD0, 0xD0), QColor(0x6B, 0xA8, 0xFF), QColor(0xC0, 0x8C, 0xFF), QColor(0xA8, 0xB0, 0xB8)
    };
    const int k = ((index % kTagCount) + kTagCount) % kTagCount;
    QColor hue = light[k];
    if (dark()) {
        hue = darkHues[k];
    }
    if (!qApp) {
        return hue;
    }
    const QPalette palette = qApp->palette();
    // Against the window first, then the base: the result clears both.
    QColor adjusted = withContrast(hue, palette.color(QPalette::Window), 3.0);
    adjusted = withContrast(adjusted, palette.color(QPalette::Base), 3.0);
    return adjusted;
}

QString tagName(int index)
{
    static const char *const names[kTagCount] = {
        "Red", "Orange", "Yellow", "Green", "Teal", "Blue", "Purple", "Grey"
    };
    const int k = ((index % kTagCount) + kTagCount) % kTagCount;
    return QString::fromLatin1(names[k]);
}

void setActiveTheme(int themeId)
{
    g_activeTheme = themeId;
}

int activeTheme()
{
    return g_activeTheme;
}

QColor frame()
{
    QColor c = muted();
    c.setAlpha(120);
    return c;
}

QColor grid()
{
    QColor c = muted();
    c.setAlpha(60);
    return c;
}

QString style(const QColor &c)
{
    return QStringLiteral("color:%1;").arg(c.name());
}

QString okStyle()      { return style(ok()); }
QString warningStyle() { return style(warning()); }
QString errorStyle()   { return style(error()); }
QString mutedStyle()   { return style(muted()); }
QString accentStyle()  { return style(accent()); }

QString bannerStyle()
{
    return QStringLiteral("QLabel { background:%1; color:%2; border:1px solid %3;"
                          " padding:6px; }")
        .arg(bannerBg().name(), bannerFg().name(), bannerBorder().name());
}

double contrastRatio(const QColor &a, const QColor &b)
{
    double l1 = luminance(a);
    double l2 = luminance(b);
    if (l1 < l2) { std::swap(l1, l2); }
    return (l1 + 0.05) / (l2 + 0.05);
}

void notifyThemeChanged()
{
    // Guarded: the obvious thing to do in a repaint callback is
    // setStyleSheet(), which re-polishes the widget and can land back here.
    // Unguarded that recurses until the stack runs out, which presents as a
    // bare segfault and says nothing about where it came from.
    if (g_notifying) { return; }
    g_notifying = true;

    QVector<Entry> &r = registry();
    for (int i = r.size() - 1; i >= 0; --i) {
        if (!r.at(i).owner) { r.removeAt(i); continue; }   // widget is gone
        if (r.at(i).fn) { r.at(i).fn(); }
    }

    g_notifying = false;
}

void onThemeChange(QWidget *w, std::function<void()> repaint)
{
    if (!w || !repaint) { return; }
    registry().push_back({ QPointer<QWidget>(w), std::move(repaint) });
}

}  // namespace UiColor
