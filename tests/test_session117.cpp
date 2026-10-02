#include "testutil.h"

#include "settings.h"
#include "theme.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QApplication>
#include <QButtonGroup>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>

// =============================================================================
//  Session 117 — the UI revamp's foundation: components chosen by role.
//
//  Chips, segmented controls, rail buttons, section captions, surfaces and
//  drawn icons, every colour from the active theme. The checks that matter
//  most: a chip's text meets the contrast floor in EVERY theme (7:1 in High
//  Contrast), and a role really reaches the stylesheet.
// =============================================================================

namespace {
using UiStyle::Tone;

QImage render(const QIcon &icon, int px)
{
    return icon.pixmap(px, px).toImage().convertToFormat(QImage::Format_ARGB32);
}

int inked(const QImage &img)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 40) ++n;
    return n;
}
}  // namespace

TEST_SUITE(session117)
{
    const Theme was = ThemeUtil::fromString(Settings::theme());

    // ---- chips readable in every theme -------------------------------------------------
    {
        QStringList failures;
        for (Theme t : ThemeUtil::all()) {
            ThemeUtil::apply(t);
            UiStyle::apply();
            for (Tone tone : { Tone::Neutral, Tone::Accent, Tone::Ok, Tone::Warn, Tone::Fail }) {
                const double ratio = UiColor::contrastRatio(UiStyle::chipText(tone), UiStyle::chipFill(tone));
                if (ratio + 1e-6 < UiStyle::contrastFloor())
                    failures << QStringLiteral("%1/%2: %3").arg(ThemeUtil::toString(t)).arg(int(tone)).arg(ratio, 0, 'f', 2);
            }
        }
        CHECK(failures.isEmpty(), QByteArray("every chip tone meets the contrast floor in all 11 themes: ")
                                      + failures.join(QStringLiteral(", ")).toUtf8());
    }
    ThemeUtil::apply(Theme::HighContrast);
    UiStyle::apply();
    CHECK(UiStyle::contrastFloor() == 7.0, "High Contrast asks 7:1 of chips");
    ThemeUtil::apply(Theme::Dark);
    UiStyle::apply();
    CHECK(UiStyle::contrastFloor() == 4.5, "the other themes 4.5:1");
    CHECK(UiStyle::chipFill(Tone::Ok) != UiStyle::chipFill(Tone::Fail)
              && UiStyle::chipFill(Tone::Warn) != UiStyle::chipFill(Tone::Neutral),
          "tones are told apart by their fill");

    // ---- roles ---------------------------------------------------------------------------
    {
        QLabel chip(QStringLiteral("\u25CF UDP 50002"));
        UiStyle::makeChip(&chip, Tone::Ok);
        CHECK(chip.property("dlRole").toString() == QLatin1String("chip")
                  && UiStyle::toneOf(&chip) == Tone::Ok, "a chip carries its role and tone");
        UiStyle::setTone(&chip, Tone::Warn);
        CHECK(UiStyle::toneOf(&chip) == Tone::Warn, "and changes tone in place");
        CHECK(qApp->styleSheet().contains(QLatin1String("[dlRole=\"chip\"][dlTone=\"warn\"]")),
              "the stylesheet has a rule for each tone");
        chip.ensurePolished();
        CHECK(chip.palette().color(QPalette::WindowText) == UiStyle::chipText(Tone::Warn)
                  || qApp->styleSheet().contains(UiStyle::chipText(Tone::Warn).name()),
              "the warn chip is drawn in the warn text colour");
    }
    {
        QButtonGroup *g = nullptr;
        QWidget *seg = UiStyle::segmented({ QStringLiteral("All"), QStringLiteral("Errors"),
                                            QStringLiteral("Warnings") }, &g);
        CHECK(g && g->buttons().size() == 3 && g->checkedId() == 0, "segmented: three, the first checked");
        g->button(2)->click();
        CHECK(g->checkedId() == 2 && !g->button(0)->isChecked(), "one segment at a time");
        CHECK(seg->property("dlRole").toString() == QLatin1String("segmented"), "role set");
        delete seg;
    }
    {
        QToolButton b;
        UiStyle::makeRailButton(&b, QStringLiteral("Live log"));
        CHECK(b.toolTip() == QLatin1String("Live log") && b.accessibleName() == QLatin1String("Live log"),
              "a rail button's tooltip is also its accessible name (an icon is not a label)");
        QLabel cap(QStringLiteral("Sources \u00B7 6"));
        UiStyle::makeSectionLabel(&cap);
        CHECK(cap.text() == QStringLiteral("SOURCES \u00B7 6"), "a section caption is upper case");
        QWidget panel;
        UiStyle::makePanel(&panel);
        CHECK(panel.testAttribute(Qt::WA_StyledBackground), "a panel paints its own surface");
        QLabel mono(QStringLiteral("14:02:27.512"));
        UiStyle::makeMono(&mono);
        CHECK(QFontInfo(mono.font()).fixedPitch(), "makeMono gives a fixed-pitch face");
    }
    CHECK(UiStyle::space(1) == 4 && UiStyle::space(4) == 16 && UiStyle::space(6) == 24, "the spacing scale");

    // ---- icons ---------------------------------------------------------------------------
    {
        QStringList empty, same;
        QHash<QString, QImage> imgs;
        for (const QString &n : UiIcons::names()) {
            const QImage img = render(UiIcons::icon(n, Qt::white, 18), 18);
            if (inked(img) < 12) empty << n;
            for (auto it = imgs.constBegin(); it != imgs.constEnd(); ++it)
                if (it.value() == img) same << n + QLatin1Char('=') + it.key();
            imgs.insert(n, img);
        }
        CHECK(empty.isEmpty(), QByteArray("every icon draws something: ") + empty.join(',').toUtf8());
        CHECK(same.isEmpty(), QByteArray("every icon is its own drawing: ") + same.join(',').toUtf8());
        CHECK(inked(render(UiIcons::icon(QStringLiteral("no-such-icon"), Qt::white, 18), 18)) > 12,
              "an unknown name draws a visible box, not nothing");
        const QImage red = render(UiIcons::icon(QStringLiteral("log"), QColor(220, 30, 30), 18), 18);
        bool reddish = false;
        for (int y = 0; y < red.height() && !reddish; ++y)
            for (int x = 0; x < red.width(); ++x)
                if (qAlpha(red.pixel(x, y)) > 200 && qRed(red.pixel(x, y)) > 150 && qGreen(red.pixel(x, y)) < 80) { reddish = true; break; }
        CHECK(reddish, "an icon is drawn in the colour asked for (themes recolour it)");
    }

    ThemeUtil::apply(was);
    UiStyle::apply();
}
