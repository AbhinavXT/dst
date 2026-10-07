#include "testutil.h"

#include "capturedecoder.h"
#include "dmipanel.h"
#include "dmitimetravel.h"
#include "settings.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QComboBox>

// =============================================================================
//  Session 160 — DMI window: two locos side by side.
//  Real frames: replay/loco_1_1_27062026_140226.cap and
//  replay/loco_2_1_27062026_140226.cap (two locos, the same run).
// =============================================================================

namespace {

// The n-th @dmi line of a capture, as text.
QString nthDmi(const QString &file, int n)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/") + file);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    int seen = 0;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.startsWith("@dmi_") && seen++ == n) return QString::fromLatin1(l);
    }
    return QString();
}

}  // namespace

TEST_SUITE(session160)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    QSettings ini(Settings::iniPath(), QSettings::IniFormat);
    const QVariant savedTwo = ini.value(QStringLiteral("dmi/twoLocos"));
    const QVariant savedFollow = ini.value(QStringLiteral("dmi/followCursor"));
    ini.setValue(QStringLiteral("dmi/twoLocos"), false);
    ini.setValue(QStringLiteral("dmi/followCursor"), false);
    ini.sync();
    DmiTimeTravel::instance()->reset();

    const QString line1 = nthDmi(QStringLiteral("loco_1_1_27062026_140226.cap"), 300);
    const QString line2 = nthDmi(QStringLiteral("loco_2_1_27062026_140226.cap"), 300);
    const CaptureLine cap1 = CaptureDecoder::parseLine(line1);
    const CaptureLine cap2 = CaptureDecoder::parseLine(line2);
    CHECK(cap1.valid && cap2.valid && cap1.key() == QStringLiteral("1_1") && cap2.key() == QStringLiteral("2_1"),
          "fixture: a real @dmi frame from each loco");

    {
        DmiWindow w(nullptr);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        w.resize(880, 720);
        w.show();
        QCoreApplication::processEvents();
        CHECK(!w.twoLocos() && !w.viewB()->isVisible(), "one panel by default");

        // Live: each panel draws its own loco's latest frame.
        w.observeLine(QStringLiteral("1_1"), line1);
        w.observeLine(QStringLiteral("2_1"), line2);
        w.setSelectedSource(QStringLiteral("1_1"));
        const int before = w.width();
        w.setTwoLocos(true);
        QCoreApplication::processEvents();
        CHECK(w.twoLocos() && w.viewB()->isVisible(), "Two locos: a second panel");
        CHECK(w.width() > before + 300, "the window widens by a panel rather than halving the first");
        CHECK(w.selectedSourceB() == QStringLiteral("2_1"), "B starts on the other loco, not on A's");
        CHECK(w.view()->state().locoId == dmiStateFromCapture(cap1).locoId &&
                  w.viewB()->state().locoId == dmiStateFromCapture(cap2).locoId &&
                  w.view()->state().locoId != w.viewB()->state().locoId,
              "live: A draws loco 1's frame, B loco 2's");
        CHECK(w.statusTextB().startsWith(QStringLiteral("2_1")), "B has its own status line");

        // Following: both at one moment, picked on loco 1's row.
        DmiMoment m;
        m.valid = true;
        m.atMs = qMax(cap1.rtc.toMSecsSinceEpoch(), cap2.rtc.toMSecsSinceEpoch());
        m.origin = QStringLiteral("test");
        m.preferredKey = QStringLiteral("1_1");
        m.frames = { DmiFrameAt{ QStringLiteral("1_1"), cap1, cap1.rtc.toMSecsSinceEpoch() },
                     DmiFrameAt{ QStringLiteral("2_1"), cap2, cap2.rtc.toMSecsSinceEpoch() } };
        w.setFollowCursor(true);
        w.setSelectedSourceB(QStringLiteral("1_1"));   // B on A's loco: the moment moves it
        w.showMoment(m);
        CHECK(w.selectedSource() == QStringLiteral("1_1") && w.selectedSourceB() == QStringLiteral("2_1"),
              "at a moment: A on the loco pointed at, B on the other loco that has a frame");
        CHECK(w.view()->state().time == dmiStateFromCapture(cap1).time &&
                  w.viewB()->state().time == dmiStateFromCapture(cap2).time,
              "each panel shows its loco's frame at that moment");
        CHECK(w.statusTextB().contains(QStringLiteral("2_1's @dmi")), "B's status names its frame");

        // A moment with no frame from B's loco: B says so instead of drawing an old one.
        DmiMoment onlyOne = m;
        onlyOne.frames = { m.frames.first() };
        w.showMoment(onlyOne);
        CHECK(!w.viewB()->state().valid && w.statusTextB().contains(QStringLiteral("no @dmi")),
              "no frame from B's loco then: B is empty and says why");

        // Save image: both panels, side by side.
        QTemporaryDir temp;
        const QString png = temp.filePath(QStringLiteral("both.png"));
        CHECK(w.saveImage(png), "Save image");
        const QImage img(png);
        CHECK(img.width() >= w.view()->width() + w.viewB()->width() - 2, "the image holds both panels");

        w.setTwoLocos(false);
        CHECK(!w.viewB()->isVisible(), "unticked: one panel again");
        w.setTwoLocos(true);
        w.close();
    }
    {
        DmiWindow again(nullptr);
        again.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(again.twoLocos(), "remembered for the next window");
        again.setTwoLocos(false);
        again.setFollowCursor(false);
    }

    if (savedTwo.isValid()) ini.setValue(QStringLiteral("dmi/twoLocos"), savedTwo);
    else ini.remove(QStringLiteral("dmi/twoLocos"));
    if (savedFollow.isValid()) ini.setValue(QStringLiteral("dmi/followCursor"), savedFollow);
    else ini.remove(QStringLiteral("dmi/followCursor"));
    DmiTimeTravel::instance()->reset();
}
