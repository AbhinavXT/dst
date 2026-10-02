#include "uistyle.h"
#include "textzoom.h"

#include <QFontMetrics>
#include <QTabBar>
#include <QTabWidget>
#include "uicolors.h"


#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QFontDatabase>
#include <QFontInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QStyle>

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
    // Session 117: check it IS fixed-pitch. Some platforms (the offscreen
    // plugin, some Linux setups) answer the "fixed" request with the
    // proportional UI face, and timestamps, hex and values then lose their
    // columns without anything saying so. Fall back by family.
    if (!QFontInfo(font).fixedPitch()) {
        const qreal pt = font.pointSizeF();
        // Only families that are installed: asking for a missing one makes
        // Qt build its whole alias table (measured: 136 ms).
        static const QStringList installed = QFontDatabase().families();
        for (const char *family : { "JetBrains Mono", "IBM Plex Mono", "Cascadia Mono", "Consolas",
                                    "Menlo", "SF Mono", "DejaVu Sans Mono", "Liberation Mono",
                                    "Courier New" }) {
            if (!installed.contains(QString::fromLatin1(family))) continue;
            QFont candidate(QString::fromLatin1(family));
            if (pt > 0) candidate.setPointSizeF(pt);
            if (QFontInfo(candidate).fixedPitch()) { font = candidate; break; }
        }
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
    }
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * TextZoom::factor());
    }
    return font;
}

int gap()    { return 6; }
int margin() { return 12; }
int radius() { return 6; }     // session 117: rounder, as the revamp's components are

namespace {

QString toneName(Tone t)
{
    switch (t) {
    case Tone::Accent: return QStringLiteral("accent");
    case Tone::Ok:     return QStringLiteral("ok");
    case Tone::Warn:   return QStringLiteral("warn");
    case Tone::Fail:   return QStringLiteral("fail");
    default:           return QStringLiteral("neutral");
    }
}

QColor toneInk(Tone t)
{
    switch (t) {
    case Tone::Accent: return UiColor::accent();
    case Tone::Ok:     return UiColor::ok();
    case Tone::Warn:   return UiColor::warning();
    case Tone::Fail:   return UiColor::error();
    default:           return qApp->palette().color(QPalette::WindowText);
    }
}

void repolish(QWidget *w)
{
    if (!w) return;
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

// The rules for the roles (components) of session 117. Every colour from
// the palette and UiColor, as everywhere in this file.
QString componentRules(const QPalette &p, bool isDark, const QColor &window,
                       const QColor &base, const QColor &text, const QColor &border,
                       const QColor &subtle, const QColor &hover)
{
    const QColor hi       = p.color(QPalette::Highlight);
    const QColor muted    = UiColor::muted();
    const QColor sunk     = mix(window, isDark ? QColor(Qt::black) : text, isDark ? 0.25 : 0.05);
    const QColor raised   = mix(window, base, 0.6).lighter(isDark ? 135 : 100);
    const QColor panelBg  = mix(window, base, isDark ? 0.35 : 0.65);
    const QColor railOn   = mix(window, hi, isDark ? 0.24 : 0.16);
    const qreal basePt    = qApp->font().pointSizeF() > 0 ? qApp->font().pointSizeF() : 9.0;
    const qreal smallPt   = qMax(7.5, basePt - 1.5);

    QString s;

    // Chips: one rule for the shape, one per tone for the colours.
    s += QStringLiteral(
        "*[dlRole=\"chip\"] { border:none; border-radius:11px; padding:3px 10px;"
        " font-weight:600; min-height:16px; }");
    for (Tone t : { Tone::Neutral, Tone::Accent, Tone::Ok, Tone::Warn, Tone::Fail }) {
        const QColor fill = chipFill(t);
        const QColor ink  = chipText(t);
        s += QStringLiteral("*[dlRole=\"chip\"][dlTone=\"%1\"] { background:%2; color:%3; }"
                            "QAbstractButton[dlRole=\"chip\"][dlTone=\"%1\"]:hover { background:%4; }")
                 .arg(toneName(t), fill.name(), ink.name(), mix(fill, ink, 0.12).name());
    }

    // Segmented control: a sunken track, the checked segment raised.
    s += QStringLiteral(
        "QWidget[dlRole=\"segmented\"] { background:%1; border:1px solid %2; border-radius:8px; }"
        "QWidget[dlRole=\"segmented\"] QPushButton { background:transparent; border:none;"
        " border-radius:6px; padding:4px 12px; color:%3; margin:2px; }"
        "QWidget[dlRole=\"segmented\"] QPushButton:hover { color:%4; background:%5; }"
        "QWidget[dlRole=\"segmented\"] QPushButton:checked { background:%6; color:%4;"
        " border:1px solid %2; }")
        .arg(sunk.name(), border.name(), muted.name(), text.name(), hover.name(), raised.name());

    // The icon rail.
    s += QStringLiteral(
        "QAbstractButton[dlRole=\"rail\"] { background:transparent; border:none;"
        " border-radius:10px; min-width:40px; max-width:40px; min-height:40px; max-height:40px;"
        " padding:0px; }"
        "QAbstractButton[dlRole=\"rail\"]:hover { background:%1; }"
        "QAbstractButton[dlRole=\"rail\"]:checked { background:%2; }")
        .arg(hover.name(), railOn.name());

    // The primary action.
    {
        const QColor acc = UiColor::accent();
        const QColor ink = UiColor::withContrast(isDark ? QColor(Qt::black) : QColor(Qt::white), acc, contrastFloor());
        s += QStringLiteral(
            "QAbstractButton[dlRole=\"primary\"] { background:%1; color:%2; border:1px solid %1;"
            " font-weight:600; }"
            "QAbstractButton[dlRole=\"primary\"]:hover { background:%3; border-color:%3; }"
            "QAbstractButton[dlRole=\"primary\"]:disabled { background:%4; color:%5; border-color:%4; }")
            .arg(acc.name(), ink.name(), mix(acc, ink, 0.12).name(), mix(window, acc, 0.25).name(), muted.name());
    }

    // Section captions, surfaces.
    s += QStringLiteral(
        "QLabel[dlRole=\"section\"] { color:%1; font-size:%2pt; font-weight:600;"
        " letter-spacing:1px; padding:2px 0px; }"
        "QWidget[dlRole=\"panel\"] { background:%3; border:1px solid %4; border-radius:10px; }"
        "QWidget[dlRole=\"strip\"] { background:%5; border:none; border-bottom:1px solid %4; }")
        .arg(muted.name()).arg(smallPt)
        .arg(panelBg.name(), border.name(), mix(window, base, isDark ? 0.2 : 0.8).name());
    Q_UNUSED(subtle);
    return s;
}

}  // namespace

// ---- components (session 117) ---------------------------------------------------

double contrastFloor() { return UiColor::activeTheme() == 6 ? 7.0 : 4.5; }

QColor chipFill(Tone tone)
{
    const QPalette p = qApp->palette();
    const QColor window = p.color(QPalette::Window);
    const bool isDark = window.lightness() < 128;
    if (tone == Tone::Neutral)
        return mix(window, p.color(QPalette::WindowText), isDark ? 0.12 : 0.08);
    return mix(window, toneInk(tone), isDark ? 0.20 : 0.13);
}

QColor chipText(Tone tone)
{
    return UiColor::withContrast(toneInk(tone), chipFill(tone), contrastFloor());
}

void makeChip(QWidget *w, Tone tone)
{
    if (!w) return;
    w->setProperty("dlRole", QStringLiteral("chip"));
    w->setAttribute(Qt::WA_StyledBackground, true);
    setTone(w, tone);
}

void setTone(QWidget *w, Tone tone)
{
    if (!w) return;
    w->setProperty("dlTone", toneName(tone));
    repolish(w);
}

Tone toneOf(const QWidget *w)
{
    const QString t = w ? w->property("dlTone").toString() : QString();
    if (t == QLatin1String("accent")) return Tone::Accent;
    if (t == QLatin1String("ok"))     return Tone::Ok;
    if (t == QLatin1String("warn"))   return Tone::Warn;
    if (t == QLatin1String("fail"))   return Tone::Fail;
    return Tone::Neutral;
}

QWidget *segmented(const QStringList &labels, QButtonGroup **group, QWidget *parent)
{
    auto *box = new QWidget(parent);
    box->setProperty("dlRole", QStringLiteral("segmented"));
    box->setAttribute(Qt::WA_StyledBackground, true);
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto *g = new QButtonGroup(box);
    g->setExclusive(true);
    for (int i = 0; i < labels.size(); ++i) {
        auto *b = new QPushButton(labels.at(i), box);
        b->setCheckable(true);
        b->setChecked(i == 0);
        b->setCursor(Qt::PointingHandCursor);
        g->addButton(b, i);
        row->addWidget(b);
    }
    if (group) *group = g;
    return box;
}

void makeRailButton(QAbstractButton *b, const QString &tip)
{
    if (!b) return;
    b->setProperty("dlRole", QStringLiteral("rail"));
    b->setToolTip(tip);
    b->setAccessibleName(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setIconSize(QSize(18, 18));
    repolish(b);
}

void makePrimary(QAbstractButton *b)
{
    if (!b) return;
    b->setProperty("dlRole", QStringLiteral("primary"));
    repolish(b);
}

void makeSectionLabel(QLabel *l)
{
    if (!l) return;
    l->setProperty("dlRole", QStringLiteral("section"));
    l->setText(l->text().toUpper());
    repolish(l);
}

void makePanel(QWidget *w)
{
    if (!w) return;
    w->setProperty("dlRole", QStringLiteral("panel"));
    w->setAttribute(Qt::WA_StyledBackground, true);
    repolish(w);
}

void makeStrip(QWidget *w)
{
    if (!w) return;
    w->setProperty("dlRole", QStringLiteral("strip"));
    w->setAttribute(Qt::WA_StyledBackground, true);
    repolish(w);
}

void makeMono(QWidget *w)
{
    if (!w) return;
    QFont f = monoFont();
    if (w->font().pointSizeF() > 0) f.setPointSizeF(w->font().pointSizeF());
    w->setFont(f);
}

int space(int step) { return 4 * qMax(0, step); }

QString durationText(qint64 ms)
{
    if (ms < 0) return QStringLiteral("--");
    if (ms < 1000) return QStringLiteral("%1 ms").arg(ms);
    const qint64 s = ms / 1000;
    if (s < 60) return QStringLiteral("%1 s").arg(ms / 1000.0, 0, 'f', 1);
    if (s < 3600) return QStringLiteral("%1m %2s").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    if (s < 86400) return QStringLiteral("%1h %2m").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1d %2h").arg(s / 86400).arg((s % 86400) / 3600, 2, 10, QLatin1Char('0'));
}

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
        // Session 117: a quieter border (the fill carries the shape), a
        // little more room, and medium weight so a button reads as a control
        // next to a label of the same size.
        "QPushButton, QToolButton {"
        " background:%1; color:%2; border:1px solid %3; border-radius:%4px;"
        " padding:6px 14px; font-weight:500; }"
        "QPushButton:hover, QToolButton:hover { background:%5; }"
        "QPushButton:pressed, QToolButton:pressed { background:%6; }"
        "QPushButton:default { border:1px solid %7; }"
        "QPushButton:disabled, QToolButton:disabled {"
        " background:%1; color:%8; border-color:%9; }")
        .arg(button.name(), p.color(QPalette::ButtonText).name(), subtle.name())
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
        " padding:5px 8px; selection-background-color:%5;"
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
        // Session 117: the header is a quiet caption (muted, a hairline under
        // it, no separators between columns), and a view sits on its panel
        // with a hairline frame rather than a box.
        "QHeaderView::section {"
        " background:%1; color:%2; border:none;"
        " border-bottom:1px solid %3; border-right:1px solid %1;"
        " padding:5px 8px; font-weight:600; }"
        "QHeaderView::section:hover { background:%5; }"
        "QTableView, QTreeView, QListView {"
        " background:%6; alternate-background-color:%7;"
        " border:1px solid %4; border-radius:%8px;"
        " gridline-color:%9; }"
        // The cell padding stays where it was: column widths elsewhere are
        // computed for it, and 3px 6px clipped the Time column (session 118).
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

    s += componentRules(p, isDark, window, base, text, border, subtle, hover);
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

// ---- icons (session 117) ----------------------------------------------------------

namespace UiIcons {

QStringList names()
{
    return { "log", "dmi", "track", "serial", "send", "report", "settings",
             "search", "twoloco", "more", "pin", "filter" };
}

QIcon icon(const QString &name, const QColor &color, int px)
{
    // Drawn at 2x and 1x so it is crisp on either kind of screen.
    QIcon out;
    for (int scale : { 1, 2 }) {
        const int s = px * scale;
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter g(&pm);
        g.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(color, 1.8 * scale * px / 18.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        g.setPen(pen);
        g.setBrush(Qt::NoBrush);
        const qreal u = s / 24.0;           // paths in a 24-unit box
        auto P = [u](qreal x, qreal y) { return QPointF(x * u, y * u); };
        QPainterPath path;
        if (name == QLatin1String("log")) {
            path.moveTo(P(4, 6));  path.lineTo(P(20, 6));
            path.moveTo(P(4, 12)); path.lineTo(P(20, 12));
            path.moveTo(P(4, 18)); path.lineTo(P(14, 18));
        } else if (name == QLatin1String("dmi")) {
            path.addEllipse(P(12, 12), 8 * u, 8 * u);
            path.moveTo(P(12, 12)); path.lineTo(P(16, 9));
        } else if (name == QLatin1String("track")) {
            path.moveTo(P(3, 16)); path.lineTo(P(21, 16));
            path.moveTo(P(6, 16)); path.lineTo(P(6, 13));
            path.moveTo(P(11, 16)); path.lineTo(P(11, 9));
            path.moveTo(P(16, 16)); path.lineTo(P(16, 11));
        } else if (name == QLatin1String("serial")) {
            path.addRoundedRect(QRectF(P(4, 8), P(20, 16)), 2 * u, 2 * u);
            for (qreal x : { 8.0, 12.0, 16.0 }) { path.moveTo(P(x, 12)); path.lineTo(P(x + 0.01, 12)); }
        } else if (name == QLatin1String("send")) {
            path.moveTo(P(5, 12)); path.lineTo(P(19, 12));
            path.moveTo(P(12, 5)); path.lineTo(P(19, 12)); path.lineTo(P(12, 19));
        } else if (name == QLatin1String("report")) {
            path.moveTo(P(7, 3)); path.lineTo(P(14, 3)); path.lineTo(P(19, 8));
            path.lineTo(P(19, 21)); path.lineTo(P(7, 21)); path.closeSubpath();
            path.moveTo(P(14, 3)); path.lineTo(P(14, 8)); path.lineTo(P(19, 8));
        } else if (name == QLatin1String("settings")) {
            path.addEllipse(P(12, 12), 3 * u, 3 * u);
            path.moveTo(P(12, 3)); path.lineTo(P(12, 6));
            path.moveTo(P(12, 18)); path.lineTo(P(12, 21));
            path.moveTo(P(3, 12)); path.lineTo(P(6, 12));
            path.moveTo(P(18, 12)); path.lineTo(P(21, 12));
        } else if (name == QLatin1String("search")) {
            path.addEllipse(P(11, 11), 7 * u, 7 * u);
            path.moveTo(P(20, 20)); path.lineTo(P(16.5, 16.5));
        } else if (name == QLatin1String("twoloco")) {
            path.moveTo(P(2, 17)); path.lineTo(P(22, 17));
            path.addRoundedRect(QRectF(P(3, 10), P(9, 15)), u, u);
            path.addRoundedRect(QRectF(P(14, 10), P(20, 15)), u, u);
        } else if (name == QLatin1String("more")) {
            for (qreal x : { 6.0, 12.0, 18.0 }) { path.moveTo(P(x, 12)); path.lineTo(P(x + 0.01, 12)); }
            pen.setWidthF(pen.widthF() * 1.6);
            g.setPen(pen);
        } else if (name == QLatin1String("pin")) {
            path.moveTo(P(9, 4)); path.lineTo(P(15, 4)); path.lineTo(P(14, 10));
            path.lineTo(P(17, 13)); path.lineTo(P(7, 13)); path.lineTo(P(10, 10)); path.closeSubpath();
            path.moveTo(P(12, 13)); path.lineTo(P(12, 20));
        } else if (name == QLatin1String("filter")) {
            path.moveTo(P(4, 5)); path.lineTo(P(20, 5)); path.lineTo(P(14, 12));
            path.lineTo(P(14, 19)); path.lineTo(P(10, 17)); path.lineTo(P(10, 12)); path.closeSubpath();
        } else {
            path.addRect(QRectF(P(5, 5), P(19, 19)));     // unknown name: a visible box, not nothing
        }
        g.drawPath(path);
        g.end();
        pm.setDevicePixelRatio(scale);
        out.addPixmap(pm);
    }
    return out;
}

QIcon icon(const QString &name)
{
    return icon(name, qApp ? qApp->palette().color(QPalette::WindowText) : QColor(Qt::black));
}

}  // namespace UiIcons
