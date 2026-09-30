#ifndef THEME_H
#define THEME_H

// =============================================================================
//  Theme
//  -----------------------------------------------------------------------------
//  The colour themes. Two are the originals (Ayu Light / Ayu Dark); the rest
//  were added for long shifts at the console: softer backgrounds that are
//  easier on the eyes, and one High Contrast theme for bright cabs and
//  sunlight.
//
//  EVERY THEME IS HELD TO THE SAME FLOORS
//    tests/test_contrastaudit.cpp loops over ThemeUtil::all() and checks,
//    for each theme, every palette role pair that can be text on a surface
//    and every semantic colour (ok / warning / error / muted / accent) on
//    every surface: 4.5 : 1 (WCAG AA), 3 : 1 for disabled and placeholder
//    text, a visible row stripe -- and 7 : 1 (AAA) for High Contrast. A
//    theme that does not pass does not build green. The semantic colours
//    for each theme are in uicolors.cpp.
//
//  Code that only needs "is this a dark background" (the log colour rules
//  have a light and a dark variant) asks ThemeUtil::isDark(), never
//  `== Theme::Dark`.
//
//  The application-wide palette is set up once at startup from the value
//  in Settings, applied via QApplication::setPalette + setStyle("Fusion").
//  Per-row colors come from ColorRules and are picked at paint time by
//  LogModel from the active theme.
// =============================================================================

#include <QApplication>

#include "uicolors.h"
#include <QPalette>
#include <QStyleFactory>
#include <QString>
#include <QVector>

// Settings stores the string key, but UiColor::setActiveTheme() receives
// these as ints: keep the values stable.
enum class Theme {
    Light        = 0,   // Ayu Light
    Dark         = 1,   // Ayu Dark
    Sepia        = 2,   // warm paper
    Sage         = 3,   // soft green-grey
    Nord         = 4,   // cool blue-grey, dark
    Mocha        = 5,   // Catppuccin Mocha, dark
    HighContrast = 6    // black on white, 7:1 everywhere
};

namespace ThemeUtil {

// Every theme, in the order the Settings list and the Theme menu show them:
// the light ones, then the dark ones.
inline QVector<Theme> all()
{
    return { Theme::Light, Theme::Sepia, Theme::Sage, Theme::HighContrast,
             Theme::Dark, Theme::Nord, Theme::Mocha };
}

// The key stored in dlconsole.ini.
inline const char *toString(Theme t)
{
    switch (t) {
    case Theme::Light:        return "light";
    case Theme::Dark:         return "dark";
    case Theme::Sepia:        return "sepia";
    case Theme::Sage:         return "sage";
    case Theme::Nord:         return "nord";
    case Theme::Mocha:        return "mocha";
    case Theme::HighContrast: return "high-contrast";
    }
    return "light";
}

// An unknown key (an older or newer dlconsole.ini) falls back to Light.
inline Theme fromString(const QString &s)
{
    for (Theme t : all()) {
        if (s.compare(QLatin1String(toString(t)), Qt::CaseInsensitive) == 0) {
            return t;
        }
    }
    return Theme::Light;
}

// The name shown to the operator.
inline QString label(Theme t)
{
    switch (t) {
    case Theme::Light:        return QStringLiteral("Light");
    case Theme::Dark:         return QStringLiteral("Dark");
    case Theme::Sepia:        return QStringLiteral("Sepia");
    case Theme::Sage:         return QStringLiteral("Sage");
    case Theme::Nord:         return QStringLiteral("Nord");
    case Theme::Mocha:        return QStringLiteral("Mocha");
    case Theme::HighContrast: return QStringLiteral("High contrast");
    }
    return QStringLiteral("Light");
}

// Dark background? Decides which variant of a log colour rule is used.
inline bool isDark(Theme t)
{
    return t == Theme::Dark || t == Theme::Nord || t == Theme::Mocha;
}

// The colours the newer themes are defined by. Everything a theme must
// specify is here, so none of Fusion's defaults (made for its own grey
// theme) leak through as a stray hairline.
struct ThemeColors {
    QColor window, base, alternateBase, text, button, highlight, highlightedText;
    QColor toolTipBase, toolTipText, link, linkVisited, placeholder, disabled;
    QColor midlight, mid, dark, shadow, disabledHighlight, brightText;
};

inline QPalette paletteFrom(const ThemeColors &c)
{
    QPalette p;
    p.setColor(QPalette::Window,          c.window);
    p.setColor(QPalette::WindowText,      c.text);
    p.setColor(QPalette::Base,            c.base);
    p.setColor(QPalette::AlternateBase,   c.alternateBase);
    p.setColor(QPalette::Text,            c.text);
    p.setColor(QPalette::Button,          c.button);
    p.setColor(QPalette::ButtonText,      c.text);
    p.setColor(QPalette::BrightText,      c.brightText);
    p.setColor(QPalette::Highlight,       c.highlight);
    p.setColor(QPalette::HighlightedText, c.highlightedText);
    p.setColor(QPalette::ToolTipBase,     c.toolTipBase);
    p.setColor(QPalette::ToolTipText,     c.toolTipText);
    p.setColor(QPalette::Link,            c.link);
    p.setColor(QPalette::LinkVisited,     c.linkVisited);
    p.setColor(QPalette::PlaceholderText, c.placeholder);
    p.setColor(QPalette::Midlight,        c.midlight);
    p.setColor(QPalette::Mid,             c.mid);
    p.setColor(QPalette::Dark,            c.dark);
    p.setColor(QPalette::Shadow,          c.shadow);
    p.setColor(QPalette::Disabled, QPalette::Text,       c.disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight,  c.disabledHighlight);
    return p;
}

// The newer themes. Body text contrast: Sepia 11.5 : 1, Sage 11.3 : 1,
// Nord 10.4 : 1, Mocha 12.1 : 1, High contrast 21 : 1 -- against Ayu
// Light's 8.1 and Ayu Dark's 10.
//
// Field order: window, base, alternateBase, text, button, highlight,
// highlightedText, toolTipBase, toolTipText, link, linkVisited,
// placeholder, disabled, midlight, mid, dark, shadow, disabledHighlight,
// brightText.
inline ThemeColors colorsFor(Theme t)
{
    ThemeColors c;
    if (t == Theme::Sepia) {
        // Warm paper, brown ink: little blue light, calm for long reading.
        c = { QColor(0xEF, 0xE6, 0xD3), QColor(0xF9, 0xF3, 0xE6), QColor(0xEA, 0xDF, 0xC8),
              QColor(0x3E, 0x30, 0x21), QColor(0xF4, 0xEC, 0xDC), QColor(0x8C, 0x5A, 0x2B),
              QColor(0xFF, 0xFB, 0xF3), QColor(0x2E, 0x24, 0x18), QColor(0xF1, 0xE8, 0xD6),
              QColor(0x7A, 0x44, 0x10), QColor(0x6A, 0x3C, 0x7D), QColor(0x85, 0x72, 0x59),
              QColor(0x7F, 0x6D, 0x55), QColor(0xFF, 0xF9, 0xEE), QColor(0xB8, 0xA5, 0x82),
              QColor(0xA8, 0x96, 0x74), QColor(0x8E, 0x7C, 0x5C), QColor(0xE6, 0xDB, 0xC4),
              QColor(0xA3, 0x2A, 0x2A) };
    } else if (t == Theme::Sage) {
        // Soft green-grey: neutral and restful, no pure white anywhere.
        c = { QColor(0xE6, 0xEC, 0xE5), QColor(0xF4, 0xF7, 0xF3), QColor(0xE0, 0xE8, 0xDF),
              QColor(0x2C, 0x38, 0x30), QColor(0xED, 0xF2, 0xEC), QColor(0x2F, 0x6B, 0x55),
              QColor(0xFF, 0xFF, 0xFF), QColor(0x1E, 0x26, 0x22), QColor(0xE3, 0xEB, 0xE4),
              QColor(0x1F, 0x5E, 0x6E), QColor(0x5E, 0x4A, 0x86), QColor(0x6C, 0x7A, 0x70),
              QColor(0x6B, 0x78, 0x6E), QColor(0xFF, 0xFF, 0xFF), QColor(0xA9, 0xB8, 0xAB),
              QColor(0x98, 0xA8, 0x9B), QColor(0x7E, 0x8E, 0x81), QColor(0xDC, 0xE3, 0xDB),
              QColor(0xA3, 0x30, 0x30) };
    } else if (t == Theme::Nord) {
        // Nord: arctic blue-grey. nord4 text on a base a step deeper than
        // nord0, for more contrast than the published scheme.
        c = { QColor(0x2E, 0x34, 0x40), QColor(0x27, 0x2C, 0x36), QColor(0x32, 0x39, 0x46),
              QColor(0xD8, 0xDE, 0xE9), QColor(0x3B, 0x42, 0x52), QColor(0x4C, 0x6A, 0x8C),
              QColor(0xFF, 0xFF, 0xFF), QColor(0x3B, 0x42, 0x52), QColor(0xEC, 0xEF, 0xF4),
              QColor(0x88, 0xC0, 0xD0), QColor(0xC4, 0xA0, 0xC0), QColor(0x86, 0x90, 0xA2),
              QColor(0x8A, 0x93, 0xA5), QColor(0x3B, 0x42, 0x52), QColor(0x5A, 0x64, 0x78),
              QColor(0x1F, 0x23, 0x2B), QColor(0x19, 0x1C, 0x22), QColor(0x33, 0x39, 0x45),
              QColor(0xF2, 0xA5, 0xAB) };
    } else if (t == Theme::Mocha) {
        // Catppuccin Mocha: warm dark with pastel accents.
        c = { QColor(0x1E, 0x1E, 0x2E), QColor(0x18, 0x18, 0x25), QColor(0x26, 0x26, 0x37),
              QColor(0xCD, 0xD6, 0xF4), QColor(0x31, 0x32, 0x44), QColor(0x4A, 0x4F, 0x7A),
              QColor(0xFF, 0xFF, 0xFF), QColor(0x31, 0x32, 0x44), QColor(0xCD, 0xD6, 0xF4),
              QColor(0x89, 0xB4, 0xFA), QColor(0xCB, 0xA6, 0xF7), QColor(0x7F, 0x84, 0x9C),
              QColor(0x85, 0x89, 0xA2), QColor(0x31, 0x32, 0x44), QColor(0x58, 0x5B, 0x70),
              QColor(0x11, 0x11, 0x1B), QColor(0x0B, 0x0B, 0x12), QColor(0x2A, 0x2B, 0x3C),
              QColor(0xF3, 0x8B, 0xA8) };
    } else {
        // High contrast: black on white, dark frames, a strong selection.
        // For bright cabs and daylight, and anyone who wants it.
        c = { QColor(0xFF, 0xFF, 0xFF), QColor(0xFF, 0xFF, 0xFF), QColor(0xE8, 0xE8, 0xE8),
              QColor(0x00, 0x00, 0x00), QColor(0xF2, 0xF2, 0xF2), QColor(0x00, 0x3E, 0x9C),
              QColor(0xFF, 0xFF, 0xFF), QColor(0x00, 0x00, 0x00), QColor(0xFF, 0xFF, 0xFF),
              QColor(0x00, 0x3E, 0x9C), QColor(0x5B, 0x1F, 0x8F), QColor(0x50, 0x50, 0x50),
              QColor(0x50, 0x50, 0x50), QColor(0xFF, 0xFF, 0xFF), QColor(0x6E, 0x6E, 0x6E),
              QColor(0x50, 0x50, 0x50), QColor(0x00, 0x00, 0x00), QColor(0xD0, 0xD0, 0xD0),
              QColor(0x9A, 0x00, 0x00) };
    }
    return c;
}

// Apply the theme's palette to QApplication. Safe to call multiple times
// (e.g. when the user toggles via the settings dialog).
inline void apply(Theme t)
{
    // Fusion gives us a consistent look across Windows / Linux and is the
    // style that responds correctly to setPalette() — the native styles
    // ignore palette overrides on Windows.
    qApp->setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    if (t != Theme::Light && t != Theme::Dark) {
        p = paletteFrom(colorsFor(t));
    } else if (t == Theme::Dark) {
        // Slightly cooler and darker than before, with the greys that Fusion
        // draws its frames and separators from actually specified. Leaving
        // Mid/Midlight/Dark/Shadow at their defaults is what made borders
        // read as heavy black hairlines against a dark window.
        p.setColor(QPalette::Window,          QColor(0x0F, 0x13, 0x1A));
        // Ayu Dark. The palette is the published scheme rather than an
        // approximation of it: editor background #0B0E14 as Base, panel
        // background #0F131A as Window, foreground #BFBDB6, and the tag
        // blue #39BAE6 for links.
        //
        // One deliberate departure. Ayu's line highlight (#11151C) sits at
        // 1.06:1 against the background, which is fine for a code editor
        // where it marks one line, and useless as an alternating row colour
        // in a table of ten thousand — it is the same mistake the light
        // palette used to make at 1.04:1. AlternateBase is opened up to
        // #171D27 (1.14:1): still barely there, but there.
        p.setColor(QPalette::WindowText,      QColor(0xBF, 0xBD, 0xB6));
        p.setColor(QPalette::Base,            QColor(0x0B, 0x0E, 0x14));
        p.setColor(QPalette::AlternateBase,   QColor(0x17, 0x1D, 0x27));
        p.setColor(QPalette::Text,            QColor(0xBF, 0xBD, 0xB6));
        p.setColor(QPalette::Button,          QColor(0x13, 0x17, 0x21));
        p.setColor(QPalette::ButtonText,      QColor(0xBF, 0xBD, 0xB6));
        p.setColor(QPalette::BrightText,      QColor(0xF0, 0x71, 0x71));
        p.setColor(QPalette::Highlight,       QColor(0x1F, 0x3B, 0x5C));
        p.setColor(QPalette::HighlightedText, QColor(0xE6, 0xED, 0xF5));
        p.setColor(QPalette::ToolTipBase,     QColor(0x13, 0x17, 0x21));
        p.setColor(QPalette::ToolTipText,     QColor(0xBF, 0xBD, 0xB6));
        p.setColor(QPalette::Link,            QColor(0x39, 0xBA, 0xE6));
        p.setColor(QPalette::LinkVisited,     QColor(0xD2, 0xA6, 0xFF));
        p.setColor(QPalette::PlaceholderText, QColor(0x70, 0x7A, 0x8C));
        p.setColor(QPalette::Midlight,        QColor(0x1A, 0x1F, 0x29));
        // Mid is what Fusion outlines a checkbox or radio indicator with.
        // At ayu's #2A303A that outline is 1.4:1 against this window — an
        // unticked checkbox was, in practice, not there. Brightened until
        // the box is visible without becoming a line that draws the eye.
        p.setColor(QPalette::Mid,             QColor(0x3E, 0x46, 0x55));
        p.setColor(QPalette::Dark,            QColor(0x07, 0x0A, 0x0F));
        p.setColor(QPalette::Shadow,          QColor(0x05, 0x07, 0x0A));
        // Ayu's ui.fg is #565B66, which is 2.84:1 against this background —
        // right for a code editor's inactive chrome, and below the 3:1 floor
        // for text an operator still has to read while it is greyed out.
        // Opened up one step.
        p.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x6C, 0x73, 0x80));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6C, 0x73, 0x80));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x6C, 0x73, 0x80));
        p.setColor(QPalette::Disabled, QPalette::Highlight,  QColor(0x13, 0x17, 0x21));
    } else {
        // Light palette: explicit values rather than a default-constructed
        // QPalette. Default construction picks up whatever the qApp palette
        // currently is, which is usually the previously-applied DARK one — so
        // toggling dark→light by default-constructing would do nothing.
        //
        // The window is a touch off pure grey and the alternating row colour
        // is far enough from white to actually be seen; at 245 it was 1.04:1
        // against the base, which is to say invisible.
        // Ayu Light, the sibling of the dark theme above rather than a
        // generic grey: editor background #FCFCFC as Base, panel #F3F4F5 as
        // Window, and ayu's own #E7E8E9 rule line as the alternating row.
        //
        // One departure, and it is the same argument as in the dark theme.
        // Ayu's foreground is #5C6166, a soft grey that reads beautifully in
        // an editor and gives 6.1:1 here. This window is hex and timestamps
        // scanned for hours at a stretch, so the text is a step darker at
        // #4A4F54 (8.1:1) — same neutral hue, more contrast than the scheme
        // ships with.
        p.setColor(QPalette::Window,          QColor(0xF3, 0xF4, 0xF5));
        p.setColor(QPalette::WindowText,      QColor(0x4A, 0x4F, 0x54));
        p.setColor(QPalette::Base,            QColor(0xFC, 0xFC, 0xFC));
        p.setColor(QPalette::AlternateBase,   QColor(0xE7, 0xE8, 0xE9));
        p.setColor(QPalette::Text,            QColor(0x4A, 0x4F, 0x54));
        p.setColor(QPalette::Button,          QColor(0xF8, 0xF9, 0xFA));
        p.setColor(QPalette::ButtonText,      QColor(0x4A, 0x4F, 0x54));
        p.setColor(QPalette::BrightText,      QColor(0xC0, 0x3A, 0x3A));
        p.setColor(QPalette::Highlight,       QColor(0x1F, 0x6F, 0xB2));
        p.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
        // A dark tooltip on both themes. The pale-yellow box is the one part
        // of the old look that dated the whole window.
        p.setColor(QPalette::ToolTipBase,     QColor(0x13, 0x17, 0x21));
        p.setColor(QPalette::ToolTipText,     QColor(0xBF, 0xBD, 0xB6));
        p.setColor(QPalette::Link,            QColor(0x1A, 0x6F, 0xA8));
        p.setColor(QPalette::LinkVisited,     QColor(0x7C, 0x5B, 0xB8));
        // Ayu's ui.fg. Dim enough to read as inactive, still over the 3:1
        // floor that greyed-out text has to clear to remain readable.
        p.setColor(QPalette::PlaceholderText, QColor(0x8A, 0x91, 0x99));
        p.setColor(QPalette::Midlight,        QColor(0xFF, 0xFF, 0xFF));
        p.setColor(QPalette::Mid,             QColor(0xDC, 0xDE, 0xE0));
        p.setColor(QPalette::Dark,            QColor(0xB8, 0xBC, 0xC0));
        p.setColor(QPalette::Shadow,          QColor(0x9A, 0xA0, 0xA5));
        // Against the alternating row rather than the base: ayu's #8A9199 is
        // 2.6:1 there, and disabled text on a striped table is exactly where
        // it gets read.
        p.setColor(QPalette::Disabled, QPalette::Text,       QColor(0x76, 0x7D, 0x84));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x76, 0x7D, 0x84));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x76, 0x7D, 0x84));
        p.setColor(QPalette::Disabled, QPalette::Highlight,  QColor(0xE0, 0xE2, 0xE4));
    }
    qApp->setPalette(p);
    UiColor::setActiveTheme(static_cast<int>(t));
    // Tell everything that coloured itself from the old palette. Qt's own
    // PaletteChange does not reliably reach a child of a viewport, which is
    // where several of this program's surfaces live.
    UiColor::notifyThemeChanged();
}

}   // namespace ThemeUtil

#endif // THEME_H
