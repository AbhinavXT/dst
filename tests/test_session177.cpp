#include "testutil.h"

#include "logmodel.h"
#include "messagedispatcher.h"
#include "radiohealthwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>

// =============================================================================
//  Session 177 — GPS health, in the radio window.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_105200.cap (81_1, 226
//  @ccsys). Expected, from schema/engine.py with the radio spells' rule:
//    GPS-1 satellites 0..12; GPS-2 C/N0 37..44 mostly;
//    6 GPS problem spells, all at the two restarts: 10:52:34 (GPS-1 PPS
//    fail; GPS-2 link+PPS fail to 10:52:44; only GPS-1 active to 10:52:44)
//    and 11:08:44 (the same three, to 11:08:44 / 11:09:04 / 11:09:00);
//    GPS-1 view V in all 226; GPS-2 V 219, A 6, No Data 1 (as reported).
// =============================================================================

namespace {
qint64 at177(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session177)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    MessageDispatcher disp;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
    CHECK(f.open(QIODevice::ReadOnly), "fixture");
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        disp.ingestLocal(81, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
    }
    disp.drainNow();
    LogModel *model = disp.modelForKey(QStringLiteral("81_1"));
    CHECK(model != nullptr, "fixture: the tab");
    if (!model) return;

    const RadioHealth::Report r = RadioHealth::build(model);
    CHECK(r.gpsSats.size() == 2 && r.gpsSats.at(0).ms.size() == 226 && r.gpsCno.size() == 2,
          "satellites and C/N0 for both GPS, from every @ccsys");
    double lo = 1e9, hi = -1e9;
    for (double v : r.gpsSats.at(0).v) { lo = qMin(lo, v); hi = qMax(hi, v); }
    CHECK(lo == 0 && hi == 12, "GPS-1 sees 0 to 12 satellites");
    CHECK(r.gpsProblems.size() == 6, QByteArray("6 GPS problem spells (") + QByteArray::number(r.gpsProblems.size()) + ")");
    bool atRestarts = true, pps = false, both = false;
    for (const RadioHealth::Span &s : r.gpsProblems) {
        atRestarts &= (s.fromMs == at177("10:52:34") && s.toMs <= at177("10:52:44"))
                      || (s.fromMs == at177("11:08:44") && s.toMs <= at177("11:09:04"));
        pps |= s.what == QLatin1String("GPS-1 link ok / PPS fail");
        both |= s.what == QLatin1String("active: GPS-1");
    }
    CHECK(atRestarts && pps && both, "all at the two restarts: a PPS failure, link+PPS, only GPS-1 active");
    bool exact = false;
    for (const RadioHealth::Span &s : r.gpsProblems)
        exact |= s.what == QLatin1String("GPS-2 link+PPS fail") && s.fromMs == at177("11:08:44") && s.toMs == at177("11:09:04");
    for (const RadioHealth::Span &s : r.gpsProblems)
        if (s.what == QLatin1String("active: GPS-1") && s.fromMs == at177("11:08:44")) exact &= s.toMs == at177("11:09:00");
    CHECK(exact, "each spell keeps its own end (GPS-2 to 11:09:04, only GPS-1 active to 11:09:00): trackers sharing one list do not extend each other's spans");
    CHECK(r.gpsViews == QStringLiteral("GPS-1 view: V ×226; GPS-2 view: A ×6, No Data ×1, V ×219"),
          QByteArray("the view letters, counted as reported: ") + r.gpsViews.toUtf8());
    CHECK(RadioHealth::summaryText(r).contains(QStringLiteral("GPS: 6 spell(s)")), "in the summary");

    RadioHealthWindow w(model, QStringLiteral("81_1"), QStringLiteral("81_1"));
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1000, 700);
    w.show();
    CHECK(w.windowTitle().startsWith(QStringLiteral("Radio and GPS health")) && w.canvas()->strips().size() == 8,
          "the window: radio and GPS strips");
    const QString tip = w.canvas()->describeAt(at177("10:52:40"));
    CHECK(tip.contains(QStringLiteral("GPS-2 link+PPS fail")) && tip.contains(QStringLiteral("GPS satellites:")),
          QByteArray("hover at 10:52:40: ") + tip.toUtf8());
    if (!qgetenv("DL_SHOTS").isEmpty()) w.grab().save(QString::fromLocal8Bit(qgetenv("DL_SHOTS")) + QStringLiteral("/radio_gps.png"));
    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700, "fits a laptop");
}
