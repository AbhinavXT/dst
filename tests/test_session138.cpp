#include "testutil.h"
#include "layoutaudit.h"

#include "framediffwindow.h"
#include "theme.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFile>
#include <QFontInfo>
#include <QHeaderView>
#include <QImage>
#include <QPlainTextEdit>
#include <QStyleOptionButton>
#include <QTableWidget>

// =============================================================================
//  Session 138 — UI revamp, tool windows 15: Frame Diff (+ every checkbox).
//
//  A ticked checkbox was filled with the selection highlight: in the dark
//  themes a navy close to the background, so every ticked box in the app
//  read as unticked (Frame Diff's "Show only differences" is ticked by
//  default and looked off). Ticked is now the accent. Frame Diff: hex and
//  values in UiStyle::monoFont(), frame boxes tall enough for a whole frame,
//  headers left, counts in English. Real frames: three consecutive @lsrp
//  lines from replay/loco_1_1_27062026_140226.cap.
// =============================================================================

namespace {

// The colour at the centre of a checkbox's indicator, as drawn.
QColor indicatorColour(QCheckBox *box)
{
    QStyleOptionButton opt;
    opt.initFrom(box);
    const QRect r = box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, box);
    const QImage img = box->grab().toImage();
    return img.pixelColor(r.center() * img.devicePixelRatio());
}

}  // namespace

TEST_SUITE(session138)
{
    // ---- a ticked box, in every theme ------------------------------------------------------
    for (Theme t : ThemeUtil::all()) {
        ThemeUtil::apply(t);
        UiStyle::apply();
        QWidget host;
        QCheckBox on(QStringLiteral("ticked"), &host), off(QStringLiteral("not"), &host);
        off.move(0, 30);
        on.setChecked(true);
        host.resize(200, 60);
        host.show();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        const QColor base = host.palette().color(QPalette::Base);
        const QColor ticked = indicatorColour(&on), unticked = indicatorColour(&off);
        const double vsBase = UiColor::contrastRatio(ticked, base), vsOff = UiColor::contrastRatio(ticked, unticked);
        CHECK(vsBase >= 3.0 && vsOff >= 3.0,
              QByteArray(qPrintable(ThemeUtil::toString(t))) + ": a ticked box stands out (" + QByteArray::number(vsBase, 'f', 2)
                  + ":1 against the background, " + QByteArray::number(vsOff, 'f', 2) + ":1 against an unticked box; needs 3:1)");
    }

    // ---- Frame Diff, light theme as the other suites ---------------------------------------
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    QStringList lsrp;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd() && lsrp.size() < 3) {
            const QString l = QString::fromUtf8(f.readLine()).trimmed();
            if (l.startsWith(QLatin1String("@lsrp"))) lsrp << l;
        }
    }
    CHECK(lsrp.size() == 3, "fixture: three real @lsrp lines");

    FrameDiffWindow w(nullptr);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    for (int i = 0; i < lsrp.size(); ++i) w.setSide(i, lsrp.at(i));
    w.resize(1100, 720);
    w.show();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    auto *table = w.findChild<QTableWidget *>(QStringLiteral("frameDiffTable"));
    CHECK(table && QFontInfo(table->font()).fixedPitch(), "the values are monospaced");
    CHECK(table && (table->horizontalHeader()->defaultAlignment() & Qt::AlignLeft), "headers align left");
    bool allMono = true, allFit = true;
    for (QPlainTextEdit *e : w.findChildren<QPlainTextEdit *>(QStringLiteral("frameDiffInput"))) {
        allMono = allMono && QFontInfo(e->font()).fixedPitch();
        // The whole capture line, no scrolling: the document fits the viewport.
        allFit = allFit && e->document()->size().height() * e->fontMetrics().lineSpacing() <= e->viewport()->height() + 2;
    }
    CHECK(allMono, "the frame boxes' hex is monospaced");
    CHECK(allFit, "each frame box shows its whole 39-byte frame");
    CHECK(table && table->height() > w.height() / 3, "the table keeps the bulk of the window");

    QCheckBox *onlyDiff = w.findChild<QCheckBox *>();
    CHECK(onlyDiff && onlyDiff->isChecked(), "Show only differences is on, as before");
    QString summary;
    for (QLabel *l : w.findChildren<QLabel *>())
        if (l->text().startsWith(QLatin1String("3 frames"))) summary = l->text();
    CHECK(summary.startsWith(QStringLiteral("3 frames · 5 fields differ, 33 the same")),
          QByteArray("the summary reads as English (") + summary.toUtf8() + ")");

    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366, "fits a laptop");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (")
                               + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
