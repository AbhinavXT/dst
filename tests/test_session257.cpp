#include "testutil.h"

#include "flasherwindow.h"
#include "laneband.h"
#include "lococonfigwindow.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "portedit.h"
#include "runreport.h"
#include "uicolors.h"
#include "theme.h"
#include "uistyle.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFrame>
#include <QImage>
#include <QTemporaryDir>
#include <QValidator>
#include <QVBoxLayout>

// =============================================================================
//  Session 257 — three faults seen on the bench PC (Windows 11, Qt 6.9).
//  1. Port boxes drawn empty: QSpinBox with NoButtons in 80 px under the
//     windows11 style. Now PortEdit, a validated QLineEdit.
//  2. Dark -> Light left the Flasher's app bar and detail strip (and Loco
//     Configuration's bar) dark: they coloured themselves from palette()
//     before the new stylesheet was in. UiStyle::apply() tells them again.
//  3. The Link lane: a "no keys" spell running across a traffic gap was
//     painted over the gap and cut its label. The gap is drawn on top now.
//     Real frames: replay/2026-10-08/loco_1_1_08102026_105200.cap with
//     10:52:48 - 10:56:27 cut out (a VCC restart's silence).
// =============================================================================

namespace {
qint64 at257(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session257)
{
    // ---- 1. PortEdit ---------------------------------------------------------
    {
        PortEdit pe;
        int emitted = 0, last = 0;
        QObject::connect(&pe, &PortEdit::valueChanged, [&](int v) { ++emitted; last = v; });
        pe.setValue(50001);
        CHECK(pe.value() == 50001 && pe.text() == QLatin1String("50001") && emitted == 1 && last == 50001,
              "setValue shows the number and says so once");
        pe.setValue(0);
        pe.setValue(70000);
        CHECK(pe.value() == 50001 && emitted == 1, "out of range is not taken");
        pe.setText(QStringLiteral("9"));
        emit pe.textEdited(QStringLiteral("9"));
        CHECK(pe.value() == 9 && last == 9 && emitted == 2, "typing a port changes the value");
        pe.setText(QString());
        emit pe.textEdited(QString());
        CHECK(pe.value() == 9 && emitted == 2, "an empty box keeps the last good port");
        emit pe.editingFinished();
        CHECK(pe.text() == QLatin1String("9"), "and shows it again when editing finishes");
        CHECK(!pe.hasAcceptableInput() || pe.validator() != nullptr, "a validator limits what can be typed");
        QString s = QStringLiteral("65536");
        int pos = 0;
        CHECK(pe.validator()->validate(s, pos) != QValidator::Acceptable, "65536 is not a port");
    }

    // ---- 2. The bars follow a theme change, and the ports are line edits ------
    {
        ThemeUtil::apply(Theme::Dark);
        UiStyle::apply();
        QTemporaryDir temp;
        FlasherWindow flasher(nullptr, temp.path());
        flasher.resize(1100, 720);
        flasher.show();
        LocoConfigWindow loco(nullptr, temp.path());
        loco.setAttribute(Qt::WA_DeleteOnClose, false);
        loco.resize(1100, 720);
        loco.show();
        QCoreApplication::processEvents();

        const QList<PortEdit *> ports = loco.findChildren<PortEdit *>();
        bool shown = ports.size() == 4;
        for (PortEdit *p : ports) shown = shown && p->text() == QString::number(p->value()) && p->value() > 0;
        CHECK(shown, QByteArray("Loco Configuration: four port boxes holding their port (") + QByteArray::number(ports.size()) + ")");
        CHECK(flasher.findChildren<PortEdit *>().size() == 1, "the Flasher: one port box");

        ThemeUtil::apply(Theme::Light);
        UiStyle::apply();
        QCoreApplication::processEvents();
        const QString lightAlt = qApp->palette().color(QPalette::AlternateBase).name();
        auto *bar = flasher.findChild<QFrame *>(QStringLiteral("flasherAppBar"));
        auto *strip = flasher.findChild<QFrame *>(QStringLiteral("flasherDetailStrip"));
        CHECK(bar && bar->styleSheet().contains(lightAlt),
              QByteArray("after Dark -> Light the Flasher's bar is the light colour ") + lightAlt.toUtf8());
        CHECK(strip && strip->styleSheet().contains(lightAlt), "and so is the detail strip");
        // The bar as drawn: light, not the dark theme's.
        if (bar) {
            const QImage img = bar->grab().toImage();
            const QColor px = img.pixelColor(img.width() / 2, 2);
            CHECK(px.lightness() > 160, QByteArray("the bar is drawn light: ") + px.name().toUtf8());
        }
    }

    // ---- 3. A gap over a no-keys spell ----------------------------------------
    {
        ThemeUtil::apply(Theme::Light);
        UiStyle::apply();
        MessageDispatcher disp;
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
        CHECK(f.open(QIODevice::ReadOnly), "fixture");
        const qint64 cutFrom = at257("10:52:48"), cutTo = at257("10:56:28"), end = at257("10:57:00");
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3 || !l.startsWith('@')) continue;
            const qint64 ms = QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch();
            if ((ms >= cutFrom && ms < cutTo) || ms >= end) continue;
            disp.ingestLocal(81, 1, l, ms, QString());
        }
        disp.drainNow();
        LogModel *model = disp.modelForKey(QStringLiteral("81_1"));
        CHECK(model != nullptr, "fixture: the tab");
        if (!model) return;

        const RunReport::Summary sum = RunReport::summarise(model, QStringLiteral("81_1"), QString());
        bool spans = false;
        for (const RunReport::Episode &k : sum.noKeys) spans = spans || (k.fromMs < cutFrom && k.toMs >= cutTo);
        CHECK(spans, "fixture: a no-keys spell runs across the gap");

        QWidget host;
        auto *lay = new QVBoxLayout(&host);
        auto *band = new LaneBand;
        lay->addWidget(band);
        host.resize(1200, 300);
        host.show();
        band->setModel(model, QStringLiteral("81_1"), QStringLiteral("81_1"));
        QCoreApplication::processEvents();
        const QRect r = band->laneRect(3);
        auto xAt = [&](qint64 ms) {
            return r.left() + int(double(ms - band->fromMs()) / double(qMax<qint64>(1, band->toMs() - band->fromMs())) * r.width());
        };
        const int x = xAt(at257("10:54:30"));
        const QString tip = band->describeAt(QPoint(x, r.top() + 2));
        CHECK(tip.startsWith(QStringLiteral("No traffic")), QByteArray("inside the gap the Link lane says no traffic: ") + tip.toUtf8());
        const QImage img = band->grab().toImage();
        const qreal dpr = img.devicePixelRatio();
        const QColor top = img.pixelColor(int(x * dpr), int((r.top() + 3) * dpr));
        const QColor warn = UiColor::warning();
        const int diff = qAbs(top.red() - warn.red()) + qAbs(top.green() - warn.green()) + qAbs(top.blue() - warn.blue());
        CHECK(diff < 30, QByteArray("and the top half there is the gap's colour, not the no-keys bar: ") + top.name().toUtf8());
    }
}
