// =============================================================================
//  MainWindow — session 79: window layouts, crash recovery, undo, settings
//  export/import, colour-blind-safe status colours.
//
//  In its own file because mainwindow.cpp is past four thousand lines and
//  these five features share one another's pieces (a layout switch is
//  undoable; an import can replace the layouts) far more than they share
//  anything with the capture pipeline.
// =============================================================================

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "bignumberpanel.h"
#include "clockskewalarm.h"
#include "flasher/flasherwindow.h"
#include "lococonfig/lococonfigwindow.h"
#include "pinpanel.h"
#include "settings.h"
#include "settingsbundle.h"
#include "statuspins.h"
#include "tabpopoutwindow.h"
#include "tabtags.h"
#include "textzoom.h"
#include "theme.h"
#include "uicolors.h"
#include "uistyle.h"
#include "undolog.h"
#include "watchpanel.h"
#include "workspacesnapshot.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// Recovery snapshot: checked this often, written only when it changed.
const int kRecoveryIntervalMs = 5000;
// How long the status-bar "Undo …" offer stays up after a destructive click.
const int kUndoOfferMs = 15000;

// Ticks for the export and import dialogs: one per section, with what it
// holds. Returns false on Cancel.
bool chooseSections(QWidget *parent, const QString &title, const QString &intro,
                    const SettingsBundle::Bundle &bundle,
                    const QList<SettingsBundle::Section> &available,
                    QList<SettingsBundle::Section> *chosen)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(intro, &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    QVector<QPair<SettingsBundle::Section, QCheckBox *>> boxes;
    for (SettingsBundle::Section s : available) {
        auto *box = new QCheckBox(QStringLiteral("%1 — %2").arg(SettingsBundle::label(s), bundle.summary(s)), &dialog);
        box->setChecked(true);
        layout->addWidget(box);
        boxes.append({ s, box });
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto refreshOk = [&boxes, buttons]() {
        bool any = false;
        for (const auto &b : boxes) any = any || b.second->isChecked();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(any);
    };
    for (const auto &b : boxes) {
        QObject::connect(b.second, &QCheckBox::toggled, &dialog, refreshOk);
    }
    refreshOk();
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    chosen->clear();
    for (const auto &b : boxes) {
        if (b.second->isChecked()) chosen->append(b.first);
    }
    return !chosen->isEmpty();
}

QByteArray comparable(const WorkspaceSnapshot &s)
{
    WorkspaceSnapshot copy = s;
    copy.savedAt = QDateTime();
    return QJsonDocument(copy.toJson()).toJson(QJsonDocument::Compact);
}

}  // namespace

// =============================================================================
//  Setup
// =============================================================================

void MainWindow::setupSessionFeatures(QMenu *fileMenu, QMenu *editMenu, QAction *editFirst,
                                      QMenu *viewMenu, QMenu *themeMenu)
{
    // ---- undo ---------------------------------------------------------------
    m_undo = new UndoLog(this);
    m_actUndo = m_undo->createAction(this);
    editMenu->insertAction(editFirst, m_actUndo);
    editMenu->insertSeparator(editFirst);

    // The offer in the status bar: the click that just threw something away
    // is answered where the eye already is, for a few seconds.
    m_undoButton = new QToolButton(this);
    m_undoButton->setAutoRaise(true);
    m_undoButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_undoButton->hide();
    statusBar()->addWidget(m_undoButton);
    connect(m_undoButton, &QToolButton::clicked, m_actUndo, &QAction::trigger);
    m_undoOfferTimer = new QTimer(this);
    m_undoOfferTimer->setSingleShot(true);
    m_undoOfferTimer->setInterval(kUndoOfferMs);
    connect(m_undoOfferTimer, &QTimer::timeout, m_undoButton, &QWidget::hide);
    connect(m_undo, &UndoLog::pushed, this, &MainWindow::showUndoOffer);
    connect(m_undo, &UndoLog::undone, this, [this](const QString &label, bool restored) {
        m_undoButton->hide();
        if (restored) {
            notify(NoteLevel::Info, tr("Undone: %1").arg(label));
        } else {
            notify(NoteLevel::Warning,
                   tr("Could not undo %1: what it would restore no longer exists").arg(label));
        }
    });
    if (m_statusPins)  { m_statusPins->setUndoLog(m_undo); }
    if (m_watchPanel)  { m_watchPanel->setUndoLog(m_undo); }

    // ---- colour-blind-safe status colours --------------------------------------
    themeMenu->addSeparator();
    m_actColorBlind = themeMenu->addAction(tr("Colour-&blind-safe status colours"));
    m_actColorBlind->setCheckable(true);
    m_actColorBlind->setChecked(UiColor::colorBlindSafe());
    m_actColorBlind->setToolTip(tr("OK, warning and error in blue, amber and raspberry instead of green, "
                                   "orange and red: told apart with red-green (and blue-yellow) colour blindness.\n"
                                   "Works with every theme."));
    connect(m_actColorBlind, &QAction::toggled, this, &MainWindow::onToggleColorBlindSafe);

    // ---- layouts ----------------------------------------------------------------
    m_layoutsMenu = new QMenu(tr("&Layouts"), this);
    viewMenu->insertMenu(themeMenu->menuAction(), m_layoutsMenu);
    rebuildLayoutsMenu();
    // Also on opening: the file may have changed under it (an import, a
    // second DLConsole on the same folder). Built eagerly as well, so the
    // Ctrl+Alt+n shortcuts work before the menu has ever been opened.
    connect(m_layoutsMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildLayoutsMenu);

    // ---- settings export / import ------------------------------------------------
    QAction *quitAction = fileMenu->actions().isEmpty() ? nullptr : fileMenu->actions().last();
    QAction *before = quitAction;
    // The separator above Quit, so the new pair sits in its own group.
    const QList<QAction *> fileActions = fileMenu->actions();
    if (fileActions.size() >= 2 && fileActions.at(fileActions.size() - 2)->isSeparator()) {
        before = fileActions.at(fileActions.size() - 2);
    }
    QAction *actExportSettings = new QAction(tr("E&xport settings…"), this);
    actExportSettings->setToolTip(tr("Theme, tab tags, pins, layouts, flasher profiles and loco "
                                     "configurations, in one file for another PC"));
    connect(actExportSettings, &QAction::triggered, this, &MainWindow::onActionExportSettings);
    QAction *actImportSettings = new QAction(tr("&Import settings…"), this);
    connect(actImportSettings, &QAction::triggered, this, &MainWindow::onActionImportSettings);
    fileMenu->insertSeparator(before);
    fileMenu->insertAction(before, actExportSettings);
    fileMenu->insertAction(before, actImportSettings);

    // ---- clock-skew alarm (session 81) -------------------------------------------
    // The status bar goes red on its own; this says so once, in words, per
    // episode — and again when it is over, with how long it lasted.
    m_skewAlarm = new ClockSkewAlarm(3000, this);
    connect(m_skewAlarm, &ClockSkewAlarm::raised, this, [this](const ClockSkewAlarm::Episode &e) {
        notify(NoteLevel::Warning,
               tr("Loco and station clocks are outside the accept window: %1").arg(ClockSkewAlarm::gapText(e.worstGap)),
               tr("Each end discards a packet more than 4 s old or 2 s or more ahead of its own clock, so while "
                  "this lasts the loco ignores the station's packets and the station the loco's. The clocks come "
                  "from FRAME_NUM (seconds since midnight + 1)."));
    });
    connect(m_skewAlarm, &ClockSkewAlarm::cleared, this, [this](const ClockSkewAlarm::Episode &e) {
        notify(NoteLevel::Info, tr("Loco and station clocks back inside the accept window after %1 (worst: %2)")
                                    .arg(ClockSkewAlarm::durationText(e.durationMs(e.endMs)),
                                         ClockSkewAlarm::gapText(e.worstGap)));
    });

    // ---- crash recovery -------------------------------------------------------
    m_recoveryTimer = new QTimer(this);
    m_recoveryTimer->setInterval(kRecoveryIntervalMs);
    connect(m_recoveryTimer, &QTimer::timeout, this, [this]() { writeRecoverySnapshot(); });
    m_recoveryTimer->start();
    if (!m_recoveredSummary.isEmpty()) {
        QTimer::singleShot(0, this, [this]() {
            notify(NoteLevel::Warning,
                   tr("DLConsole did not close cleanly last time. Restored the workspace it had: %1.")
                       .arg(m_recoveredSummary),
                   tr("The disk log is written as traffic arrives, so it is complete up to the moment "
                      "the program stopped. The tabs and pop-outs come from the snapshot taken every "
                      "few seconds while it ran."));
        });
    }
}

// =============================================================================
//  The snapshot core
// =============================================================================

WorkspaceSnapshot MainWindow::captureWorkspace() const
{
    WorkspaceSnapshot s;
    s.mainGeometry = saveGeometry();
    s.mainState    = saveState(Settings::kLayoutVersion);

    // Tab-bar order first, then the hidden ones — as saveWorkspace() always has.
    QStringList seen;
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        QWidget *w = ui->tabWidget->widget(i);
        for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
            if (it->container != w) { continue; }
            s.tabs.append({ it->tabKey, it->friendlyName, true });
            seen << it->tabKey;
            break;
        }
    }
    QStringList hidden;
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        if (!seen.contains(it->tabKey)) hidden << it->tabKey;
    }
    hidden.sort();   // hash order would make every snapshot look changed
    for (const QString &key : hidden) {
        s.tabs.append({ key, m_tabs.value(key).friendlyName, false });
    }

    const int cur = ui->tabWidget->currentIndex();
    if (cur >= 0) {
        QWidget *w = ui->tabWidget->widget(cur);
        for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
            if (it->container == w) { s.activeTab = it->tabKey; break; }
        }
    }

    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    for (auto it = m_popouts.constBegin(); it != m_popouts.constEnd(); ++it) {
        if (!it.value()) continue;
        s.popouts << it.key();
        if (!it.value()->isMinimized()) {
            s.popoutGeometry.insert(it.key(), it.value()->saveGeometry());
        }
    }
    for (const QString &key : m_pendingPopouts) {
        if (s.popouts.contains(key)) continue;
        s.popouts << key;
        const QByteArray g = settings.value(
            QStringLiteral("windows/%1_geometry").arg(TabPopoutWindow::geometryKey(key))).toByteArray();
        if (!g.isEmpty()) s.popoutGeometry.insert(key, g);
    }
    s.popouts.sort();
    s.savedAt = QDateTime::currentDateTimeUtc();
    return s;
}

void MainWindow::applyWorkspace(const WorkspaceSnapshot &s)
{
    if (!m_dispatcher) { return; }

    if (!s.mainGeometry.isEmpty()) restoreGeometry(s.mainGeometry);
    if (!s.mainState.isEmpty())    restoreState(s.mainState, Settings::kLayoutVersion);
    if (m_failDock && m_failRefs.isEmpty()) m_failDock->hide();

    // Tabs: take every one off the bar, then put back the layout's in its
    // order. showTab() appends, so the order comes out right without
    // moving anything. Sources the layout does not name stay hidden, as a
    // closed tab always has: still watched, and back if they speak.
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        hideTab(*it);
    }
    for (const WorkspaceTab &t : s.tabs) {
        auto it = m_tabs.find(t.key);
        if (it == m_tabs.end()) {
            buildOrShowTab(t.key, t.name.isEmpty() ? m_dispatcher->friendlyNameFor(t.key) : t.name);
            it = m_tabs.find(t.key);
            if (it == m_tabs.end()) continue;
        } else if (t.visible) {
            showTab(*it);
        }
        if (!t.visible) hideTab(*it);
    }
    if (!s.activeTab.isEmpty()) {
        auto it = m_tabs.find(s.activeTab);
        if (it != m_tabs.end() && it->container) {
            const int idx = ui->tabWidget->indexOf(it->container);
            if (idx >= 0) ui->tabWidget->setCurrentIndex(idx);
        }
    }

    // Pop-outs: close the ones the layout does not have, place the ones it
    // does. A tab whose model does not exist yet waits, as at startup.
    const QList<QString> open = m_popouts.keys();
    for (const QString &key : open) {
        TabPopoutWindow *w = m_popouts.value(key);
        if (w && !s.popouts.contains(key)) w->close();
    }
    m_pendingPopouts.clear();
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    for (const QString &key : s.popouts) {
        const QByteArray g = s.popoutGeometry.value(key);
        if (!g.isEmpty()) {
            settings.setValue(QStringLiteral("windows/%1_geometry").arg(TabPopoutWindow::geometryKey(key)), g);
        }
        if (TabPopoutWindow *w = m_popouts.value(key)) {
            if (!g.isEmpty()) w->restoreGeometry(g);
            w->showNormal();
            continue;
        }
        if (m_tabs.contains(key)) {
            settings.sync();
            popOutTab(key);
        } else {
            m_pendingPopouts << key;
        }
    }
    settings.sync();
    savePopoutKeys();
    saveWorkspace();
    scheduleLayoutSave();
    rebuildWindowMenu();
}

void MainWindow::writeRecoverySnapshot(bool force)
{
    const WorkspaceSnapshot s = captureWorkspace();
    const QByteArray now = comparable(s);
    if (!force && now == m_lastRecoveryJson) {
        return;
    }
    if (SessionRecovery::write(SessionRecovery::defaultPath(), s)) {
        m_lastRecoveryJson = now;
    }
}

// =============================================================================
//  Layouts
// =============================================================================

void MainWindow::rebuildLayoutsMenu()
{
    if (!m_layoutsMenu) return;
    // clear() deletes the actions but not a submenu made by addMenu(), which
    // would pile up one "Delete layout" menu per opening.
    qDeleteAll(m_layoutsMenu->findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly));
    m_layoutsMenu->clear();
    QAction *save = m_layoutsMenu->addAction(tr("&Save current layout as…"));
    save->setToolTip(tr("Tabs in their order, hidden tabs, the tab in front, pop-outs and where they are, "
                        "and the panels"));
    connect(save, &QAction::triggered, this, &MainWindow::onActionSaveLayoutAs);

    WindowLayoutStore store;
    store.load();
    const QStringList names = store.names();
    if (names.isEmpty()) {
        QAction *none = m_layoutsMenu->addAction(tr("(no saved layouts)"));
        none->setEnabled(false);
        return;
    }
    m_layoutsMenu->addSeparator();
    for (int i = 0; i < names.size(); ++i) {
        const QString name = names.at(i);
        QAction *a = m_layoutsMenu->addAction(name);
        a->setToolTip(store.snapshot(name).summary());
        // Ctrl+Alt+1…9 for the first nine: switching layouts is something
        // done at the bench with both hands busy.
        if (i < 9) {
            a->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+%1").arg(i + 1)));
        }
        connect(a, &QAction::triggered, this, [this, name]() { switchToLayout(name); });
    }
    m_layoutsMenu->addSeparator();
    QMenu *del = m_layoutsMenu->addMenu(tr("&Delete layout"));
    for (const QString &name : names) {
        QAction *a = del->addAction(name);
        connect(a, &QAction::triggered, this, [this, name]() { deleteLayout(name); });
    }
}

void MainWindow::onActionSaveLayoutAs()
{
    WindowLayoutStore store;
    store.load();
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Save layout"),
        tr("Name for the current layout (%1):").arg(captureWorkspace().summary()),
        QLineEdit::Normal, m_lastLayout, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (store.contains(name)
        && QMessageBox::question(this, tr("Save layout"),
                                 tr("Replace the layout \"%1\"?").arg(name)) != QMessageBox::Yes) {
        return;
    }
    saveCurrentLayoutAs(name);
}

bool MainWindow::saveCurrentLayoutAs(const QString &name)
{
    WindowLayoutStore store;
    if (!store.load()) {
        notify(NoteLevel::Error, tr("Layouts not saved: %1 cannot be read (%2)")
                                     .arg(store.filePath(), store.lastError()));
        return false;
    }
    const WorkspaceSnapshot s = captureWorkspace();
    store.put(name, s);
    if (!store.save()) {
        notify(NoteLevel::Error, tr("Layout not saved: %1").arg(store.lastError()));
        return false;
    }
    m_lastLayout = name.trimmed().left(WindowLayoutStore::kMaxNameLength);
    rebuildLayoutsMenu();
    notify(NoteLevel::Info, tr("Layout \"%1\" saved: %2").arg(m_lastLayout, s.summary()));
    return true;
}

bool MainWindow::switchToLayout(const QString &name)
{
    WindowLayoutStore store;
    if (!store.load() || !store.contains(name)) {
        notify(NoteLevel::Warning, tr("No layout called \"%1\"").arg(name));
        return false;
    }
    const WorkspaceSnapshot target = store.snapshot(name);
    const WorkspaceSnapshot before = captureWorkspace();
    applyWorkspace(target);
    m_lastLayout = name;
    QPointer<MainWindow> self(this);
    m_undo->push(tr("Switch to layout \"%1\"").arg(name), [self, before]() {
        if (!self) return false;
        self->applyWorkspace(before);
        return true;
    });
    notify(NoteLevel::Info, tr("Layout \"%1\": %2").arg(name, target.summary()));
    return true;
}

bool MainWindow::deleteLayout(const QString &name)
{
    WindowLayoutStore store;
    if (!store.load() || !store.contains(name)) return false;
    const int at = store.indexOf(name);
    const WorkspaceSnapshot gone = store.snapshot(name);
    store.remove(name);
    if (!store.save()) {
        notify(NoteLevel::Error, tr("Layout not deleted: %1").arg(store.lastError()));
        return false;
    }
    rebuildLayoutsMenu();
    QPointer<MainWindow> self(this);
    m_undo->push(tr("Delete layout \"%1\"").arg(name), [self, name, at, gone]() {
        if (!self) return false;
        WindowLayoutStore back;
        if (!back.load() || back.contains(name)) return false;
        back.insert(at, name, gone);
        if (!back.save()) return false;
        self->rebuildLayoutsMenu();
        return true;
    });
    notify(NoteLevel::Info, tr("Layout \"%1\" deleted").arg(name));
    return true;
}

// =============================================================================
//  Undo offer
// =============================================================================

void MainWindow::showUndoOffer(const QString &label)
{
    if (!m_undoButton) return;
    QString text = label;
    if (text.size() > 40) text = text.left(38) + QStringLiteral("…");
    m_undoButton->setText(tr("↶ Undo %1").arg(text));
    m_undoButton->setToolTip(tr("Undo %1 (Ctrl+Z, or Edit ▸ Undo)").arg(label));
    m_undoButton->show();
    m_undoOfferTimer->start();
}

// =============================================================================
//  Colour-blind-safe status colours
// =============================================================================

void MainWindow::onToggleColorBlindSafe(bool on)
{
    if (UiColor::colorBlindSafe() == on) return;
    Settings::setColorBlindSafe(on);
    UiColor::setColorBlindSafe(on);
    // Re-applying the theme re-sets the palette and notifies every surface
    // that coloured itself (UiColor::onThemeChange); the log tables read
    // their colours at paint time.
    ThemeUtil::apply(m_theme);
    UiStyle::apply();
    refreshTabHealth();
    applyAllTabTags();
    notify(NoteLevel::Info, on ? tr("Status colours: colour-blind-safe (blue / amber / raspberry)")
                               : tr("Status colours: the theme's own"));
}

// =============================================================================
//  Settings export / import
// =============================================================================

void MainWindow::onActionExportSettings()
{
    using namespace SettingsBundle;
    const Bundle everything = capture(allSections());
    const QList<Section> available = everything.present();
    QList<Section> chosen;
    if (!chooseSections(this, tr("Export settings"),
                        tr("Put these into one file, to import on another PC or give to a colleague. "
                           "Network, disk-log and key settings stay with this machine."),
                        everything, available, &chosen)) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export settings"), QStringLiteral("dlconsole-settings.json"),
        tr("DLConsole settings (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    Bundle out = everything;
    for (Section s : available) {
        if (!chosen.contains(s)) out.sections.remove(id(s));
    }
    QString error;
    if (!writeFile(path, out, &error)) {
        notify(NoteLevel::Error, tr("Settings not exported: %1").arg(error), path);
        return;
    }
    QStringList names;
    for (Section s : chosen) names << label(s);
    notify(NoteLevel::Info, tr("Settings exported: %1").arg(names.join(QStringLiteral(", "))), path);
}

void MainWindow::onActionImportSettings()
{
    using namespace SettingsBundle;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import settings"), QString(), tr("DLConsole settings (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    Bundle bundle;
    QString error;
    if (!readFile(path, &bundle, &error)) {
        QMessageBox::warning(this, tr("Import settings"), error);
        return;
    }
    if (bundle.present().isEmpty()) {
        QMessageBox::information(this, tr("Import settings"), tr("That file has no settings in it."));
        return;
    }
    QString from = bundle.exportedBy;
    if (bundle.exported.isValid()) {
        from += tr(", exported %1").arg(bundle.exported.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    }
    QList<Section> chosen;
    if (!chooseSections(this, tr("Import settings"),
                        tr("From %1.\n\nEach ticked item REPLACES what this console has now (it is not merged). "
                           "Edit ▸ Undo puts it back.").arg(from.isEmpty() ? QFileInfo(path).fileName() : from),
                        bundle, bundle.present(), &chosen)) {
        return;
    }
    QStringList ids;
    for (Section s : chosen) ids << id(s);
    if (!importSettingsFrom(path, ids, &error)) {
        QMessageBox::warning(this, tr("Import settings"), error);
    }
}

bool MainWindow::importSettingsFrom(const QString &path, const QStringList &sectionIds, QString *error)
{
    using namespace SettingsBundle;
    Bundle bundle;
    if (!readFile(path, &bundle, error)) return false;
    QList<Section> sections;
    for (const QString &sid : sectionIds) {
        Section s;
        if (fromId(sid, &s)) sections << s;
    }
    // A window holding its own copy would write it back over the import
    // the next time it saves. Refused, not raced.
    if (sections.contains(Section::FlasherProfiles) && m_flasher) {
        if (error) *error = tr("Close the Firmware Flasher first: it holds its own copy of the profiles "
                               "and would save it over the imported ones.");
        return false;
    }
    if (sections.contains(Section::LocoConfigs) && m_locoConfig) {
        if (error) *error = tr("Close the Loco Configuration window first: it holds its own copy of the "
                               "configurations and would save it over the imported ones.");
        return false;
    }
    const Bundle backup = capture(sections);
    if (!apply(bundle, sections, QString(), QString(), error)) return false;
    afterSettingsImported(sectionIds);

    QStringList names;
    for (Section s : sections) names << label(s);
    QPointer<MainWindow> self(this);
    m_undo->push(tr("Import settings"), [self, backup, sections, sectionIds]() {
        if (!self) return false;
        QString why;
        if (!SettingsBundle::restore(backup, sections, QString(), QString(), &why)) return false;
        self->afterSettingsImported(sectionIds);
        return true;
    });
    notify(NoteLevel::Info, tr("Settings imported: %1. Ctrl+Z puts back what was here.")
                                .arg(names.join(QStringLiteral(", "))), path);
    return true;
}

void MainWindow::afterSettingsImported(const QStringList &ids)
{
    using namespace SettingsBundle;
    if (ids.contains(id(Section::Appearance))) {
        const bool cb = Settings::colorBlindSafe();
        UiColor::setColorBlindSafe(cb);
        if (m_actColorBlind) {
            const QSignalBlocker block(m_actColorBlind);
            m_actColorBlind->setChecked(cb);
        }
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        if (TextZoom::isInitialised()) {
            TextZoom::setPercent(settings.value(QStringLiteral("ui/textZoom"), 100).toInt());
        }
        const Theme t = ThemeUtil::fromString(Settings::theme());
        if (t == m_theme) {
            // Same theme: re-apply anyway, for the colour-blind switch.
            ThemeUtil::apply(t);
            UiStyle::apply();
            refreshTabHealth();
            applyAllTabTags();
        } else {
            onThemeChanged(Settings::theme());
        }
    }
    if (ids.contains(id(Section::Tags))) {
        applyAllTabTags();
        for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
            emit TabTags::instance()->changed(it.key());   // pop-outs redraw their dot too
        }
    }
    if (ids.contains(id(Section::Pins))) {
        if (m_statusPins) m_statusPins->reload();
        if (m_pinPanel)   m_pinPanel->restore();
        BigNumberPanel::reloadAll();
    }
    if (ids.contains(id(Section::Layouts))) {
        rebuildLayoutsMenu();
    }
}
