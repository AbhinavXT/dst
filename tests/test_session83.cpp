#include "testutil.h"

#include "cabpanel.h"
#include "capturedecoder.h"
#include "dmipanel.h"
#include "livefields.h"
#include "lococonsolewindow.h"
#include "settings.h"
#include "theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTest>

// =============================================================================
//  Session 83: the DMI (LP-OCIP) window, laid out per RDSO/SPN/196/2020
//  Annexure-B Amdt-3, fed from REAL @dmi frames in replay/; and fields the
//  operator adds to the cab view.
// =============================================================================

namespace {

QStringList realDmi(int limit)
{
    QStringList out;
    const QDir dir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"));
    for (const QString &f : dir.entryList({ QStringLiteral("loco_*.cap") }, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        while (!file.atEnd() && out.size() < limit) {
            const QString l = QString::fromLatin1(file.readLine()).trimmed();
            if (l.startsWith(QLatin1String("@dmi_"))) out << l;
        }
    }
    return out;
}

// How much of the panel a frame exercises.
int richness(const DmiState &s)
{
    return (s.speed > 0) * 4 + (s.targetDistance > 0) * 3 + (s.aspect != 0) * 3 + (s.movementAuthority > 0) * 2
         + (!s.systemMessages.isEmpty()) * 2 + (!s.contextMessages.isEmpty()) + (!s.signalName.isEmpty()) * 2
         + (s.tagId > 0) + (s.rfBars > 0);
}

QString shotDir() { return QString::fromLocal8Bit(qgetenv("DL_SHOTS")); }

}  // namespace

TEST_SUITE(session83)
{
    // ---- the annexure's scales and lamps -------------------------------------------
    CHECK(dmiTargetScaleY(0) == 337 && dmiTargetScaleY(250) == 272 && dmiTargetScaleY(500) == 207
              && dmiTargetScaleY(1000) == 157 && dmiTargetScaleY(2000) == 107 && dmiTargetScaleY(5000) == 107,
          "A1: 0/250/500/1000/2000 m at the annexure's 337/272/207/157/107, capped at 2000 m");
    CHECK(dmiMaScaleY(0) == 342 && dmiMaScaleY(100) == 308 && dmiMaScaleY(250) == 259 && dmiMaScaleY(3000) == 77
              && dmiMaScaleY(3001) == 45,
          "C1: 0/100/250/3000 m at 342/308/259/77, and beyond 3000 m at +++ (45)");
    CHECK(dmiLitLamps(1) == QVector<int>{ 3 } && dmiLitLamps(2) == QVector<int>{ 2 } && dmiLitLamps(10) == QVector<int>({ 0, 2 })
              && dmiLitLamps(11) == QVector<int>{ 1 } && dmiLitLamps(0).isEmpty(),
          "D1 lamps (YLW, GRN, YLW, RED): red, yellow, double yellow, green, none");
    CHECK(dmiModeName(6) == QLatin1String("On Sight Mode") && dmiModeName(4) == QLatin1String("Full Supervision Mode"),
          "G: the mode in words, as Annexure-A1 names it");

    // ---- messages: the flag names with their numbers filled in --------------------
    {
        QHash<QString, qint64> raw{ { "target_distance", 60 }, { "collision_loco_id", 23456 }, { "collision_loco_distance", 812 },
                                    { "override_timeout", 45 }, { "fsb_applied_time", 8 }, { "sos_station_id", 1201 } };
        QHash<QString, QString> text{ { "train_type", "2 (GOODS_TRAIN)" } };
        CHECK(dmiSystemMessage(QStringLiteral("End of Authority"), raw, text) == QLatin1String("End of Authority in 60m"),
              "H: End of Authority in 60m (the photo's message)");
        CHECK(dmiSystemMessage(QStringLiteral("Head-On Collision with Loco"), raw, text)
                  == QLatin1String("Head On Collision with Loco 23456 in 0812 m"), "H: collision with loco id and distance");
        CHECK(dmiSystemMessage(QStringLiteral("Override Selected, Pass Signal"), raw, text)
                  == QLatin1String("Override selected, Pass Signal in 45s"), "H: override countdown");
        CHECK(dmiSystemMessage(QStringLiteral("KAVACH Territory Entry"), raw, text) == QLatin1String("KAVACH Territory Entry"),
              "H: a message with no number is shown as it is");
        CHECK(dmiContextMessage(QStringLiteral("FSB will be Applied in XX s"), raw, text) == QLatin1String("FSB will be applied in 8S"),
              "I: FSB countdown");
        CHECK(dmiContextMessage(QStringLiteral("SOS - Station (This Loco)"), raw, text).contains(QLatin1String("1201")),
              "I: SOS names the station");
        CHECK(dmiContextMessage(QStringLiteral("Train Type Selected"), raw, text) == QLatin1String("GOODS TRAIN Train Type selected"),
              "I: the train type in words");
    }

    // ---- real frames -------------------------------------------------------------------
    const QStringList frames = realDmi(12000);
    CHECK(frames.size() > 1000, QByteArray("real @dmi frames in replay/: ") + QByteArray::number(frames.size()));
    if (frames.isEmpty()) return;
    DmiState best;
    QString bestLine;
    int decoded = 0, withMessages = 0;
    for (const QString &l : frames) {
        const DmiState s = dmiStateFromLine(l);
        if (!s.valid) continue;
        ++decoded;
        if (!s.systemMessages.isEmpty()) ++withMessages;
        if (richness(s) > richness(best)) { best = s; bestLine = l; }
    }
    CHECK(decoded == frames.size(), "every real @dmi frame gives a panel state");
    CHECK(withMessages > 0, "some carry system messages");
    {
        QHash<QString, qint64> raw;
        CaptureDecoder::describe(CaptureDecoder::parseLine(bestLine), nullptr, 0, &raw);
        CHECK(best.speed == raw.value("train_speed") && best.permitted == raw.value("speed_limit_permissible")
                  && best.targetDistance == raw.value("target_distance") && best.aspect == raw.value("current_sig_aspect")
                  && best.movementAuthority == raw.value("ma_w_r_t_sig") && best.trainLength == raw.value("train_length"),
              "each region shows its own field of the frame");
        CHECK(!best.date.isEmpty() && best.date.contains(QLatin1Char('-')) && best.time.count(QLatin1Char(':')) == 2,
              QByteArray("B5/B6 date and time as DD-Mmm-YYYY HH:MM:SS: ") + best.date.toUtf8() + " " + best.time.toUtf8());
        CHECK(best.dc.startsWith(QLatin1String("DC ")), "E: DC X.XX");
        CHECK(!best.modeText.isEmpty(), QByteArray("G: ") + best.modeText.toUtf8());
    }

    // ---- the window ---------------------------------------------------------------------
    {
        DmiWindow w(nullptr);
        w.resize(880, 720);
        w.show();
        CHECK(!w.view()->state().valid, "before any @dmi: waiting");
        w.observeLine(QStringLiteral("1_1"), bestLine);
        QTest::qWait(20);
        CHECK(w.view()->state().valid && w.selectedSource() == QLatin1String("1_1"), "a frame arrives: the panel shows it");
        CHECK(w.view()->state().speed == best.speed, "the view shows that frame");
        auto *annexure = w.findChild<QCheckBox *>(QStringLiteral("dmiAnnexureColours"));
        CHECK(annexure != nullptr, "there is an Annexure-B colours switch");
        const bool was = annexure && annexure->isChecked();
        if (annexure) annexure->setChecked(false);
        CHECK(!w.view()->annexureColours(), "by default the panel follows the theme");
        const DmiColours spec = DmiColours::annexure();
        CHECK(spec.lor == QColor(255, 165, 0) && spec.lbl == QColor(0, 139, 206) && spec.lgr == QColor(128, 255, 0)
                  && spec.org == QColor(255, 128, 64),
              "Annexure-B colours are Table B.2's RGB");
        // On a light theme every Annexure hue is moved to be visible.
        const QPalette saved = qApp->palette();
        const int savedTheme = UiColor::activeTheme();
        ThemeUtil::apply(Theme::Light);
        const DmiColours light = DmiColours::forTheme(qApp->palette());
        double worst = 99;
        for (const QColor &x : { light.lbl, light.ylw, light.lor, light.org, light.brd, light.lgr, light.grn, light.dgr })
            worst = qMin(worst, UiColor::contrastRatio(x, light.bg));
        CHECK(worst >= 3.0, QByteArray("theme colours: every Annexure hue at least 3:1 on the light theme (worst ")
                                + QByteArray::number(worst, 'f', 2) + ")");
        if (!shotDir().isEmpty()) {
            w.view()->update();
            QTest::qWait(30);
            w.grab().save(QDir(shotDir()).filePath(QStringLiteral("dmi_light.png")));
        }
        ThemeUtil::apply(Theme::Dark);
        if (annexure) annexure->setChecked(true);
        CHECK(w.view()->annexureColours(), "the switch gives the annexure's own colours");
        if (!shotDir().isEmpty()) {
            QTest::qWait(30);
            w.grab().save(QDir(shotDir()).filePath(QStringLiteral("dmi_annexure.png")));
        }
        qApp->setPalette(saved);
        UiColor::setActiveTheme(savedTheme);
        if (annexure) annexure->setChecked(was);
        const QString png = QDir::temp().filePath(QStringLiteral("dmi_test.png"));
        CHECK(w.saveImage(png) && QFile(png).size() > 1000, "the panel saves as an image");
        QFile::remove(png);
        QString other = frames.first();
        other.replace(QLatin1String("@dmi_1_1"), QLatin1String("@dmi_2_1"));
        w.observeLine(QStringLiteral("2_1"), other);
        CHECK(w.findChild<QComboBox *>(QStringLiteral("dmiSource"))->count() >= 2, "each loco heard is offered");
    }

    // ---- fields added to the cab view -----------------------------------------------------
    {
        QSettings s(Settings::iniPath(), QSettings::IniFormat);
        const QVariant was = s.value(QStringLiteral("lococonsole/cabFields"));
        const QVariant cabWas = s.value(QStringLiteral("lococonsole/cabShown"));
        s.remove(QStringLiteral("lococonsole/cabFields"));
        s.setValue(QStringLiteral("lococonsole/cabShown"), true);
        {
            LocoConsoleWindow window(nullptr);
            window.show();
            LiveFieldRef ref;
            ref.label = QStringLiteral("train_length");
            ref.sources = QStringList{ QStringLiteral("dmi:train_length") };
            CHECK(window.addCabField(ref), "a field is added to the cab view");
            CHECK(!window.addCabField(ref), "the same field twice is refused");
            LiveFieldRef dc;
            dc.label = QStringLiteral("deceleration_constant");
            dc.sources = QStringList{ QStringLiteral("dmi:deceleration_constant") };
            window.addCabField(dc);
            LogEntryPtr e(new LogEntry);
            e->text = bestLine;
            e->epochMs = QDateTime::currentMSecsSinceEpoch();
            QMetaObject::invokeMethod(&window, "onEntryAppended", Q_ARG(QString, QStringLiteral("1_1")), Q_ARG(LogEntryPtr, e));
            QMetaObject::invokeMethod(&window, "onRefreshTick");
            const QVector<CabDisplay::Extra> extras = window.cabDisplay()->extras();
            CHECK(extras.size() == 2 && extras.at(0).value == QStringLiteral("%1 m").arg(best.trainLength) && !extras.at(0).stale,
                  "the cab shows the added fields' live values");
            CHECK(extras.at(1).value == best.dc, "each from its own field");
            if (!shotDir().isEmpty()) {
                window.resize(1300, 820);
                QTest::qWait(40);
                window.grab().save(QDir(shotDir()).filePath(QStringLiteral("cab_fields.png")));
            }
        }
        {
            LocoConsoleWindow again(nullptr);
            CHECK(again.cabFields().size() == 2, "added fields are remembered");
            CHECK(again.removeCabField(0) && again.cabFields().size() == 1, "and removed");
        }
        if (was.isValid()) s.setValue(QStringLiteral("lococonsole/cabFields"), was); else s.remove(QStringLiteral("lococonsole/cabFields"));
        if (cabWas.isValid()) s.setValue(QStringLiteral("lococonsole/cabShown"), cabWas); else s.remove(QStringLiteral("lococonsole/cabShown"));
    }
}
