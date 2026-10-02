// =============================================================================
//  mainwindow_shell.cpp — the main window's frame (session 118, UI revamp 2)
//  -----------------------------------------------------------------------------
//  "Control Room": a strip of live status across the top, an icon rail down
//  the left, and a header over the log in place of the column of buttons
//  beside it. Built from the components of session 117 (uistyle.h).
//
//  NOTHING IS REMOVED, ONLY MOVED
//    Every rail button is an EXISTING menu action (found by its menu text,
//    so the two cannot drift apart); the menus keep every command. The
//    buttons that stood beside the log keep their objects and slots — the
//    header holds the everyday ones, the "more" menu clicks the others — so
//    the auto-connected slots, the tests that click them by name and Undo
//    all work unchanged. The live labels (UDP bind, the two clocks and their
//    gap, the serial chips) move from the status bar to the top strip; the
//    status bar keeps the notices, pins, queue, drops and disk.
// =============================================================================

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "uicolors.h"
#include "uistyle.h"

#include <QAction>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>

namespace {

// An action anywhere in the menu bar, by its text without '&' and the
// trailing ellipsis. Null when absent (a build without that feature).
QAction *findMenuAction(QMenuBar *bar, const QString &text)
{
    std::function<QAction *(QMenu *)> walk = [&](QMenu *m) -> QAction * {
        for (QAction *a : m->actions()) {
            if (a->menu()) {
                if (QAction *hit = walk(a->menu())) return hit;
                continue;
            }
            QString t = a->text().remove(QLatin1Char('&'));
            t.remove(QStringLiteral("…")).remove(QStringLiteral("..."));
            if (t.trimmed() == text) return a;
        }
        return nullptr;
    };
    for (QAction *top : bar->actions())
        if (top->menu())
            if (QAction *hit = walk(top->menu())) return hit;
    return nullptr;
}

QWidget *hSpacer(QWidget *parent)
{
    auto *w = new QWidget(parent);
    w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return w;
}

}  // namespace

void MainWindow::buildShell()
{
    // ---- the top strip ---------------------------------------------------------------
    m_strip = new QToolBar(tr("Status strip"), this);
    m_strip->setObjectName(QStringLiteral("statusStrip"));
    m_strip->setMovable(false);
    m_strip->setFloatable(false);
    m_strip->toggleViewAction()->setVisible(false);     // always there, like the menu bar
    UiStyle::makeStrip(m_strip);
    addToolBar(Qt::TopToolBarArea, m_strip);

    auto *brand = new QLabel(QStringLiteral("DLConsole"), m_strip);
    brand->setObjectName(QStringLiteral("brandLabel"));
    brand->setStyleSheet(QStringLiteral("font-weight:600; padding:0 12px 0 6px;"));
    m_strip->addWidget(brand);

    // The command palette, as a search field you can click: it already
    // reaches every menu command by name.
    auto *search = new QPushButton(UiIcons::icon(QStringLiteral("search"), UiColor::muted(), 14),
                                   tr("Search commands, tools, settings…   Ctrl+P"), m_strip);
    search->setObjectName(QStringLiteral("stripSearch"));
    search->setToolTip(tr("Run any command by name (Help ▸ Run command…)"));
    search->setMinimumWidth(320);
    search->setStyleSheet(QStringLiteral("text-align:left; color:%1;").arg(UiColor::muted().name()));
    connect(search, &QPushButton::clicked, this, &MainWindow::onActionCommandPalette);
    m_strip->addWidget(search);
    m_strip->addWidget(hSpacer(m_strip));

    // Live state, moved here from the status bar.
    // In a container of our own: a widget placed straight into a toolbar
    // has its visibility run by the toolbar's action, and removeWidget() had
    // already hidden it — the chip never appeared. (The clocks, below, are
    // in a container too.)
    statusBar()->removeWidget(m_lblBindStatus);
    UiStyle::makeChip(m_lblBindStatus, UiStyle::Tone::Neutral);
    auto *bindBox = new QWidget(m_strip);
    bindBox->setObjectName(QStringLiteral("stripBind"));
    auto *bl = new QHBoxLayout(bindBox);
    bl->setContentsMargins(0, 0, UiStyle::space(1), 0);
    bl->addWidget(m_lblBindStatus);
    m_lblBindStatus->show();
    m_strip->addWidget(bindBox);
    if (m_serialChips) {
        statusBar()->removeWidget(m_serialChips);
        // In a toolbar a widget is shown and hidden through its action.
        m_serialChipsAction = m_strip->addWidget(m_serialChips);
        m_serialChipsAction->setVisible(false);  // rebuildSerialChips() decides from here on
    }
    auto *clocks = new QWidget(m_strip);
    clocks->setObjectName(QStringLiteral("stripClocks"));
    auto *cl = new QHBoxLayout(clocks);
    cl->setContentsMargins(UiStyle::space(3), 0, UiStyle::space(1), 0);
    cl->setSpacing(UiStyle::space(2));
    for (QLabel *l : { m_lblFrameClock, m_lblStationClock, m_lblClockGap }) {
        statusBar()->removeWidget(l);
        UiStyle::makeMono(l);
        cl->addWidget(l);
        l->show();
    }
    m_strip->addWidget(clocks);

    // ---- the rail ----------------------------------------------------------------------
    m_rail = new QToolBar(tr("Tools rail"), this);
    m_rail->setObjectName(QStringLiteral("toolsRail"));
    m_rail->setOrientation(Qt::Vertical);
    m_rail->setMovable(false);
    m_rail->setFloatable(false);
    m_rail->toggleViewAction()->setVisible(false);
    m_rail->setIconSize(QSize(18, 18));
    UiStyle::makeStrip(m_rail);
    addToolBar(Qt::LeftToolBarArea, m_rail);

    struct RailItem { const char *icon; const char *menuText; };
    static const RailItem items[] = {
        { "log",     "All sources, chronological" },
        { "search",  "Search all sources" },
        { "dmi",     "DMI (LP-OCIP)" },
        { "track",   "Track diagram" },
        { "twoloco", "Two-loco view" },
        { "report",  "Incident report" },
        { "serial",  "Serial Port Terminal" },
        { "send",    "Packet Maker" },
    };
    for (const RailItem &it : items) {
        QAction *a = findMenuAction(menuBar(), QString::fromLatin1(it.menuText));
        if (!a) continue;                       // feature not in this build
        auto *b = new QToolButton(m_rail);
        b->setDefaultAction(a);
        b->setIcon(UiIcons::icon(QString::fromLatin1(it.icon), palette().color(QPalette::WindowText)));
        UiStyle::makeRailButton(b, a->text().remove(QLatin1Char('&')).remove(QStringLiteral("…")));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setObjectName(QStringLiteral("rail_%1").arg(QString::fromLatin1(it.icon)));
        m_rail->addWidget(b);
    }
    auto *railGap = new QWidget(m_rail);
    railGap->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_rail->addWidget(railGap);
    if (QAction *settings = findMenuAction(menuBar(), QStringLiteral("Settings"))) {
        auto *b = new QToolButton(m_rail);
        b->setDefaultAction(settings);
        b->setIcon(UiIcons::icon(QStringLiteral("settings"), palette().color(QPalette::WindowText)));
        UiStyle::makeRailButton(b, tr("Settings"));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setObjectName(QStringLiteral("rail_settings"));
        m_rail->addWidget(b);
    }

    // ---- the log header, in place of the button column -------------------------------
    auto *header = new QWidget(ui->page);
    header->setObjectName(QStringLiteral("logHeader"));
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(UiStyle::space(1), UiStyle::space(1), UiStyle::space(1), UiStyle::space(1));
    hl->setSpacing(UiStyle::space(2));

    m_headerTitle = new QLabel(header);
    m_headerTitle->setObjectName(QStringLiteral("logHeaderTitle"));
    m_headerTitle->setStyleSheet(QStringLiteral("font-weight:600; font-size:%1pt;")
                                     .arg(qMax(9.0, font().pointSizeF() + 2)));
    m_headerMeta = new QLabel(header);
    m_headerMeta->setObjectName(QStringLiteral("logHeaderMeta"));
    m_headerMeta->setStyleSheet(UiColor::mutedStyle());
    hl->addWidget(m_headerTitle);
    hl->addWidget(m_headerMeta);
    hl->addStretch(1);

    // The count, as a quiet mono readout.
    ui->label_2->hide();
    UiStyle::makeMono(ui->logCountLb);
    ui->logCountLb->setStyleSheet(UiColor::mutedStyle());
    ui->logCountLb->setToolTip(tr("Messages in this tab"));
    hl->addWidget(ui->logCountLb);

    hl->addWidget(ui->cbScrollLock);
    hl->addWidget(ui->cbDatetime);
    hl->addWidget(ui->pbSaveBtn);
    hl->addWidget(ui->pbDetach);

    // The rest behind "more": the buttons themselves stay (hidden), so their
    // slots, Undo and the tests that click them by name are untouched.
    m_moreBtn = new QToolButton(header);
    m_moreBtn->setObjectName(QStringLiteral("logMore"));
    m_moreBtn->setIcon(UiIcons::icon(QStringLiteral("more"), palette().color(QPalette::WindowText), 16));
    m_moreBtn->setToolTip(tr("More: clear this tab, clear all tabs, check buffer"));
    m_moreBtn->setAccessibleName(tr("More actions"));
    m_moreBtn->setPopupMode(QToolButton::InstantPopup);
    auto *more = new QMenu(m_moreBtn);
    for (QPushButton *b : { ui->pbClearTabLogs, ui->pbclearAllLogs, ui->pbCheckBuffer }) {
        QAction *a = more->addAction(b->text());
        a->setObjectName(QStringLiteral("more_%1").arg(b->objectName()));
        connect(a, &QAction::triggered, b, &QPushButton::click);
    }
    // Check buffer renames itself ("Back to log"): follow it.
    connect(more, &QMenu::aboutToShow, this, [this, more]() {
        const QList<QAction *> acts = more->actions();
        const QList<QPushButton *> btns{ ui->pbClearTabLogs, ui->pbclearAllLogs, ui->pbCheckBuffer };
        for (int i = 0; i < acts.size() && i < btns.size(); ++i) {
            acts[i]->setText(btns[i]->text());
            acts[i]->setEnabled(btns[i]->isEnabled());
        }
    });
    m_moreBtn->setMenu(more);
    hl->addWidget(m_moreBtn);

    ui->verticalLayout->insertWidget(0, header);
    ui->rightFrame->hide();

    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int) { refreshHeader(); });
    // The first currentChanged comes before the new tab is registered (its
    // key is not known yet), so the status tick refreshes it too (wired
    // where that timer is made, after this).
    refreshHeader();
}

void MainWindow::refreshHeader()
{
    if (!m_headerTitle) return;
    const int i = ui->tabWidget->currentIndex();
    if (i < 0) {
        m_headerTitle->setText(tr("No source yet"));
        m_headerMeta->setText(tr("tabs appear as sources report in"));
        return;
    }
    const QString key = currentTabKey();
    const QString shown = ui->tabWidget->tabText(i);
    if (key.isEmpty()) { m_headerTitle->setText(shown); m_headerMeta->clear(); return; }
    m_headerTitle->setText(shown);
    // Say what the tab name does not: the friendly name when the tab shows
    // the key, the key when it shows a name, and "source 81 · channel 2"
    // spelled out either way.
    const QString friendly = m_nameMap.lookupByKey(key);
    const QString src = key.section(QLatin1Char('_'), 0, 0), ch = key.section(QLatin1Char('_'), 1, 1);
    QString meta = tr("source %1 · channel %2").arg(src, ch);
    if (!friendly.isEmpty() && friendly != shown) meta = friendly + QStringLiteral(" · ") + meta;
    else if (shown != key) meta = key + QStringLiteral(" · ") + meta;
    m_headerMeta->setText(meta);
}
