#include "testutil.h"

#include "cabio.h"
#include "pressuregauge.h"
#include "settings.h"
#include "theme.h"
#include "uicolors.h"
#include "brakebenchwindow.h"

#ifdef DL_HAVE_SERIAL
#include "cabiosimwindow.h"
#endif

#include <QApplication>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QSettings>

// =============================================================================
//  Session 358 — Cab I/O Simulator: the Scenarios tab as a brake dashboard.
//
//  Abhinav: "Make the cab io simulator ui better looking and include ui
//  enhancements like gauges etc. In the feedback scenerios when brake is
//  applied the pressure should drop in the gauges too". Asked and answered:
//  a brake dashboard (lamps, BP/BC/MR dials with the target marked) and the
//  rules as cards, lit while their condition holds.
// =============================================================================

namespace {

using namespace CabIo;

void clearSaved()
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("cabio"));
    for (const QString &k : s.childKeys())
        if (k.startsWith(QLatin1String("brakeKg_")) || k.startsWith(QLatin1String("analog")) || k == QLatin1String("rampRate")
            || k == QLatin1String("brakePressures") || k.startsWith(QLatin1String("rules_")) || k == QLatin1String("cab")
            || k == QLatin1String("scenarios"))
            s.remove(k);
    s.endGroup();
    s.remove(QStringLiteral("brakebench"));
}

}  // namespace

TEST_SUITE(session358)
{
    // ---- the gauge's target mark ------------------------------------------------------------------
    {
        PressureGauge g(QStringLiteral("BP"));
        CHECK(g.target() < 0, "no target at first");
        g.setTarget(3.5);
        CHECK(g.target() == 3.5, "a target");
        g.resize(170, 170);
        QImage img(170, 170, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        g.render(&img);
        CHECK(!img.isNull(), "draws with a target");
    }

#ifdef DL_HAVE_SERIAL
    // ---- every theme: the new marks hold the graphic floor, a lit card's words the text floor --------
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
                const QString where = QString::fromLatin1(ThemeUtil::toString(t)) + (safe ? QStringLiteral(" (safe)") : QString());
                const QColor base = qApp->palette().color(QPalette::Base);
                if (UiColor::contrastRatio(UiColor::ok(), base) + 1e-9 < graphicFloor()) shortIn << where + QStringLiteral(" target mark");
                if (UiColor::contrastRatio(UiColor::accent(), base) + 1e-9 < graphicFloor()) shortIn << where + QStringLiteral(" lit edge");
                if (UiColor::contrastRatio(lampRim(QStringLiteral("red")), base) + 1e-9 < graphicFloor()) shortIn << where + QStringLiteral(" brake lamp");
            }
        }
        UiColor::setColorBlindSafe(false);
        ThemeUtil::apply(Theme(savedTheme));
        CHECK(themes == 22, "fixture: eleven themes, colour-blind-safe off and on");
        CHECK(shortIn.isEmpty(), qPrintable(QStringLiteral("target mark, lit card edge and brake lamp reach 3 : 1 (7 : 1 High "
                                                           "Contrast) on the card; short in: %1").arg(shortIn.join(QStringLiteral(", ")))));
    }
    // A lit card's grey words take the tinted tile's ink, in a dark and the High Contrast theme.
    for (Theme t : { Theme::Mocha, Theme::HighContrast }) {
        ThemeUtil::apply(t);
        clearSaved();
        CabIoSimWindow w(nullptr);
        w.setWriteSink([](const QByteArray &) {});
        w.setCab(Cab::Sifa);
        w.feedBytes(outputData(13, 1, quint16(0x3C00 & ~(1u << 13)), WireFormat()));
        QFrame *lit = nullptr;
        for (QFrame *f : w.findChildren<QFrame *>())
            if (f->objectName() == QLatin1String("ruleCard") && f->property("holds").toBool()) { lit = f; break; }
        QLabel *word = nullptr;
        if (lit)
            for (QLabel *l : lit->findChildren<QLabel *>())
                if (l->objectName() == QLatin1String("muted")) { word = l; break; }
        CHECK(word, "a lit card with a grey word");
        if (word) {
            // What the card paints with: its own style sheet (not the label's
            // palette, which Qt 5 can leave stale in a window never shown).
            const QColor ink = CabIoColors::tileInk(UiColor::muted(), CabIoColors::Tile::On);
            CHECK(lit->styleSheet().contains(QStringLiteral("QLabel#muted { color: %1; }").arg(ink.name())),
                  "the lit card's grey words take the tile ink");
            CHECK(UiColor::contrastRatio(ink, CabIoColors::tileFill(CabIoColors::Tile::On)) + 1e-9 >= CabIoColors::textFloor(),
                  qPrintable(QStringLiteral("%1: a lit card's grey word reaches the text floor on its tint")
                                 .arg(QString::fromLatin1(ThemeUtil::toString(t)))));
        }
    }
    ThemeUtil::apply(Theme::Light);
    clearSaved();
    {
        CabIoSimWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.setWriteSink([](const QByteArray &) {});
        w.setCab(Cab::Sifa);
        w.flushLog();
        // Session 360: the dials moved to the Brake Test Bench, which follows
        // this window's output word when its source is the brake relays.
        BrakeBenchWindow bench(nullptr);
        bench.setAttribute(Qt::WA_DeleteOnClose, false);
        QObject::connect(&w, &CabIoSimWindow::outputWordChanged, &bench, &BrakeBenchWindow::observeOutputWord);
        qint64 t = 1000;
        bench.setClock([&t]() { return t; });
        bench.setSource(BrakeBenchWindow::Source::Relays);
        for (int ch = 0; ch < 3; ++ch) bench.setRates(ch, 1.0, 1.0);
        bench.advance(0);

        QVector<PressureGauge *> dash;
        for (PressureGauge *g : bench.findChildren<PressureGauge *>())
            if (g->objectName() == QLatin1String("benchGauge")) dash << g;
        CHECK(dash.size() == 3, "three dials on the bench: BP, BC, MR");
        if (dash.size() != 3) return;
        CHECK(qAbs(dash[0]->value() - 5.0) < 0.02 && qAbs(dash[0]->target() - 5.0) < 0.02,
              "BP reads 5.0 and is heading nowhere else before output data");

        QVector<QFrame *> cards;
        for (QFrame *f : w.findChildren<QFrame *>())
            if (f->objectName() == QLatin1String("ruleCard")) cards << f;
        CHECK(cards.size() == w.rules().size(), "a card per rule");

        // EB applied (SIFA: bit 13 = 0, the others released).
        const quint16 eb = quint16(0x3C00 & ~(1u << 13));
        w.feedBytes(outputData(13, 1, eb, WireFormat()));
        bench.advance(0);
        CHECK(dash[0]->target() == bench.brakePressure(EbApplied, 0), "BP's mark at the EB value");
        bench.advance(1.0);
        CHECK(dash[0]->value() < 4.1 && dash[0]->value() > 3.9, "after 1 s the BP dial has dropped to 4.0");
        CHECK(dash[1]->value() > 0.9 && dash[1]->value() < 1.1, "and the BC dial has risen to 1.0");
        bench.advance(10.0);
        CHECK(qAbs(dash[0]->value() - bench.brakePressure(EbApplied, 0)) < 0.02, "BP dial lands on the EB value");

        // The EB rule's card is lit, the NB rule's is not.
        int ebCard = -1, nbCard = -1;
        for (int i = 0; i < w.rules().size(); ++i) {
            if (w.rules().at(i).when == QLatin1String("out:emergency_brake")) ebCard = i;
            if (w.rules().at(i).when == QLatin1String("out:normal_brake")) nbCard = i;
        }
        cards.clear();
        for (QFrame *f : w.findChildren<QFrame *>())
            if (f->objectName() == QLatin1String("ruleCard")) cards << f;
        CHECK(ebCard >= 0 && nbCard >= 0 && cards.size() == w.rules().size(), "the EB and NB rules' cards");
        if (ebCard >= 0 && nbCard >= 0 && cards.size() == w.rules().size()) {
            CHECK(cards[ebCard]->property("holds").toBool(), "EB applied: the EB rule's card is lit");
            CHECK(!cards[nbCard]->property("holds").toBool(), "NB released: the NB rule's card is not");
        }

        // A drag on a bench dial moves the air, and the analog card with it.
        emit dash[1]->valueEdited(2.0);
        CHECK(qAbs(currentToPressure(bench.analogMa(1), bench.analogScale(1)).kgcm2 - 2.0) < 0.02, "dragging the BC dial sets BC 2.0");

        bench.setSource(BrakeBenchWindow::Source::Manual);
        bench.advance(0);
        CHECK(dash[0]->target() < 0, "manual: no target marks");
    }
    clearSaved();
#endif
}
