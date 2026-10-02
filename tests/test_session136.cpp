#include "testutil.h"
#include "layoutaudit.h"

#include "replaywindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

// =============================================================================
//  Session 136 — UI revamp, tool windows 13: Replay.
//
//  The controls row ended with the position readout, which made the window
//  at least 1483 px wide (wider than a 1366-px laptop) and was cut off even
//  so. The readout now ends the scroll row; the play and jump arrows and the
//  Jump combos are sized to their text (at fixed pixels the arrows were
//  blank and "Frame # (s/midnight)" was cut). The timeline's fixed dark
//  canvas is a recorded decision (test_contrastaudit) and is left as it is.
//  Real captures: both locos of replay/loco_?_1_27062026_151052.cap.
// =============================================================================

TEST_SUITE(session136)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    ReplayWindow w(QStringList{ QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_151052.cap"),
                                QStringLiteral(DL_SRC_DIR "/replay/loco_2_1_27062026_151052.cap") });
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    CHECK(w.recordCount() > 1000, "fixture: two real captures, merged");
    w.resize(1300, 720);
    w.show();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    CHECK(w.minimumSizeHint().width() <= 1366 && w.minimumSizeHint().height() <= 700,
          QByteArray("fits a 1366 x 768 laptop (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
              + QByteArray::number(w.minimumSizeHint().height()) + "; was 1483 wide)");

    auto *readout = w.findChild<QLabel *>(QStringLiteral("replayReadout"));
    QSlider *scroll = w.findChild<QSlider *>();
    CHECK(readout && scroll && qAbs(readout->geometry().center().y() - scroll->geometry().center().y()) <= 12,
          "the readout ends the scroll row");
    CHECK(readout && readout->text().contains(QLatin1String("pos ")) && readout->text().contains(QLatin1String("@1_1")),
          QByteArray("it reads time, position, record and source (") + (readout ? readout->text().toUtf8() : QByteArray()) + ")");
    CHECK(readout && readout->fontMetrics().horizontalAdvance(readout->text()) <= readout->width(), "and is not cut off");

    for (const char *name : { "replayPlay", "replayJumpPrev", "replayJumpNext" }) {
        auto *b = w.findChild<QPushButton *>(QLatin1String(name));
        CHECK(b && b->width() >= b->fontMetrics().horizontalAdvance(b->text()) + 20,
              QByteArray(name) + " is wide enough for its arrow (" + QByteArray::number(b ? b->width() : -1) + " px)");
    }
    QComboBox *field = nullptr;
    for (QComboBox *c : w.findChildren<QComboBox *>())
        if (c->findText(QStringLiteral("Frame # (s/midnight)")) >= 0) field = c;
    CHECK(field && field->width() >= field->fontMetrics().horizontalAdvance(QStringLiteral("Frame # (s/midnight)")) + 20,
          "the Jump field shows \"Frame # (s/midnight)\" whole");

    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (")
                               + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
