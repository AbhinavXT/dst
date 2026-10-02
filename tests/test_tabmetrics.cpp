#include "testutil.h"
#include <cstdio>

#include "uistyle.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QToolButton>
#include <QWidget>

// =============================================================================
//  The tab bar.
//
//  Three reports, one root cause. Patch 30 gave the SELECTED tab
//  font-weight:600 as its cue, which makes the drawn label wider than the one
//  Qt measured:
//
//    1. the selected tab, named L1_V1, drew as "_1_V1" — Qt centres tab text,
//       so an overrun is cut off at both ends
//    2. reserving the difference per tab fixed that and put scroll arrows in
//       the middle of a HALF-EMPTY bar: widening tabs does not widen the bar
//       Qt has already sized, so the bar concluded it was overflowing while
//       twelve hundred pixels sat empty beside it
//    3. those arrows were blank boxes, because the sheet's generic QToolButton
//       rule had styled them and a styled tool button loses its arrow
//
//  The fix for the first two is to stop creating the gap: every tab is drawn
//  at ONE weight, so what is measured is what is drawn, and tab width goes
//  back to being Qt's business. Selection is said by background, text colour
//  and a 2px accent underline — three ways, which is what it was mostly being
//  said by anyway.
//
//  These tests pin the PROPERTIES — the label fits, the bar does not scroll
//  when it has room, the arrows have something drawn in them — rather than
//  pixel counts, which would be a test of this machine's font rather than of
//  the bugs.
// =============================================================================

namespace {

// The names from the console in the photographs.
const char *kNames[] = { "L2_V2", "L2_V1", "L1_V1", "81_1",
                         "82_2",  "82_1",  "81_2",  "L1_V2" };

void fill(QTabWidget *w)
{
    for (const char *n : kNames) { w->addTab(new QWidget, QString::fromLatin1(n)); }
}

int visibleScrollers(QTabBar *bar)
{
    int n = 0;
    for (QToolButton *b : bar->findChildren<QToolButton *>()) {
        if (b->isVisible()) { ++n; }
    }
    return n;
}

// Pixels inside a rect that differ from its own top-left corner. Used to ask
// "is anything drawn here at all", which is the question both the clipped
// label and the empty arrow buttons turned on.
int ink(const QImage &img, QRect r)
{
    r = r.intersected(img.rect());
    if (r.width() < 3 || r.height() < 3) { return 0; }
    const QRgb bg = img.pixel(r.topLeft());
    int n = 0;
    for (int x = r.left() + 1; x < r.right(); ++x) {
        for (int y = r.top() + 1; y < r.bottom(); ++y) {
            const QRgb p = img.pixel(x, y);
            if (qAbs(qRed(p) - qRed(bg)) + qAbs(qGreen(p) - qGreen(bg))
                    + qAbs(qBlue(p) - qBlue(bg)) > 60) {
                ++n;
            }
        }
    }
    return n;
}

}  // namespace

TEST_SUITE(tabmetrics)
{
    const QString savedSheet = qApp->styleSheet();
    UiStyle::apply();      // the scroller and tab rules are the thing under test

    // ---- a bar with room does not scroll -----------------------------------
    //
    // The regression that mattered: eight tabs needing 580px, a 1900px window,
    // and scroll arrows anyway.
    {
        QTabWidget w;
        UiStyle::useTabTooltips(&w);
        fill(&w);
        w.resize(1900, 300);
        w.show();
        QTest::qWait(60);

        int sum = 0;
        for (int i = 0; i < w.tabBar()->count(); ++i) {
            sum += w.tabBar()->tabRect(i).width();
        }
        CHECK(sum < 1900, "eight tabs need less than the window is wide");
        CHECK(w.tabBar()->width() >= sum,
              "and the bar asks for at least the room its tabs occupy — the "
              "gap between those two is what put arrows in a half-empty bar");
        CHECK(visibleScrollers(w.tabBar()) == 0,
              "so there is nothing to scroll and no arrows are shown");
    }

    // ---- the selected label is not clipped ---------------------------------
    {
        QTabWidget w;
        UiStyle::useTabTooltips(&w);
        fill(&w);
        w.setCurrentIndex(2);          // L1_V1, the tab that drew as "_1_V1"
        w.resize(1900, 300);
        w.show();
        QTest::qWait(60);

        QTabBar *bar = w.tabBar();
        QFont heavy = bar->font();
        heavy.setWeight(QFont::DemiBold);
        const int drawn = QFontMetrics(heavy).horizontalAdvance(QStringLiteral("L1_V1"));

        CHECK(bar->tabRect(2).width() > drawn,
              "the selected tab is wider than its label at the weight it is "
              "drawn, with room left over for padding");

        // And the ink proves it rather than the arithmetic: the label sits
        // clear of both edges of its tab.
        const QImage shot = w.grab().toImage();
        const QRect r = bar->tabRect(2).translated(bar->mapTo(&w, QPoint(0, 0)));
        int first = -1;
        int last  = -1;
        // Two pixels in from each side: the selected tab is a filled block
        // now, so its own edge against the neighbouring tab is a colour step,
        // and sampling from the very edge would find that rather than the
        // label.
        const QRect probe = r.adjusted(2, 3, -2, -6).intersected(shot.rect());
        const QRgb bg = shot.pixel(probe.topLeft());
        for (int x = probe.left(); x < probe.right(); ++x) {
            for (int y = probe.top(); y < probe.bottom(); ++y) {
                const QRgb p = shot.pixel(x, y);
                if (qAbs(qRed(p) - qRed(bg)) + qAbs(qGreen(p) - qGreen(bg))
                        + qAbs(qBlue(p) - qBlue(bg)) > 90) {
                    if (first < 0) { first = x; }
                    last = x;
                    break;
                }
            }
        }
        CHECK(first > probe.left() && last < probe.right() - 1,
              "and nothing is touching either edge, which is what a clipped "
              "label does");
    }

    // ---- a long label leaves room for the close cross ----------------------
    //
    // "Fault_L1_V1" cropped while "L2_V1" did not, which is the shape of a
    // fixed reserve that is missing rather than a per-character shortfall: a
    // short label survives on its padding, a long one runs into whatever sits
    // at the right-hand end of the tab. What sits there is the close button.
    //
    // Whether a styled ::close-button counts towards the tab's size hint is up
    // to the platform style. The reserve is stated in the tab rule now, so it
    // does not depend on that — and stated in the rule that also sizes the
    // tab, so it cannot come apart the way a reserve computed in C++ did.
    {
        QTabWidget w;
        UiStyle::useTabTooltips(&w);
        w.setTabsClosable(true);
        w.addTab(new QWidget, QStringLiteral("Fault_L1_V1"));
        w.addTab(new QWidget, QStringLiteral("L2_V1"));
        w.resize(1900, 300);
        w.show();
        QTest::qWait(60);

        QTabBar *bar = w.tabBar();
        QFont heavy = bar->font();
        heavy.setWeight(QFont::DemiBold);
        const QFontMetrics fm(heavy);

        for (int i = 0; i < 2; ++i) {
            const int spare = bar->tabRect(i).width()
                            - fm.horizontalAdvance(bar->tabText(i));
            CHECK(spare >= 28,
                  "a closable tab is wider than its label by enough for the "
                  "cross and its padding, long name or short");
        }

        // The long one specifically: it is the one that cropped.
        CHECK(bar->tabRect(0).width() > bar->tabRect(1).width(),
              "and a longer name still gets a proportionally wider tab");
    }

    // ---- one weight for every tab ------------------------------------------
    //
    // If selecting a tab changed its width, the gap between measured and drawn
    // would be back, and so would both faults above.
    {
        QTabWidget w;
        UiStyle::useTabTooltips(&w);
        fill(&w);
        w.resize(1900, 300);
        w.show();
        QTest::qWait(40);

        const int wasSelected   = w.tabBar()->tabRect(0).width();
        const int wasUnselected = w.tabBar()->tabRect(1).width();
        w.setCurrentIndex(1);
        QTest::qWait(40);

        CHECK(w.tabBar()->tabRect(0).width() == wasSelected,
              "a tab keeps its width when it stops being selected");
        CHECK(w.tabBar()->tabRect(1).width() == wasUnselected,
              "and when it starts");
    }

    // ---- when it really is too narrow, the arrows are drawn ----------------
    //
    // They are QToolButtons inside the bar, so the sheet's generic rule caught
    // them: padding 5px 12px on a 16px button, and a styled tool button loses
    // its arrow. Two bordered empty boxes, reported as blank tabs.
    {
        QTabWidget w;
        UiStyle::useTabTooltips(&w);
        fill(&w);
        w.resize(300, 200);
        w.show();
        QTest::qWait(60);

        QTabBar *bar = w.tabBar();
        CHECK(visibleScrollers(bar) == 2,
              "a bar genuinely too narrow for its tabs does scroll");

        const QImage shot = w.grab().toImage();
        for (QToolButton *b : bar->findChildren<QToolButton *>()) {
            if (!b->isVisible()) { continue; }
            const QRect r(b->mapTo(&w, QPoint(0, 0)), b->size());
            CHECK(ink(shot, r) >= 12,
                  "and each arrow has something drawn in it, not an empty box");
        }
    }

    // ---- eleven tabs, the count that broke it ------------------------------
    //
    // The last report came from reopening the console: every saved tab came
    // back at once, eleven of them, and the bar could not fit them. When a
    // tab bar cannot fit its tabs it SHRINKS them, and with elision off it
    // draws the centred label out of both ends — so every tab lost characters
    // at the front and the back, not just the selected one.
    //
    // Normal weight and a point off the size is what buys the width back.
    {
        QTabWidget w;
        UiStyle::useTabTooltips(&w);
        w.setTabsClosable(true);
        for (const char *n : { "L1_V1", "81_1", "L1_V2", "82_1", "L2_V2", "82_2",
                               "L2_V1", "81_2", "Fault_L1V1", "Fault_L1V2",
                               "Fault_L2V2" }) {
            w.addTab(new QWidget, QString::fromLatin1(n));
        }
        w.resize(1900, 300);
        w.show();
        QTest::qWait(60);

        QTabBar *bar = w.tabBar();
        int sum = 0;
        for (int i = 0; i < bar->count(); ++i) { sum += bar->tabRect(i).width(); }
        CHECK(sum <= 1900,
              "eleven tabs — a full workspace reopened — fit a laptop screen");
        // Only meaningful when the window really is 1900 px wide. On a
        // smaller screen a native window is held to the screen (a Windows CI
        // runner is 1024 px: session 116), and scroll arrows are then right.
        if (w.width() >= 1900) {
            CHECK(visibleScrollers(bar) == 0, "with nothing to scroll");
        } else {
            std::printf("  note [tabmetrics] scroll check not run: the screen held the window to %d px\n",
                        w.width());
        }

        // No tab is narrower than its own label, which is the state that
        // produces characters falling off both ends.
        const QFontMetrics fm(bar->font());
        for (int i = 0; i < bar->count(); ++i) {
            CHECK(bar->tabRect(i).width() > fm.horizontalAdvance(bar->tabText(i)),
                  "and none is narrower than the name it carries");
        }
    }

    // ---- selection is said by the background -------------------------------
    //
    // It used to be said by weight, which is what made the drawn label wider
    // than the measured tab and caused everything above. A fill costs no
    // width. It does have to be VISIBLE, though, or the cue is gone rather
    // than moved.
    {
        QTabWidget w;
        UiStyle::useTabTooltips(&w);
        fill(&w);
        w.resize(1900, 300);
        w.show();
        w.setCurrentIndex(0);
        QTest::qWait(60);

        const QImage shot = w.grab().toImage();
        QTabBar *bar = w.tabBar();
        const QRect sel   = bar->tabRect(0).translated(bar->mapTo(&w, QPoint(0, 0)));
        const QRect other = bar->tabRect(3).translated(bar->mapTo(&w, QPoint(0, 0)));

        // Sample a corner of each, away from the label and the underline.
        const QRgb a = shot.pixel(sel.left() + 2, sel.top() + 2);
        const QRgb b = shot.pixel(other.left() + 2, other.top() + 2);
        const int diff = qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b))
                       + qAbs(qBlue(a) - qBlue(b));
        CHECK(diff >= 24,
              "the selected tab is filled differently from its neighbours, "
              "clearly enough to find at a glance");
    }

    // ---- the full name is always one hover away ----------------------------
    {
        QTabWidget w;
        UiStyle::useTabTooltips(&w);
        fill(&w);
        w.show();
        QTest::qWait(40);
        CHECK(w.tabBar()->tabToolTip(0) == QStringLiteral("L2_V2"),
              "every tab carries its full name as a tooltip");

        // Source tabs are named when their first message arrives, so the
        // tooltip has to follow a rename and not only an insertion.
        w.setTabText(0, QStringLiteral("L2_V2_renamed"));
        QTest::qWait(40);
        CHECK(w.tabBar()->tabToolTip(0) == QStringLiteral("L2_V2_renamed"),
              "and follows a rename");
    }

    // ---- it is a construction-time call, and says so -----------------------
    {
        QTabWidget late;
        late.addTab(new QWidget, QStringLiteral("L1_V1"));
        QTabBar *before = late.tabBar();
        UiStyle::useTabTooltips(&late);
        CHECK(late.tabBar() == before,
              "called after a tab exists it declines rather than replacing the "
              "bar and dropping the tabs with it");
        CHECK(late.count() == 1, "the tab is still there");
    }

    UiStyle::useTabTooltips(nullptr);
    CHECK(true, "a null tab widget is accepted and ignored");

    qApp->setStyleSheet(savedSheet);
}
