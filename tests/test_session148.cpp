#include "testutil.h"
#include "layoutaudit.h"

#include "messagedispatcher.h"
#include "speeddistance.h"
#include "statusline.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QRegularExpression>

// =============================================================================
//  Session 148 — UI revamp, tool windows 24: Speed/distance.
//
//  0 m is "location not known" here too, as in the track diagram (131), the
//  two-loco view (132) and the incident report (133); Abhinav's decision,
//  asked again for this window. On the real run the 0 m samples stretched
//  the axis from 0 km and squeezed the run (153-161 km) against its far
//  end. SpeedDistance::knownOnly() now does it for both this window and
//  the incident report, recomputing span, direction and targets from the
//  known samples. Counts in English. Real run:
//  replay/loco_1_1_26062026_162418.cap.
// =============================================================================

TEST_SUITE(session148)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

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
    LogModel *model = disp.modelForKey(QStringLiteral("21_1"));

    // ---- knownOnly ------------------------------------------------------------------
    const SpeedDistance::Trace all = SpeedDistance::extract(model);
    int unknownInAll = 0;
    for (const SpeedDistance::Sample &s : all.samples) unknownInAll += SpeedDistance::locationKnown(s) ? 0 : 1;
    CHECK(all.source == QLatin1String("dmi") && unknownInAll > 0 && all.minLocM == 0.0,
          "fixture: the run has 0 m samples, and extract() keeps them (its other callers count them)");
    int dropped = -1;
    const SpeedDistance::Trace known = SpeedDistance::knownOnly(all, &dropped);
    CHECK(dropped == unknownInAll && known.samples.size() + dropped == all.samples.size(),
          "knownOnly() drops exactly the 0 m samples and says how many");
    bool anyZero = false;
    for (const SpeedDistance::Sample &s : known.samples) anyZero = anyZero || !SpeedDistance::locationKnown(s);
    CHECK(!anyZero, "none left");
    CHECK(known.minLocM > 150000.0 && known.maxLocM < 162000.0,
          QByteArray("the span is the run's, not from 0 (") + QByteArray::number(known.minLocM) + " - "
              + QByteArray::number(known.maxLocM) + ")");
    bool targetNearZero = false;
    for (const SpeedDistance::Target &t : known.targets) targetNearZero = targetNearZero || t.locM < 100000.0;
    CHECK(!targetNearZero, "no target placed relative to a 0 m sample");
    CHECK(!known.targets.isEmpty(), "the real targets are still there");

    // ---- the window ---------------------------------------------------------------------
    SpeedDistanceWindow w(model, QStringLiteral("21_1"));
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1100, 720);
    w.show();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
    CHECK(w.canvas()->trace().minLocM > 150000.0, "the plot's axis starts at the run, not at 0 km");
    CHECK(w.canvas()->viewFromM() > 150000.0, "and so does the view");

    auto *status = w.findChild<StatusLine *>();
    const QString st = status ? status->text() : QString();
    CHECK(st.contains(QStringLiteral("%1 samples at 0 m (location not known) left off").arg(dropped)),
          QByteArray("the status says how many were left off (") + st.toUtf8() + ")");
    CHECK(!st.contains(QRegularExpression(QStringLiteral("\\w\\((s|es)\\)"))), "counts in English");

    QLabel *hint = nullptr;
    for (QLabel *l : w.findChildren<QLabel *>())
        if (l->text().startsWith(QLatin1String("Wheel: zoom distance"))) hint = l;
    // One line at 1100 px with the Mac's and Windows' fonts; Linux's are
    // wider and it wraps there, which is allowed: what matters is that no
    // line is cut off.
    CHECK(hint && hint->height() >= hint->heightForWidth(hint->width()),
          "the mouse hint is shown whole, wrapped or not");
    CHECK(w.minimumSizeHint().width() <= 1040 && w.minimumSizeHint().height() <= 680, "fits a laptop with room for wider fonts");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
