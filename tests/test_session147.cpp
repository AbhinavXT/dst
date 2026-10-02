#include "testutil.h"
#include "layoutaudit.h"

#include "fieldplot.h"
#include "messagedispatcher.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <cmath>

// =============================================================================
//  Session 147 — UI revamp, tool windows 23: Field plot.
//
//  A whole-number field over a wide range is labelled at 1-2-5 steps
//  (ABS_LOCO_LOC read 36121, 72242, 108363, 144484: the range over four).
//  The status counts in English ("588 points from 588 decoded rows", was
//  "point(s)" / "row(s)"); the mouse hint is shorter, one line at 1100 px.
//  Real run: replay/loco_1_1_26062026_162418.cap.
// =============================================================================

namespace {

bool isNiceStep(double step)
{
    if (step <= 0.0) return false;
    const double mag = std::pow(10.0, std::floor(std::log10(step)));
    const double n = step / mag;
    return std::fabs(n - 1.0) < 1e-9 || std::fabs(n - 2.0) < 1e-9 || std::fabs(n - 5.0) < 1e-9;
}

}  // namespace

TEST_SUITE(session147)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- ticks ----------------------------------------------------------------------
    for (int target : { 3, 4, 5, 6 }) {
        const QVector<double> t = niceTicks(-10836.0, 191442.0, target, true);   // ABS_LOCO_LOC, padded
        CHECK(t.size() >= 2 && isNiceStep(t.at(1) - t.at(0)),
              QByteArray("a wide whole-number range steps 1-2-5 (target ") + QByteArray::number(target) + ", step "
                  + QByteArray::number(t.size() > 1 ? t.at(1) - t.at(0) : 0.0) + ")");
        CHECK(t.size() <= target + 2, "and is not over-labelled");
    }
    {
        const QVector<double> t = niceTicks(1.0, 6.0, 5, true);
        CHECK(t.size() == 6 && t.first() == 1.0, "1..6 still reads every value (test_fieldplot's case)");
    }

    // ---- the window, on a real run ----------------------------------------------------
    MessageDispatcher disp;
    QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_26062026_162418.cap"));
    CHECK(run.open(QIODevice::ReadOnly), "fixture: the run");
    while (!run.atEnd()) {
        const QByteArray l = run.readLine().trimmed();
        if (!l.startsWith('@')) continue;
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3) continue;
        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    disp.drainNow();
    FieldPlotWindow w(disp.modelForKey(QStringLiteral("21_1")), QStringLiteral("21_1"));
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.plotField(QStringLiteral("TRAIN_SPEED"));
    CHECK(w.addField(QStringLiteral("ABS_LOCO_LOC")), "two series");
    w.resize(1100, 720);
    w.show();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    auto *status = w.findChild<StatusLine *>();
    const QRegularExpression maybePlural(QStringLiteral("\\w\\((s|es)\\)"));
    CHECK(status && !maybePlural.match(status->text()).hasMatch(),
          QByteArray("the status has no \"(s)\" counts (") + (status ? status->text().toUtf8() : QByteArray()) + ")");
    CHECK(status && status->text().contains(QRegularExpression(QStringLiteral("TRAIN_SPEED: \\d+ points from \\d+ decoded rows"))),
          "\"N points from N decoded rows\"");

    QLabel *hint = nullptr;
    for (QLabel *l : w.findChildren<QLabel *>())
        if (l->text().startsWith(QLatin1String("Wheel: zoom time"))) hint = l;
    CHECK(hint && hint->fontMetrics().horizontalAdvance(hint->text()) <= 1100 - 24,
          "the mouse hint is short enough for one line at 1100 px");

    CHECK(w.minimumSizeHint().width() <= 1040 && w.minimumSizeHint().height() <= 680,
          QByteArray("fits a laptop, with room for wider fonts (minimum ") + QByteArray::number(w.minimumSizeHint().width())
              + " x " + QByteArray::number(w.minimumSizeHint().height()) + ")");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
