#include "uistyle.h"
#include "textzoom.h"

#include <QFontMetrics>
#include <QTabBar>
#include <QTabWidget>
#include "uicolors.h"


#include <QApplication>
#include <QFontDatabase>
#include <QPalette>

namespace UiStyle {
namespace {

// Blend two colours. Used to derive the in-between greys — borders,
// header fills, hover states — that a palette does not name but a
// stylesheet needs.
QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor(int(a.red()   * (1 - t) + b.red()   * t),
                  int(a.green() * (1 - t) + b.green() * t),
                  int(a.blue()  * (1 - t) + b.blue()  * t));
}

}  // namespace

QFont monoFont()
{
    // At the chosen text size (TextZoom), so a window opened after zooming
    // comes out the same size as one rescaled in place.
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * TextZoom::factor());
    }
    return font;
}

int gap()    { return 6; }
int margin() { return 10; }
int radius() { return 4; }

QString sheet()
{
    const QPalette p = qApp->palette();

    const QColor window  = p.color(QPalette::Window);
    const QColor base    = p.color(QPalette::Base);
    const QColor text    = p.color(QPalette::WindowText);
    const QColor button  = p.color(QPalette::Button);
    const QColor hi      = p.color(QPalette::Highlight);
    const bool   isDark  = window.lightness() < 128;

    // Derived tones. Towards Text on a light theme and towards Base on a
    // dark one, so "slightly raised" and "slightly recessed" mean the same
    // thing visually in both.
    const QColor border  = mix(window, text, isDark ? 0.18 : 0.22);
    const QColor subtle  = mix(window, text, isDark ? 0.10 : 0.10);
    const QColor headerB = mix(window, base, isDark ? 0.35 : 0.55);
    const QColor hover   = mix(button, hi,   0.18);
    const QColor pressed = mix(button, hi,   0.34);
    const QColor grid    = mix(base,   text, isDark ? 0.14 : 0.12);

    const int r  = radius();
    const int g  = gap();

    QString s;

    // Buttons: room to breathe, and a hover that says the control is live
    // without the border thickening and shifting the layout by a pixel.
    s += QStringLiteral(
        "QPushButton, QToolButton {"
        " background:%1; color:%2; border:1px solid %3; border-radius:%4px;"
        " padding:5px 12px; }"
        "QPushButton:hover, QToolButton:hover { background:%5; }"
        "QPushButton:pressed, QToolButton:pressed { background:%6; }"
        "QPushButton:default { border:1px solid %7; }"
        "QPushButton:disabled, QToolButton:disabled {"
        " background:%1; color:%8; border-color:%9; }")
        .arg(button.name(), p.color(QPalette::ButtonText).name(), border.name())
        .arg(r)
        .arg(hover.name(), pressed.name(), hi.name(),
             p.color(QPalette::Disabled, QPalette::ButtonText).name(), subtle.name());

    // The tab bar's scroll arrows.
    //
    // They are QToolButtons living inside the tab bar, so the generic
    // QToolButton rule above catches them and hands them a border, a
    // background and padding:5px 12px — and once a stylesheet has styled a
    // tool button, Qt stops drawing its arrow. The result is two blank
    // rounded boxes at the end of the bar, which is exactly what they were
    // reported as: "2 blank tabs at the end".
    //
    // They only appear when the tabs overflow, which is why this surfaced the
    // day tabs got wider. Padding of 12px on a 16px-wide button is most of
    // the reason there was no room left for an arrow.
    s += QStringLiteral(
        "QTabBar::scroller { width:32px; }"
        "QTabBar QToolButton {"
        " background:%1; color:%2; border:1px solid %3; border-radius:0px;"
        " padding:0px; margin:0px; min-width:16px; }"
        "QTabBar QToolButton:hover { background:%4; }"
        "QTabBar QToolButton:pressed { background:%5; }"
        "QTabBar QToolButton:disabled { color:%6; }")
        .arg(button.name(), p.color(QPalette::ButtonText).name(), border.name())
        .arg(hover.name(), pressed.name(),
             p.color(QPalette::Disabled, QPalette::ButtonText).name());

    // Check and radio indicators. Without a rule here they are drawn from the
    // palette by the base style, and on a near-black window that outline is
    // faint enough that an unticked checkbox reads as no checkbox at all —
    // "Follow newest" looked like a label with a stray gap in front of it.
    // A tick you cannot find the box for is a control the operator does not
    // know is there.
    s += QStringLiteral(
        "QCheckBox::indicator, QRadioButton::indicator {"
        " width:14px; height:14px; background:%1; border:1px solid %2; }"
        "QRadioButton::indicator { border-radius:8px; }"
        "QCheckBox::indicator { border-radius:3px; }"
        "QCheckBox::indicator:hover, QRadioButton::indicator:hover {"
        " border-color:%3; }"
        "QCheckBox::indicator:checked, QRadioButton::indicator:checked {"
        " background:%3; border-color:%3; }"
        "QCheckBox::indicator:disabled, QRadioButton::indicator:disabled {"
        " background:%4; border-color:%5; }")
        .arg(base.name(), mix(window, text, isDark ? 0.42 : 0.38).name(),
             hi.name(), window.name(), subtle.name());

    // Text entry. The focus ring is the highlight colour, so it follows the
    // theme rather than being a fixed blue that clashes with the dark one.
    s += QStringLiteral(
        "QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {"
        " background:%1; color:%2; border:1px solid %3; border-radius:%4px;"
        " padding:3px 6px; selection-background-color:%5;"
        " selection-color:%6; }"
        "QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus,"
        "QDoubleSpinBox:focus, QComboBox:focus { border:1px solid %5; }"
        "QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled { color:%7; }")
        .arg(base.name(), p.color(QPalette::Text).name(), border.name())
        .arg(r)
        .arg(hi.name(), p.color(QPalette::HighlightedText).name(),
             p.color(QPalette::Disabled, QPalette::Text).name());

    // Group boxes: a soft frame with the title sitting on the edge, rather
    // than a hard rectangle with the label overlapping it.
    s += QStringLiteral(
        "QGroupBox {"
        " border:1px solid %1; border-radius:%2px; margin-top:%3px;"
        " padding:%4px %4px %4px %4px; }"
        "QGroupBox::title {"
        " subcontrol-origin:margin; subcontrol-position:top left;"
        " left:%5px; padding:0 4px; color:%6; }")
        .arg(subtle.name()).arg(r).arg(g + 4).arg(g)
        .arg(g + 2)
        .arg(mix(text, window, 0.25).name());

    // Tables and lists. The header is a surface, not a button; the grid is a
    // hint rather than a cage.
    s += QStringLiteral(
        "QHeaderView::section {"
        " background:%1; color:%2; border:none;"
        " border-bottom:1px solid %3; border-right:1px solid %4;"
        " padding:5px 8px; font-weight:600; }"
        "QHeaderView::section:hover { background:%5; }"
        "QTableView, QTreeView, QListView {"
        " background:%6; alternate-background-color:%7;"
        " border:1px solid %3; border-radius:%8px;"
        " gridline-color:%9; }"
        "QTableView::item, QTreeView::item, QListView::item { padding:2px 4px; }"
        "QTableView::item:selected, QTreeView::item:selected,"
        "QListView::item:selected { background:%10; color:%11; }")
        .arg(headerB.name(), mix(text, window, 0.15).name(),
             border.name(), subtle.name(), hover.name())
        .arg(base.name(), p.color(QPalette::AlternateBase).name())
        .arg(r)
        .arg(grid.name(), hi.name(), p.color(QPalette::HighlightedText).name());

    // A point smaller than the rest of the UI. The tab bar is the one place
    // where the number of items is set by how many sources the operator has
    // open rather than by design, so it is the one place worth buying width
    // back with size. Floored at 8pt: a label too small to read is a worse
    // trade than one that does not quite fit.
    const qreal basePt = qApp->font().pointSizeF() > 0 ? qApp->font().pointSizeF() : 9.0;
    const qreal tabPt  = qMax(8.0, basePt - 1.0);

    // The selected tab is a filled block now rather than a bolder word, so the
    // fill has to read as deliberate against the bar. The hover tint is too
    // faint to carry it alone.
    //
    // Ayu Light keeps its bright blue block (accent lightened). The other
    // light themes tint their own base toward their accent instead: Sepia's
    // ink-blue lightened the same way comes out a cyan that belongs to no
    // part of a warm palette.
    QColor selectedTabBg = base.lighter(135);
    if (!isDark) {
        if (UiColor::activeTheme() <= 0) {
            selectedTabBg = UiColor::accent().lighter(185);
        } else {
            selectedTabBg = mix(base, UiColor::accent(), 0.22);
        }
    }

    // Tabs.
    //
    // The selected tab used to be drawn as a bordered box "joined" to the
    // page below it by painting its bottom edge in the page's colour. That
    // trick only works when the tab's fill and the pane's fill are the same
    // and the two overlap by exactly one pixel; here they were not, so the
    // seam showed and the current tab read as a box floating above the
    // content rather than as the tab that is open. Seen in use, it is the
    // first thing the eye lands on and it is wrong.
    //
    // So: no box. The current tab is marked the way editors and browsers
    // mark one — a solid underline in the accent colour, a page-coloured
    // fill, and heavier text. Nothing depends on pixel-perfect overlap, so
    // there is no seam to get wrong.
    s += QStringLiteral(
        "QTabWidget::pane { border:1px solid %1; border-radius:%2px; top:-1px; }"
        "QTabBar { background:transparent; }"
        // Tabs: smaller, one weight, and selection said by BACKGROUND.
        //
        // Three faults came out of the selected tab being drawn heavier than
        // the tab Qt measured — a clipped label, then scroll arrows in a
        // half-empty bar once the difference was reserved per tab. Patch 37
        // levelled the weight; this drops it to normal and takes a point off
        // the size too, because the real pressure is WIDTH. Eleven tabs on a
        // laptop screen do not fit, and when they do not fit the bar shrinks
        // them and draws the centred label out of both ends — which is why
        // every tab in the last report lost characters at the front AND the
        // back, "Fault_L1V1" as "ault_L1V", and why it started after a
        // restart reopened every saved tab at once.
        //
        // Selection is now the background block plus the accent underline. A
        // filled tab among unfilled ones is at least as findable as a bold
        // word among plain ones, and unlike weight it costs no width.
        "QTabBar::tab {"
        " background:transparent; color:%3;"
        " padding:5px 24px 5px 10px; margin-right:2px;"
        " font-size:%8pt; font-weight:normal;"
        " border:none; border-bottom:2px solid transparent; }"
        "QTabBar::tab:hover { background:%4; color:%5; }"
        "QTabBar::tab:selected {"
        " background:%6; color:%5; border-bottom:2px solid %7; }")
        .arg(UiColor::frame().name(QColor::HexRgb)).arg(r)
        .arg(UiColor::muted().name(), subtle.name(), text.name(),
             selectedTabBg.name(), UiColor::accent().name())
        .arg(tabPt);

    // The close cross.
    //
    // Setting an icon on the button does not work once a stylesheet is in
    // force: QStyleSheetStyle draws the close-button subcontrol itself and
    // ignores the icon. Measured on the dark theme, its default cross came
    // out at RGB(36,36,37) — all but invisible next to a label at 5:1. So
    // the cross is a resource, one per theme, matching UiColor::muted().
    s += QStringLiteral(
        "QTabBar::close-button { image: url(:/tabclose_%1.png);"
        " width:12px; height:12px; margin-left:4px; }"
        "QTabBar::close-button:hover { background:%2; border-radius:7px; }")
        .arg(isDark ? QStringLiteral("dark") : QStringLiteral("light"),
             hover.name());

    // Scrollbars: thin, no arrow buttons, visible against either theme.
    s += QStringLiteral(
        "QScrollBar:vertical { background:transparent; width:12px; margin:0; }"
        "QScrollBar:horizontal { background:transparent; height:12px; margin:0; }"
        "QScrollBar::handle:vertical, QScrollBar::handle:horizontal {"
        " background:%1; border-radius:5px; min-height:28px; min-width:28px;"
        " margin:2px; }"
        "QScrollBar::handle:hover { background:%2; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height:0; width:0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background:transparent; }")
        .arg(mix(window, text, 0.28).name(), mix(window, text, 0.42).name());

    // Menus, tooltips, splitters, the status bar.
    s += QStringLiteral(
        "QMenuBar { background:%1; border-bottom:1px solid %2; }"
        "QMenuBar::item { padding:5px 10px; background:transparent; }"
        "QMenuBar::item:selected { background:%3; border-radius:%4px; }"
        "QMenu { background:%5; border:1px solid %2; border-radius:%4px; padding:4px; }"
        "QMenu::item { padding:5px 24px 5px 20px; border-radius:3px; }"
        "QMenu::item:selected { background:%6; color:%7; }"
        "QMenu::separator { height:1px; background:%2; margin:4px 8px; }"
        "QToolTip { background:%8; color:%9; border:1px solid %2;"
        " border-radius:%4px; padding:5px 7px; }"
        "QSplitter::handle { background:%2; }"
        "QSplitter::handle:horizontal { width:1px; }"
        "QSplitter::handle:vertical { height:1px; }"
        "QStatusBar { background:%1; border-top:1px solid %2; }"
        "QStatusBar::item { border:none; }")
        .arg(window.name(), subtle.name(), hover.name())
        .arg(r)
        .arg(base.name(), hi.name(), p.color(QPalette::HighlightedText).name(),
             p.color(QPalette::ToolTipBase).name(),
             p.color(QPalette::ToolTipText).name());

    // Docks and progress.
    s += QStringLiteral(
        "QDockWidget { titlebar-close-icon:none; }"
        "QDockWidget::title {"
        " background:%1; padding:5px 8px; border-bottom:1px solid %2; }"
        "QProgressBar {"
        " border:1px solid %2; border-radius:%3px; height:14px;"
        " text-align:center; background:%4; }"
        "QProgressBar::chunk { background:%5; border-radius:%3px; }")
        .arg(headerB.name(), subtle.name())
        .arg(r)
        .arg(base.name(), hi.name());

    return s;
}

void apply()
{
    if (!qApp) { return; }

    // Leave the family alone — the system UI font is the one the operator's
    // machine is set up to render well. Only the size is nudged, and only
    // when it is unusually small, which some X11 setups report.
    // Once TextZoom is running it owns the size (80 % of 9 pt is meant to
    // be smaller than 9 pt), so the floor only applies before it starts.
    QFont f = qApp->font();
    if (!TextZoom::isInitialised() && f.pointSizeF() > 0 && f.pointSizeF() < 9.0) {
        f.setPointSizeF(9.0);
    }
    qApp->setFont(f);

    qApp->setStyleSheet(sheet());
}


// ---------------------------------------------------------------------------
//  Tab tooltips. See useTabTooltips().
// ---------------------------------------------------------------------------
namespace {

class TooltipTabBar : public QTabBar
{
public:
    using QTabBar::QTabBar;

protected:
    // The whole name on every tab's tooltip, so a tab can be identified even
    // when the bar is scrolled or the label does not fit.
    //
    // There used to be tab-width arithmetic here as well, reserving the
    // difference between normal and demi-bold. It is gone: the stylesheet now
    // draws every tab at one weight, so there is no difference to reserve, and
    // reserving it was what put scroll arrows in a half-empty bar. Sizing tabs
    // is Qt's job, and it does it correctly when it is told the truth about
    // the font.
    //
    // Tab text changes after insertion — a source tab is named when its first
    // message arrives. QTabBar has no signal for that, but it calls
    // tabLayoutChange() when the text changes and tabInserted() when a tab
    // appears. (QEvent::LayoutRequest looked like the tidier hook and simply
    // never arrives.)
    void tabInserted(int) override { syncToolTips(); }
    void tabLayoutChange() override { syncToolTips(); }

private:
    void syncToolTips()
    {
        for (int i = 0; i < count(); ++i) {
            if (tabToolTip(i) != tabText(i)) { setTabToolTip(i, tabText(i)); }
        }
    }
};

}  // namespace

// QTabWidget::setTabBar() is protected, which is Qt saying "subclass me".
// A one-line subclass is the sanctioned way in and is cheaper than the
// alternative — reaching for it through a cast, which would work today and
// break on any Qt that reorders the class.
namespace {
class TabWidgetAccess : public QTabWidget
{
public:
    static void install(QTabWidget *w) {
        static_cast<TabWidgetAccess *>(w)->setTabBar(new TooltipTabBar(w));
    }
};
}  // namespace

void useTabTooltips(QTabWidget *tabs)
{
    // setTabBar() replaces the bar wholesale, so this is a construction-time
    // call and says so rather than silently doing nothing later.
    if (!tabs || tabs->count() > 0) { return; }
    TabWidgetAccess::install(tabs);

}

}  // namespace UiStyle
