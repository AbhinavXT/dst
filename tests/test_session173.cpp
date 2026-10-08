#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "radiohealthwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <cmath>

// =============================================================================
//  Session 173 — radio health.
//  Real frames: replay/2026-10-08/loco_1_1_08102026_105200.cap (81_1,
//  10:52-11:13). Expected, from schema/engine.py with the same spell rule
//  (a silence over 10 s between frames ends a spell):
//    747 @dmi; no radio 10:52:00-10:52:06, 10:52:20-10:52:40,
//    10:56:34-11:08:23, 11:08:37-11:08:45; no radio-hole announcement;
//    10 radio-not-OK spells, all at 10:52:34-39 and 11:08:44-45 (the radios
//    going down before each restart); radio 1 at 36-37 °C over 226 @ccsys;
//    110 @dlsys, GSM-1 RSSI 99 (not known) throughout.
//  No capture holds "Approaching Radio Hole", so the matching rule is
//  checked on spans set by hand (no bytes invented).
// =============================================================================

namespace {
qint64 at173(const char *hms)
{
    return QDateTime::fromString(QStringLiteral("2026-10-08T") + QLatin1String(hms), Qt::ISODate).toMSecsSinceEpoch();
}
}  // namespace

TEST_SUITE(session173)
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
    CHECK(r.signal.ms.size() == 747, QByteArray("every @dmi's signal bars (") + QByteArray::number(r.signal.ms.size()) + ")");
    const QVector<QPair<const char *, const char *>> none{ { "10:52:00", "10:52:06" }, { "10:52:20", "10:52:40" },
                                                          { "10:56:34", "11:08:23" }, { "11:08:37", "11:08:45" } };
    bool spells = r.noRadio.size() == none.size();
    for (int i = 0; spells && i < none.size(); ++i)
        spells = r.noRadio.at(i).fromMs == at173(none.at(i).first) && r.noRadio.at(i).toMs == at173(none.at(i).second);
    CHECK(spells, QByteArray("the four no-radio spells, as engine.py reads them (") + QByteArray::number(r.noRadio.size()) + ")");
    CHECK(r.longestNoRadioMs == at173("11:08:23") - at173("10:56:34"), "the longest: 11 min 49 s");
    CHECK(r.radioHole.isEmpty() && r.announcedSpells == 0, "no radio hole was announced, so none of them is 'announced'");
    CHECK(r.radioFail.size() == 10, QByteArray("10 radio-not-OK spells (") + QByteArray::number(r.radioFail.size()) + ")");
    bool atRestarts = true;
    for (const RadioHealth::Span &s : r.radioFail)
        atRestarts &= (s.fromMs >= at173("10:52:34") && s.toMs <= at173("10:52:39"))
                      || (s.fromMs >= at173("11:08:44") && s.toMs <= at173("11:08:45"));
    CHECK(atRestarts, "all at the radios going down before the two restarts (10:52:34-39, 11:08:44-45)");
    CHECK(r.temperatures.size() == 4 && r.temperatures.at(0).ms.size() == 226, "radio and PA temperatures from every @ccsys");
    double lo = 1e9, hi = -1e9;
    for (double v : r.temperatures.at(0).v) { lo = qMin(lo, v); hi = qMax(hi, v); }
    CHECK(lo == 36 && hi == 37, "radio 1 at 36-37 °C (the display value's number)");
    bool gsm1Unknown = r.gsm.size() == 2 && r.gsm.at(0).v.size() == 110;
    for (double v : r.gsm.at(0).v) gsm1Unknown &= std::isnan(v);
    CHECK(gsm1Unknown && !std::isnan(r.gsm.at(1).v.first()), "GSM-1 RSSI 99 is 'not known'; GSM-2 is a number");
    const QString text = RadioHealth::summaryText(r);
    CHECK(text.contains(QStringLiteral("No radio on the DMI in 4 spell(s)")) && text.contains(QStringLiteral("0 of them followed a radio-hole announcement"))
              && text.contains(QStringLiteral("4 did not")),
          QByteArray("the summary: ") + text.toUtf8());

    // The rule, on spans set by hand.
    {
        RadioHealth::Report h;
        h.noRadio = { { 1000000, 1060000, QString(), false }, { 2000000, 2010000, QString(), false }, { 3000000, 3030000, QString(), false } };
        h.radioHole = { { 1000000 - RadioHealth::kRadioHoleLeadMs, 1000000 - RadioHealth::kRadioHoleLeadMs + 500, QString(), false },
                        { 3010000, 3020000, QString(), false } };
        RadioHealth::matchAnnouncements(h);
        CHECK(h.noRadio.at(0).announced && !h.noRadio.at(1).announced && h.noRadio.at(2).announced && h.announcedSpells == 2,
              "announced: one ending exactly the lead time before the spell, one during it; not one with none near");
    }

    // The window.
    RadioHealthWindow w(model, QStringLiteral("81_1"), QStringLiteral("81_1"));
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1000, 640);
    w.show();
    CHECK(w.summary() == text && w.canvas()->strips().size() == 5, "the window: the summary over five strips");
    if (!qgetenv("DL_SHOTS").isEmpty()) w.grab().save(QString::fromLocal8Bit(qgetenv("DL_SHOTS")) + QStringLiteral("/radio_health.png"));
    const QString tip = w.canvas()->describeAt(at173("11:00:00"));
    CHECK(tip.contains(QStringLiteral("DMI signal: 0 bars")) && tip.contains(QStringLiteral("no radio hole announced"))
              && tip.contains(QStringLiteral("Temperature: Radio 1 3")),
          QByteArray("hovering 11:00:00: ") + tip.toUtf8());
    qint64 jumped = -1;
    QObject::connect(&w, &RadioHealthWindow::jumpRequested, [&jumped](const QString &, qint64 ms) { jumped = ms; });
    emit w.canvas()->timeClicked(at173("11:00:00"));
    CHECK(jumped == at173("11:00:00"), "a click jumps the log there");
    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700, "fits a laptop");
}
