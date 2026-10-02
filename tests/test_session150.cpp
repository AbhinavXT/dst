#include "testutil.h"
#include "layoutaudit.h"

#include "bignumberpanel.h"
#include "lococonsolewindow.h"
#include "messagedispatcher.h"
#include "settings.h"
#include "theme.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPushButton>
#include <QRegularExpression>

// =============================================================================
//  Session 150 — UI revamp, tool windows 27: Big numbers (and every toggle).
//
//  The Big numbers panel itself, in the Live Loco Console, was sound. Its
//  button was not: a checked push or tool button looked exactly like an
//  unchecked one, app-wide, so "Big numbers", "Cab view", "Record" never
//  showed whether they were on. Now an accent fill with an accent border,
//  as strong as the button text's contrast floor allows, on every theme.
//  Real traffic: replay/loco_1_1_27062026_140226.cap.
// =============================================================================

namespace {

QColor ruleColour(const QString &sheet, const QString &selector)
{
    const QRegularExpression re(QRegularExpression::escape(selector) + QStringLiteral("\\s*\\{\\s*background:(#[0-9a-fA-F]{6})"));
    const QRegularExpressionMatch m = re.match(sheet);
    return m.hasMatch() ? QColor(m.captured(1)) : QColor();
}

double distance(const QColor &a, const QColor &b)
{
    return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue());
}

}  // namespace

TEST_SUITE(session150)
{
    // ---- a checked button looks checked, on every theme ---------------------------
    QStringList faint, unreadable, missing;
    for (Theme t : ThemeUtil::all()) {
        ThemeUtil::apply(t);
        UiStyle::apply();
        const QString sheet = UiStyle::sheet();
        const QColor on = ruleColour(sheet, QStringLiteral("QPushButton:checked, QToolButton:checked"));
        const QColor off = qApp->palette().color(QPalette::Button);
        const QString name = ThemeUtil::toString(t);
        if (!on.isValid()) { missing << name; continue; }
        if (distance(on, off) < 40) faint << name;
        if (UiColor::contrastRatio(qApp->palette().color(QPalette::ButtonText), on) < UiStyle::contrastFloor())
            unreadable << name;
    }
    CHECK(missing.isEmpty(), QByteArray("every theme styles a checked button (") + missing.join(QLatin1String(", ")).toUtf8() + ")");
    CHECK(faint.isEmpty(), QByteArray("and it differs from an unchecked one (") + faint.join(QLatin1String(", ")).toUtf8() + ")");
    CHECK(unreadable.isEmpty(), QByteArray("with its text at the contrast floor (") + unreadable.join(QLatin1String(", ")).toUtf8() + ")");

    // ---- the Loco Console with Big numbers on -------------------------------------
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    MessageDispatcher disp;
    LocoConsoleWindow w(&disp, nullptr);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1100, 720);
    w.show();
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    CHECK(f.open(QIODevice::ReadOnly), "fixture: real traffic");
    qint64 ms = 1782558147000LL;
    int n = 0;
    while (!f.atEnd() && n < 400) {
        const QByteArray l = f.readLine().trimmed();
        if (!l.startsWith('@')) continue;
        disp.ingestLocal(21, 1, l, ms + n * 137, QString());
        ++n;
    }
    disp.drainNow();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    QPushButton *big = nullptr;
    for (QPushButton *b : w.findChildren<QPushButton *>())
        if (b->text() == QLatin1String("Big numbers")) big = b;
    CHECK(big && big->isCheckable(), "fixture: the Big numbers toggle");
    if (big && !big->isChecked()) big->click();
    {
        // The tiles refresh on the console's timer.
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 1500) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    auto *panel = w.findChild<BigNumberPanel *>();
    CHECK(panel && panel->isVisible() && big && big->isChecked(), "the panel shows, and its button is checked");
    if (panel) {
        CHECK(panel->tiles().size() >= 4, "the default tiles");
        CHECK(panel->valueTextAt(1).contains(QLatin1String("Trip")), QByteArray("the mode tile reads the real run (") + panel->valueTextAt(1).toUtf8() + ")");
    }
    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700, "fits a laptop with the tiles shown");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
