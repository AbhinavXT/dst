// =============================================================================
//  mainwindow_start.cpp -- the start page (session 289)
//  ---------------------------------------------------------------------------
//  Over the tab area while no tab is on show: what the console is doing
//  (the listening state, unchanged from before), a button for each way in
//  (the menu actions themselves), and the recent files (recentfiles.h), so
//  a fresh start is one click from the last log rather than a dialog.
// =============================================================================

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "recentfiles.h"
#include "uicolors.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

void MainWindow::buildStartPage()
{
    m_startPage = new QWidget(ui->tabWidget);
    m_startPage->setObjectName(QStringLiteral("startPage"));

    auto *outer = new QVBoxLayout(m_startPage);
    outer->addStretch(1);
    auto *column = new QWidget(m_startPage);
    column->setMaximumWidth(640);
    auto *col = new QVBoxLayout(column);
    col->setContentsMargins(24, 0, 24, 0);
    col->setSpacing(12);

    m_emptyState->setParent(column);
    m_emptyState->show();
    col->addWidget(m_emptyState);

    m_startButtons = new QWidget(column);
    m_startButtons->setObjectName(QStringLiteral("startButtons"));
    col->addWidget(m_startButtons, 0, Qt::AlignHCenter);

    m_startRecent = new QWidget(column);
    m_startRecent->setObjectName(QStringLiteral("startRecent"));
    col->addWidget(m_startRecent);

    auto *hint = new QLabel(tr("Drop a .dlr, .cap or .tagroute.xml file on this window to open it."), column);
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    hint->setStyleSheet(UiColor::mutedStyle());
    col->addWidget(hint);

    auto *centre = new QHBoxLayout;
    centre->addStretch(1);
    centre->addWidget(column, 4);
    centre->addStretch(1);
    outer->addLayout(centre);
    outer->addStretch(2);
    m_startPage->hide();
}

void MainWindow::rebuildStartPage()
{
    if (!m_startButtons || !m_startRecent) return;

    // Buttons: one per way in, made once (the actions do not change).
    if (!m_startButtons->layout()) {
        // Three to a row: five in one were cut short in the 640 px column.
        auto *grid = new QGridLayout(m_startButtons);
        grid->setContentsMargins(0, 0, 0, 0);
        int i = 0;
        for (QAction *a : m_startActions) {
            auto *b = new QPushButton(a->text().remove(QLatin1Char('&')), m_startButtons);
            QString tip = a->toolTip();
            if (!a->shortcut().isEmpty())
                tip += QStringLiteral("  (%1)").arg(a->shortcut().toString(QKeySequence::NativeText));
            b->setToolTip(tip);
            b->setEnabled(a->isEnabled());
            b->setMinimumWidth(b->sizeHint().width());
            connect(b, &QPushButton::clicked, a, &QAction::trigger);
            grid->addWidget(b, i / 3, i % 3);
            ++i;
        }
    }

    // Recent files: rebuilt each time the page shows, so a file deleted
    // since reads as missing.
    // deleteLater, not delete: this runs from a recent row's own click.
    delete m_startRecent->layout();
    for (QWidget *w : m_startRecent->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        w->hide();
        w->deleteLater();
    }
    const QStringList files = RecentFiles::list();
    if (files.isEmpty()) return;
    auto *list = new QVBoxLayout(m_startRecent);
    list->setContentsMargins(0, 8, 0, 0);
    list->setSpacing(2);
    auto *head = new QLabel(tr("<b>Recent files</b>"), m_startRecent);
    list->addWidget(head);
    for (const QString &path : files) {
        const QFileInfo fi(path);
        const bool there = fi.isFile();
        const QString kind = RecentFiles::kindName(RecentFiles::kindOf(path));
        auto *b = new QPushButton(m_startRecent);
        b->setFlat(true);
        b->setStyleSheet(QStringLiteral("text-align: left; padding: 3px 6px;"));
        b->setText(there ? QStringLiteral("%1   — %2").arg(fi.fileName(), kind)
                         : tr("%1   — missing").arg(fi.fileName()));
        b->setToolTip(QDir::toNativeSeparators(path));
        b->setEnabled(there);
        b->setProperty("recentPath", path);
        connect(b, &QPushButton::clicked, this, [this, path]() { openFiles({ path }); });
        list->addWidget(b);
    }
}

void MainWindow::onActionReplayCapture()
{
    // Where the Loco Console records them (replay/ beside the working
    // folder), when there is one.
    const QString start = QDir(QStringLiteral("replay")).exists() ? QDir(QStringLiteral("replay")).absolutePath()
                                                                 : QString();
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Replay capture(s) — select several to merge them by time"), start,
        tr("Capture files (*.cap);;All files (*)"));
    if (!paths.isEmpty()) openFiles(paths);
}
