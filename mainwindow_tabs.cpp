// =============================================================================
//  mainwindow_tabs.cpp -- tab lifecycle: create, show/hide, close, pop out, tag, clear
//  Part of MainWindow: split out of mainwindow.cpp in session 89 so each
//  concern can be read, and recompiled, on its own. No behaviour change.
// =============================================================================
#include "mainwindow.h"
#include <QSettings>
#include "frameclock.h"
#include "pinpanel.h"
#include "watchpanel.h"
#include "framenumberwatch.h"
#include "emptystate.h"
#include "uistyle.h"
#include "uicolors.h"
#include "ui_mainwindow.h"

#include "exporter.h"
#include "exportdialog.h"
#include "commandpalette.h"
#include "comparewindow.h"
#include "archivesearchwindow.h"
#include "fieldcatalog.h"
#include "fieldindexdialog.h"
#include "fieldinspector.h"
#include "fieldplot.h"
#include "filterbar.h"
#include "findbar.h"
#include "gototimestampdialog.h"
#include "logmodel.h"
#include "logtableview.h"
#include "logwriter.h"
#include "rawbytespanel.h"
#include "markerscrollbar.h"
#include "mergedwindow.h"
#include "notificationcenter.h"
#include "searchwindow.h"
#include "savedata.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "settings.h"
#include "settingsdialog.h"
#include "stickymenu.h"
#include "schema/schemadecoder.h"
#include "timelineribbon.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDateTime>
#include <QClipboard>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMouseEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QMessageBox>
#include <QMetaType>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QStatusBar>
#include <QTabBar>
#include <QTableView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <limits>

#include "lococonsolewindow.h"
#include "brakingpanel.h"
#include "faultpanelwindow.h"
#include "decodeworkbench.h"
#include "flasherwindow.h"
#include "lococonfigwindow.h"
#include "minimizeddock.h"
#include "statuspins.h"
#include "presentationmode.h"
#include "tabpopoutwindow.h"
#include "undolog.h"
#include "clockskewalarm.h"
#include "dmipanel.h"
#include "dmitimetravel.h"
#ifdef DL_HAVE_SERIAL
#include "serialconsolewindow.h"
#endif
#include "runreportwindow.h"
#include "speeddistance.h"
#include "workspacesnapshot.h"
#include "tabtags.h"
#include "textzoom.h"
#include "windowgeometry.h"
#include "fieldsweepdialog.h"
#include "framediffwindow.h"
#include "roundtripwindow.h"
#include "packetmakerdialog.h"
#include "packetsequencedialog.h"
#include "dlrplayerdialog.h"
#include "sessionkeydialog.h"
#include "sessionkeystore.h"

// tshark -i any -f "udp port 50002" -T fields -e data

void MainWindow::buildOrShowTab(const QString &tabKey,
                                const QString &friendlyName)
{
    auto it = m_tabs.find(tabKey);
    if (it != m_tabs.end()) {
        // Already known. If hidden, re-attach its container to the
        // tab widget. If visible, do nothing (entries route via the
        // existing UI). The friendlyName argument is used only when
        // creating fresh; it's identical on every entry for a given
        // source so we don't need to update it on re-show.
        if (!it->visible) {
            showTab(*it);
        }
        return;
    }
    // Creates when needed (session 112): a tab restored at start-up comes
    // before its source's traffic. Fine here, unlike in a mere lookup —
    // this IS the tab UI, so nothing waits on an announcement.
    LogModel *model = m_dispatcher->ensureModel(tabKey);
    if (!model) return;

    // Per-tab filter bar — owns its proxy model.
    auto *filterBar = new FilterBar(model);
    filterBar->setNameMap(&m_nameMap);   // enables name: in query mode

    // Build the table view.
    auto *view = new QTableView;
    view->setModel(filterBar->proxyModel());

    // An empty tab is one of two quite different situations, and until now
    // both looked identical: nothing has arrived from this source, or forty
    // thousand rows are sitting behind a filter that matches none of them.
    // Asking the SOURCE model rather than the proxy is what tells them apart.
    EmptyState::attach(view, [model, friendlyName]() -> QString {
        if (model->count() == 0) {
            return QObject::tr("Waiting for traffic from %1.").arg(friendlyName);
        }
        return QObject::tr("No rows match the filter.\n"
                           "%1 message(s) are hidden by it.").arg(model->count());
    });
    view->setAlternatingRowColors(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    // ExtendedSelection: shift-click a span, ctrl-click to add. Copying a
    // block of rows into an incident report is the single most common
    // thing anyone does with this table, and single-selection made it a
    // row-at-a-time job.
    //
    // The raw-bytes and field panels follow the CURRENT row rather than the
    // selection, so they stay meaningful during a multi-row selection —
    // "the bytes of these fifty rows" is not a thing they could show.
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Row sizing — see Patch B fix notes. Fixed 44 px is the only
    // height that scales to thousands of rows without locking up the
    // view; the inserts cost ~700 rows/sec rather than ~19.
    view->setTextElideMode(Qt::ElideRight);
    view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    applyDensity(view);

    view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view, &QWidget::customContextMenuRequested,
            this, &MainWindow::onLogRowContextMenu);
    view->verticalHeader()->setVisible(false);

    QHeaderView *hh = view->horizontalHeader();
    hh->setStretchLastSection(true);
    view->setColumnWidth(LogModel::ColTime,      100);
    view->setColumnWidth(LogModel::ColSource,     60);
    view->setColumnWidth(LogModel::ColFriendly,  120);
    view->setColumnWidth(LogModel::ColDirection,  40);
    view->setColumnWidth(LogModel::ColSeverity,   66);   // fits "✕ ERR"
    applyColumnWidths(view);      // stored widths win over these defaults
    applyColumnVisibility(view);
    connect(view->horizontalHeader(), &QHeaderView::sectionResized,
            this, [this](int, int, int) { scheduleLayoutSave(); });
    view->setColumnHidden(LogModel::ColTime, !ui->cbDatetime->isChecked());

    // Row-selection → raw bytes side panel.
    connect(view->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this, [this, tabKey](const QModelIndex &cur, const QModelIndex &) {
                const LogEntryPtr e = entryFromProxyIndex(cur);
                m_rawPanel->showEntry(e);
                // Decode after the hex dump exists — the inspector's byte
                // highlight writes into it.
                if (m_fieldPanel) m_fieldPanel->showEntry(e);
                // Session 84: the DMI window, following, shows this moment.
                offerDmiMoment(tabKey, cur);
            });

    // FindBar (Ctrl+F): hidden by default; activate() shows + focuses.
    // Goes between the filter bar and the view, so when it's visible
    // the layout reads top-down: filter / find / table.
    auto *findBar = new FindBar(view);
    // "All tabs" (session 82): the same search in the search window,
    // grouped by tab.
    connect(findBar, &FindBar::searchAllTabs, this, [this](const QString &query) {
        onActionSearchAll();
        if (m_searchWin) {
            m_searchWin->setGroupByTab(true);
            m_searchWin->setQueryText(query, true);
        }
    });
    // Names the tab in the floating window's title bar: with a find bar per
    // tab, a window labelled only "Find" can be searching a tab that is not
    // even on screen.
    findBar->setContextLabel(friendlyName);
    // The bar needs to know whether this tab follows the tail, so it can
    // say that following is on hold rather than leaving a frozen tab
    // unexplained; and MainWindow needs to know when the hold is released,
    // so the tab can catch up to the end in one hop instead of waiting for
    // the next batch.
    connect(findBar, &FindBar::holdChanged, this, [this, tabKey](bool held) {
        auto tabIt = m_tabs.find(tabKey);
        if (tabIt == m_tabs.end()) return;
        if (!held && tabIt->scrollLock && tabIt->view) {
            tabIt->view->scrollToBottom();
        }
    });

    // Density strip above the table, and severity marks on the scrollbar.
    // Both read the same model but are indexed differently on purpose: the
    // ribbon by TIME, the scrollbar by ROW. A quiet hour is wide on one and
    // nearly absent from the other, so each answers a question the other
    // cannot.
    auto *ribbon = new TimelineRibbon;
    ribbon->setModel(model);
    auto *marks = new MarkerScrollBar;
    marks->setModels(filterBar->proxyModel(), model);
    view->setVerticalScrollBar(marks);

    connect(ribbon, &TimelineRibbon::timeClicked,
            this, [this, tabKey](qint64 ms) { jumpToEntry(tabKey, ms); });
    connect(ribbon, &TimelineRibbon::rangeSelected,
            this, [this, tabKey](qint64 from, qint64 to) {
                applyTimeRangeFilter(tabKey, from, to);
            });

    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(ribbon);
    layout->addWidget(filterBar);
    layout->addWidget(findBar);
    layout->addWidget(view, 1);
    auto *container = new QWidget;
    container->setLayout(layout);
    const int idx = ui->tabWidget->addTab(container, friendlyName);

    TabUi tab;
    tab.tabKey       = tabKey;
    tab.view         = view;
    tab.filterBar    = filterBar;
    tab.findBar      = findBar;
    findBar->setScrollLockActive(tab.scrollLock);
    tab.ribbon       = ribbon;
    tab.marks        = marks;
    tab.container    = container;
    tab.friendlyName = friendlyName;
    tab.visible      = true;
    tab.lastSeenMs   = QDateTime::currentMSecsSinceEpoch();
    m_tabs[tabKey]      = tab;
    rebuildSourceList();      // new source: add it to the index
    updateEmptyState();       // first traffic replaces the placeholder
    m_tabIndex[tabKey]  = idx;
    applyTabTag(tabKey);
    reopenPendingPopout(tabKey);
}

void MainWindow::showTab(TabUi &tab)
{
    if (tab.visible) return;
    // Re-add the container widget to the tab widget. Qt remembers
    // ownership: addTab on a widget that already exists just inserts
    // it into the bar. We append to the right-most position; reorder
    // is up to the user.
    const int idx = ui->tabWidget->addTab(tab.container, tab.friendlyName);
    tab.visible = true;
    m_tabIndex[tab.tabKey] = idx;
    applyTabTag(tab.tabKey);
    // Refresh m_tabIndex for any tabs that shifted left when this one
    // was removed earlier (though we always re-append at the end so
    // shifts only happen on hide, not show).
}

void MainWindow::hideTab(TabUi &tab)
{
    if (!tab.visible) return;
    auto it = m_tabIndex.find(tab.tabKey);
    if (it == m_tabIndex.end()) return;
    const int idx = it.value();
    // removeTab does NOT delete the widget — it just detaches it. The
    // container therefore lives on (parented to MainWindow now since
    // the tab widget no longer owns it), and we can re-add it later.
    ui->tabWidget->removeTab(idx);
    tab.container->setParent(this);
    tab.container->hide();
    tab.visible = false;
    m_tabIndex.remove(tab.tabKey);
    // Other tabs to the right of this one shifted left by one index.
    // Update their stored indices.
    for (auto jt = m_tabIndex.begin(); jt != m_tabIndex.end(); ++jt) {
        if (jt.value() > idx) jt.value()--;
    }
}

// -----------------------------------------------------------------------
//  Dispatcher slots
// -----------------------------------------------------------------------
void MainWindow::onTabRequested(QString tabKey, QString friendlyName)
{
    buildOrShowTab(tabKey, friendlyName);
}

void MainWindow::onTabCloseRequested(int index)
{
    // Resolve the tab key for this index, then hide (don't destroy).
    QString key;
    for (auto it = m_tabIndex.constBegin(); it != m_tabIndex.constEnd(); ++it) {
        if (it.value() == index) { key = it.key(); break; }
    }
    if (key.isEmpty()) return;

    auto it = m_tabs.find(key);
    if (it == m_tabs.end()) return;
    hideTab(*it);
}

void MainWindow::onTabContextMenu(const QPoint &pos)
{
    QTabBar *bar = ui->tabWidget->tabBar();
    const int idx = bar->tabAt(pos);
    if (idx < 0) return;

    // Look up the key for this tab so the menu actions know what to act on.
    QString key;
    for (auto it = m_tabIndex.constBegin(); it != m_tabIndex.constEnd(); ++it) {
        if (it.value() == idx) { key = it.key(); break; }
    }
    if (key.isEmpty()) return;

    QMenu menu(this);
    QAction *actClose       = menu.addAction(tr("Close"));
    QAction *actCloseOthers = menu.addAction(tr("Close Others"));
    menu.addSeparator();
    QAction *actDetach = menu.addAction(tr("Pop Out to Window"));
    actDetach->setToolTip(tr("A live copy of this tab in its own window (or drag the tab out of the bar)"));
    // Colour tag: follows this loco into its pop-out window, its minimise
    // chip and the Live Loco Console.
    QMenu *tagMenu = menu.addMenu(tr("Colour Tag"));
    const TabTag currentTag = m_tabTags->tag(key);
    QAction *actNoTag = tagMenu->addAction(tr("None"));
    actNoTag->setCheckable(true);
    actNoTag->setChecked(currentTag.color < 0);
    QList<QAction *> colourActions;
    for (int colour = 0; colour < UiColor::kTagCount; ++colour) {
        QAction *item = tagMenu->addAction(TabTags::dotIconFor(colour), UiColor::tagName(colour));
        item->setCheckable(true);
        item->setChecked(currentTag.color == colour);
        colourActions.append(item);
    }
    tagMenu->addSeparator();
    QAction *actLabel = tagMenu->addAction(tr("Label…"));
    QAction *actSave   = menu.addAction(tr("Save Log…"));
    QAction *actClear  = menu.addAction(tr("Clear Tab Logs"));

    QAction *chosen = menu.exec(bar->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == actClose) {
        onTabCloseRequested(idx);
        return;
    }
    if (chosen == actCloseOthers) {
        // Hide every visible tab whose key isn't `key`. Iterating
        // m_tabs (the source of truth) rather than tab indices because
        // hiding shifts indices around.
        QStringList toHide;
        for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
            if (it.value().visible && it.key() != key) {
                toHide.append(it.key());
            }
        }
        for (const QString &k : toHide) {
            auto it = m_tabs.find(k);
            if (it != m_tabs.end()) hideTab(*it);
        }
        return;
    }
    if (chosen == actDetach) {
        popOutTab(key);
        return;
    }
    if (chosen == actNoTag) {
        m_tabTags->setColor(key, -1);
        return;
    }
    if (colourActions.contains(chosen)) {
        m_tabTags->setColor(key, colourActions.indexOf(chosen));
        return;
    }
    if (chosen == actLabel) {
        bool ok = false;
        const QString label = QInputDialog::getText(
            this, tr("Tag label"),
            tr("Short label for %1 (empty to remove):").arg(m_tabs.value(key).friendlyName),
            QLineEdit::Normal, currentTag.label, &ok);
        if (ok) {
            m_tabTags->setLabel(key, label);
        }
        return;
    }
    if (chosen == actSave) {
        ui->tabWidget->setCurrentIndex(idx);
        on_pbSaveBtn_clicked();
        return;
    }
    if (chosen == actClear) {
        ui->tabWidget->setCurrentIndex(idx);
        on_pbClearTabLogs_clicked();
        return;
    }
}

void MainWindow::on_pbDetach_clicked()
{
    const QString currentKey = currentTabKey();
    if (currentKey.isEmpty()) return;
    popOutTab(currentKey);
}

// -----------------------------------------------------------------------
//  Pop-out windows: a live copy of a tab in its own window
// -----------------------------------------------------------------------
void MainWindow::popOutTab(const QString &key, const QPoint &globalPos)
{
    // One per tab: a second request raises the one already open.
    if (TabPopoutWindow *existing = m_popouts.value(key)) {
        existing->showNormal();
        existing->raise();
        existing->activateWindow();
        return;
    }
    LogModel *model = m_dispatcher ? m_dispatcher->modelForKey(key) : nullptr;
    if (!model) return;

    // The same name the tab shows (a restored workspace names its tabs;
    // the dispatcher might only know the key).
    QString name = m_tabs.value(key).friendlyName;
    if (name.isEmpty()) {
        name = m_dispatcher->friendlyNameFor(key);
    }
    auto *window = new TabPopoutWindow(key, name, model, m_dispatcher, this, m_tabTags);
    window->setAttribute(Qt::WA_DeleteOnClose);
    // Follow the newest row if the tab does (its scroll lock).
    auto it = m_tabs.constFind(key);
    if (it != m_tabs.constEnd()) {
        window->setFollowLatest(it->scrollLock);
    }
    // Dropped from a tab drag: put the window where the tab was let go.
    if (!globalPos.isNull()) {
        window->move(globalPos - QPoint(40, 12));
    }
    connect(window, &TabPopoutWindow::showInMainRequested, this, [this](const QString &tabKey) {
        auto tabIt = m_tabs.find(tabKey);
        if (tabIt == m_tabs.end()) return;
        showTab(*tabIt);
        ui->tabWidget->setCurrentWidget(tabIt->container);
        showNormal();
        raise();
        activateWindow();
    });
    connect(window, &TabPopoutWindow::closedByUser, this, [this](const QString &tabKey) {
        m_popouts.remove(tabKey);
        if (!m_shuttingDown) {
            savePopoutKeys();
        }
    });
    m_popouts.insert(key, window);
    window->show();
    savePopoutKeys();
}

void MainWindow::savePopoutKeys() const
{
    QStringList keys;
    for (auto it = m_popouts.constBegin(); it != m_popouts.constEnd(); ++it) {
        if (it.value()) {
            keys.append(it.key());
        }
    }
    // Keys waiting to be reopened (their loco not heard from yet this run)
    // are still wanted: keep them.
    for (const QString &pending : m_pendingPopouts) {
        if (!keys.contains(pending)) {
            keys.append(pending);
        }
    }
    keys.sort();
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("ui/popouts"), keys);
}

void MainWindow::reopenPendingPopout(const QString &key)
{
    if (!m_pendingPopouts.contains(key)) return;
    m_pendingPopouts.removeAll(key);
    // Deferred: the tab is still being built.
    QTimer::singleShot(0, this, [this, key]() { popOutTab(key); });
}

// -----------------------------------------------------------------------
//  Tab colour tags: a dot before the name (the name's colour stays free
//  for tab health), and the label after it.
// -----------------------------------------------------------------------
void MainWindow::applyTabTag(const QString &key)
{
    if (key.isEmpty()) {
        applyAllTabTags();
        return;
    }
    auto it = m_tabs.constFind(key);
    if (it == m_tabs.constEnd() || !it->visible) return;
    const int index = ui->tabWidget->indexOf(it->container);
    if (index < 0) return;
    ui->tabWidget->setTabIcon(index, m_tabTags->dotIcon(key));
    ui->tabWidget->setTabText(index, m_tabTags->decoratedName(key, it->friendlyName));
}

void MainWindow::applyAllTabTags()
{
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        applyTabTag(it.key());
    }
}

void MainWindow::rebuildWindowMenu()
{
    // Remove ONLY the dynamic per-tab entries we added on a previous
    // call. The first two actions of m_menuWindow ("Show All Hidden
    // Tabs" + separator) are permanent and were created in the
    // constructor — clearing them would destroy them (QMenu::clear
    // deletes its actions) and break their signal connections.
    const QList<QAction*> all = m_menuWindow->actions();
    for (int i = 2; i < all.size(); ++i) {
        m_menuWindow->removeAction(all[i]);
        all[i]->deleteLater();   // clean up the action we owned
    }

    bool anyHidden = false;
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        const TabUi &t = it.value();
        if (t.visible) continue;
        anyHidden = true;
        QAction *a = m_menuWindow->addAction(t.friendlyName);
        const QString key = t.tabKey;
        connect(a, &QAction::triggered, this, [this, key]() {
            auto it = m_tabs.find(key);
            if (it != m_tabs.end()) showTab(*it);
        });
    }
    if (!anyHidden) {
        QAction *placeholder = m_menuWindow->addAction(tr("(no hidden tabs)"));
        placeholder->setEnabled(false);
    }
}

void MainWindow::onActionShowAllTabs()
{
    QStringList toShow;
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        if (!it.value().visible) toShow.append(it.key());
    }
    for (const QString &k : toShow) {
        auto it = m_tabs.find(k);
        if (it != m_tabs.end()) showTab(*it);
    }
}

void MainWindow::on_tabWidget_currentChanged(int index)
{
    if (index < 0) return;

    // The chooser's "seen here" half belongs to the tab in front. Only when
    // the panel is open — walking a model to fill a chooser nobody is looking
    // at is work for nothing.
    if (m_pinDock && m_pinDock->isVisible()) { refreshPinChoices(); }

    // The 'index' parameter is what Qt gave us, but currentTabKey() does
    // the reverse lookup against currentIndex(). They're equivalent here.
    const QString currentKey = currentTabKey();
    if (!currentKey.isEmpty()) {
        auto it = m_tabs.constFind(currentKey);
        if (it != m_tabs.constEnd()) {
            ui->cbScrollLock->setCheckState(
                it->scrollLock ? Qt::Checked : Qt::Unchecked);
        }
    }
    updateLogCount();
}

QString MainWindow::currentTabKey() const
{
    const int idx = ui->tabWidget->currentIndex();
    if (idx < 0) return QString();
    for (auto it = m_tabIndex.constBegin(); it != m_tabIndex.constEnd(); ++it) {
        if (it.value() == idx) return it.key();
    }
    return QString();
}

void MainWindow::on_pbClearTabLogs_clicked()
{
    const QString currentKey = currentTabKey();
    if (currentKey.isEmpty()) return;

    LogModel *model = m_dispatcher->modelForKey(currentKey);
    if (!model) return;
    // Taken rather than dropped, so Ctrl+Z can put them back (session 79).
    const QVector<LogEntryPtr> rows = model->takeAll();
    QString name = m_tabs.value(currentKey).friendlyName;
    if (name.isEmpty()) name = currentKey;
    if (rows.isEmpty()) {
        notify(NoteLevel::Info, tr("Tab %1 was already empty").arg(name));
        return;
    }
    QPointer<LogModel> guarded(model);
    QPointer<MainWindow> self(this);
    m_undo->push(tr("Clear tab %1").arg(name), [self, guarded, rows, currentKey]() {
        if (!self || !guarded) return false;
        guarded->restoreOlder(rows);
        self->m_bookmarks.applyToModel(guarded, currentKey);
        self->updateLogCount();
        return true;
    }, rows.size());

    notify(NoteLevel::Info, tr("Tab %1 cleared (%2 rows). Ctrl+Z brings them back.")
                                .arg(name).arg(rows.size()));
    updateLogCount();
}

void MainWindow::on_pbclearAllLogs_clicked()
{
    QVector<QPair<QPointer<LogModel>, QVector<LogEntryPtr>>> taken;
    QVector<QString> keys;
    qint64 total = 0;
    for (const TabUi &t : qAsConst(m_tabs)) {
        LogModel *m = m_dispatcher->modelForKey(t.tabKey);
        if (!m) continue;
        const QVector<LogEntryPtr> rows = m->takeAll();
        if (rows.isEmpty()) continue;
        total += rows.size();
        taken.append({ QPointer<LogModel>(m), rows });
        keys.append(t.tabKey);
    }
    if (!taken.isEmpty()) {
        QPointer<MainWindow> self(this);
        m_undo->push(tr("Clear all tabs"), [self, taken, keys]() {
            if (!self) return false;
            bool any = false;
            for (int i = 0; i < taken.size(); ++i) {
                if (!taken.at(i).first) continue;
                taken.at(i).first->restoreOlder(taken.at(i).second);
                self->m_bookmarks.applyToModel(taken.at(i).first, keys.at(i));
                any = true;
            }
            self->updateLogCount();
            return any;
        }, total);
    }
    notify(NoteLevel::Info, taken.isEmpty() ? tr("All tabs were already empty")
                                            : tr("All tabs cleared (%1 rows). Ctrl+Z brings them back.").arg(total));
    updateLogCount();
}
