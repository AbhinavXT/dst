#include "testutil.h"

#include "capturedecoder.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "settings.h"
#include "theme.h"
#include "uistyle.h"
#include "watchlist.h"
#include "watchpanel.h"
#include "watchrules.h"

#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QToolButton>

#include <cstdio>

// =============================================================================
//  Session 161 — ready-made watches.
//
//  Every rule runs over the real captures in replay/, through the real
//  dispatcher, and its match count must equal an independent count taken
//  from the decoder's own rendered rows (CaptureDecoder::describe) — so a
//  rule that parses but matches the wrong thing, or nothing, fails here.
// =============================================================================

namespace {

// The rendered row named `field` (prefix, as the query matches), or empty.
QString rowValue(const QVector<FieldRow> &rows, const QString &field)
{
    for (const FieldRow &r : rows)
        if (r.field.trimmed().startsWith(field, Qt::CaseInsensitive)) return r.value.trimmed();
    return QString();
}

// What the rule should match, decided from the decoder's rows alone.
bool expected(const QString &id, const CaptureLine &cap, const QVector<FieldRow> &rows)
{
    if (id == QLatin1String("eb"))  return cap.type == CapType::Dmi && rowValue(rows, QStringLiteral("brake_type")).startsWith(QLatin1String("4 "));
    if (id == QLatin1String("fsb")) return cap.type == CapType::Dmi && rowValue(rows, QStringLiteral("brake_type")).startsWith(QLatin1String("3 "));
    if (id == QLatin1String("sos")) return cap.typeToken == QLatin1String("lsos");
    if (id == QLatin1String("tsr")) return rowValue(rows, QStringLiteral("TSR_STATUS")).startsWith(QLatin1String("2 "));
    if (id == QLatin1String("trip"))      return rowValue(rows, QStringLiteral("LOCO_MODE")).startsWith(QLatin1String("7 "));
    if (id == QLatin1String("sysfail"))   return rowValue(rows, QStringLiteral("LOCO_MODE")).startsWith(QLatin1String("12 "));
    if (id == QLatin1String("isolation")) return rowValue(rows, QStringLiteral("LOCO_MODE")).startsWith(QLatin1String("13 "));
    if (id == QLatin1String("nokeys")) return cap.typeToken == QLatin1String("nmshlth")    // session 179
                                              && rowValue(rows, QStringLiteral("REMAINING_KEY_NUMBERS")).startsWith(QLatin1String("0 "));
    if (id == QLatin1String("crc"))  return (cap.type == CapType::Dmi || cap.typeToken == QLatin1String("rfid"))
                                            && cap.crcChecked && !cap.crcOk;
    return false;
}

}  // namespace

TEST_SUITE(session161rules)
{
    const QVector<WatchRule> &rules = WatchRules::all();
    CHECK(rules.size() >= 8, "the library holds the rules");

    // Each rule parses as a watch.
    for (const WatchRule &r : rules) {
        WatchList one;
        one.add(r.expr, r.label);
        CHECK(one.watches().first().valid(),
              QByteArray("rule parses: ") + r.id.toUtf8() + " — " + one.watches().first().parseError.toUtf8());
    }

    // Captures from replay/, through the dispatcher, one tab each, until
    // every rule that real frames can show has been seen at least once.
    const QStringList proven{ QStringLiteral("eb"), QStringLiteral("fsb"), QStringLiteral("trip"),
                              QStringLiteral("sysfail"), QStringLiteral("crc") };
    MessageDispatcher disp;
    const QStringList files = QDir(QStringLiteral(DL_SRC_DIR "/replay")).entryList({ QStringLiteral("*.cap") }, QDir::Files, QDir::Name);
    CHECK(files.size() >= 5, "fixture: the replay corpus");
    QHash<QString, int> want, got;
    WatchList list;
    for (const WatchRule &r : rules) list.add(r.expr, r.label);
    int frames = 0, fileNo = 0;
    for (const QString &name : files) {
        QFile f(QStringLiteral(DL_SRC_DIR "/replay/") + name);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const int src = 40 + fileNo++;
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            if (!l.startsWith('@')) continue;
            const QList<QByteArray> tok = l.split(' ');
            if (tok.size() < 3) continue;
            disp.ingestLocal(src, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
        }
        disp.drainNow();
        const QString key = QStringLiteral("%1_1").arg(src);
        LogModel *m = disp.modelForKey(key);
        for (int row = 0; m && row < m->count(); ++row) {
            const LogEntryPtr e = m->entryAt(row);
            const CaptureLine cap = CaptureDecoder::parseLine(e->text);
            if (!cap.valid) continue;
            ++frames;
            const QVector<FieldRow> rows = CaptureDecoder::describe(cap, nullptr, 0, nullptr);
            for (const WatchRule &r : rules)
                if (expected(r.id, cap, rows)) ++want[r.id];
            list.observe(e, key);
        }
        bool all = true;
        for (const QString &id : proven) all = all && want.value(id) > 0;
        if (all) break;
    }
    CHECK(frames > 1000, QByteArray("fixture: frames decoded (") + QByteArray::number(frames) + ")");
    for (int i = 0; i < rules.size(); ++i) got[rules.at(i).id] = list.watches().at(i).matches;
    for (const QString &id : proven)
        CHECK(want.value(id) > 0, QByteArray("real frames show '") + id.toUtf8() + "' (so its check below is not vacuous)");

    for (const WatchRule &r : rules) {
        std::printf("    %-10s matches %6d, decoder says %6d\n", qPrintable(r.id), got.value(r.id), want.value(r.id));
        CHECK(got.value(r.id) == want.value(r.id),
              QByteArray("rule '") + r.id.toUtf8() + "' matches exactly the frames the decoder's rows say");
    }
}

// =============================================================================
TEST_SUITE(session161panel)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    const QStringList saved = Settings::watches();
    Settings::setWatches(QStringList());

    WatchPanel panel;
    QToolButton *btn = panel.findChild<QToolButton *>(QStringLiteral("watchReadyMade"));
    CHECK(btn && btn->menu() == panel.readyMadeMenu(), "a Ready-made button beside Watch, with its menu");

    emit panel.readyMadeMenu()->aboutToShow();
    QAction *eb = panel.readyMadeMenu()->findChild<QAction *>(QStringLiteral("watchRule_eb"));
    CHECK(eb && eb->isCheckable() && !eb->isChecked(), "EB applied is listed, unticked");
    CHECK(eb && eb->toolTip().contains(QStringLiteral("@dmi field:brake_type=4")), "its tooltip shows the exact condition");

    if (eb) eb->setChecked(true);           // what a click does
    CHECK(panel.ruleOn(QStringLiteral("eb")) && panel.list().count() == 1
              && panel.list().watches().first().label == QStringLiteral("EB applied")
              && panel.list().watches().first().expr == QStringLiteral("@dmi field:brake_type=4"),
          "ticked: an ordinary watch, named, with the rule's condition");
    CHECK(Settings::watches().size() == 1, "and saved like any watch");
    CHECK(!panel.setRuleOn(QStringLiteral("eb"), true) && panel.list().count() == 1, "ticked twice: still one watch");

    // Typed by hand, the same condition counts as the rule being on.
    panel.list().add(QStringLiteral("field:LOCO_MODE=7"), QStringLiteral("mine"));
    emit panel.readyMadeMenu()->aboutToShow();
    QAction *trip = panel.readyMadeMenu()->findChild<QAction *>(QStringLiteral("watchRule_trip"));
    CHECK(trip && trip->isChecked(), "a watch with the rule's condition shows the rule ticked");

    QAction *ebAgain = panel.readyMadeMenu()->findChild<QAction *>(QStringLiteral("watchRule_eb"));
    if (ebAgain) ebAgain->setChecked(false);
    CHECK(!panel.ruleOn(QStringLiteral("eb")) && panel.list().count() == 1, "unticked: that watch is removed, the other stays");

    bool note = false;
    for (QAction *a : panel.readyMadeMenu()->actions())
        if (!a->isEnabled() && a->text().contains(QStringLiteral("Mode changes and frame age"))) note = true;
    CHECK(note, "the menu says what a watch cannot do (mode changes, frame age)");
    CHECK(!panel.setRuleOn(QStringLiteral("no-such-rule"), true), "an unknown rule id does nothing");

    Settings::setWatches(saved);
}
