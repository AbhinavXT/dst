#include "testutil.h"

#include "cabio.h"
#include "settings.h"
#include "theme.h"
#include "uicolors.h"

#ifdef DL_HAVE_SERIAL
#include "cabiosimwindow.h"
#include "brakebenchwindow.h"
#endif

#include <QApplication>
#include <QSettings>
#include <QWidget>

// =============================================================================
//  Session 359 — Cab I/O Simulator: review follow-ups on session 358.
//
//  A brake lamp says "no output data yet" (not "released") before any output
//  frame, to a screen reader as in its tooltip; a lit rule card's accent edge
//  holds the graphic floor on the card's own tint, not only on Base.
// =============================================================================

TEST_SUITE(session359)
{
#ifdef DL_HAVE_SERIAL
    using namespace CabIo;

    // ---- a lit card's edge on the card's tint, every theme ------------------------------------------
    {
        using namespace CabIoColors;
        const int savedTheme = UiColor::activeTheme();
        int themes = 0;
        QStringList shortIn;
        for (bool safe : { false, true }) {
            UiColor::setColorBlindSafe(safe);
            for (Theme t : ThemeUtil::all()) {
                ThemeUtil::apply(t);
                ++themes;
                if (UiColor::contrastRatio(UiColor::accent(), tileFill(Tile::On)) + 1e-9 < graphicFloor())
                    shortIn << QString::fromLatin1(ThemeUtil::toString(t)) + (safe ? QStringLiteral(" (safe)") : QString());
            }
        }
        UiColor::setColorBlindSafe(false);
        ThemeUtil::apply(Theme(savedTheme));
        CHECK(themes == 2 * ThemeUtil::all().size(), "fixture: every theme (14 since session 434), colour-blind-safe off and on");
        CHECK(shortIn.isEmpty(), qPrintable(QStringLiteral("a lit rule card's accent edge reaches 3 : 1 (7 : 1 High Contrast) "
                                                           "on its tint; short in: %1").arg(shortIn.join(QStringLiteral(", ")))));
    }

    // ---- brake lamps: what they say before and after output data -------------------------------------
    ThemeUtil::apply(Theme::Light);
    {
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        s.beginGroup(QStringLiteral("cabio"));
        s.remove(QStringLiteral("cab"));
        s.remove(QStringLiteral("rules_sifa"));
    }
    {
        CabIoSimWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.setWriteSink([](const QByteArray &) {});
        w.setCab(Cab::Sifa);
        // Session 360: the brake lamps are on the Brake Test Bench's Brake
        // relays tab, lit by this window's output word.
        BrakeBenchWindow bench(nullptr);
        bench.setAttribute(Qt::WA_DeleteOnClose, false);
        QObject::connect(&w, &CabIoSimWindow::outputWordChanged, &bench, &BrakeBenchWindow::observeOutputWord);
        auto lamp = [&bench](const QString &name) -> QWidget * {
            for (QWidget *x : bench.findChildren<QWidget *>())
                if (x->objectName() == QLatin1String("brakeLamp") && x->accessibleName() == name) return x;
            return nullptr;
        };
        QWidget *eb = lamp(QStringLiteral("EB")), *nb = lamp(QStringLiteral("NB"));
        CHECK(eb && nb, "the EB and NB brake lamps");
        if (!eb || !nb) return;
        CHECK(eb->accessibleDescription() == QLatin1String("no output data yet"),
              "before output data the EB lamp says so, not \"released\"");
        CHECK(eb->toolTip() == QLatin1String("EB: no output data yet"), "and its tooltip agrees");

        // EB applied (SIFA: bit 13 = 0, the others released) — a real-layout output word, as in session 358.
        w.feedBytes(outputData(13, 1, quint16(0x3C00 & ~(1u << 13)), WireFormat()));
        CHECK(eb->accessibleDescription() == QLatin1String("applied"), "EB applied: \"applied\"");
        CHECK(nb->accessibleDescription() == QLatin1String("released"), "NB released: \"released\"");
        CHECK(nb->toolTip() == QLatin1String("NB: released"), "and its tooltip agrees");
    }
#endif
}
