// =============================================================================
//  mainwindow_recent.cpp -- File ▸ Open Recent and drag-and-drop (session 287)
//  ---------------------------------------------------------------------------
//  Both go through openFiles(), which sends each file where its kind
//  belongs (recentfiles.h): recorded sessions to one session viewer,
//  captures to one replay window, tag routes to the Tag Builder.
// =============================================================================

#include "mainwindow.h"

#include "recentfiles.h"
#include "replaywindow.h"
#include "tagbuilderwindow.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMenu>
#include <QMimeData>
#include <QUrl>

namespace {
QStringList localFiles(const QMimeData *mime)
{
    QStringList out;
    if (!mime || !mime->hasUrls()) return out;
    for (const QUrl &u : mime->urls())
        if (u.isLocalFile()) out << u.toLocalFile();
    return out;
}
}  // namespace

QStringList MainWindow::openFiles(const QStringList &paths)
{
    using RecentFiles::Kind;
    QStringList sessions, captures, routes, refused;
    for (const QString &p : paths) {
        const Kind k = RecentFiles::kindOf(p);
        if (k == Kind::Unknown || !QFileInfo(p).isFile()) {
            refused << p;
            continue;
        }
        (k == Kind::Session ? sessions : k == Kind::Capture ? captures : routes) << p;
    }

    if (!sessions.isEmpty()) openSessionFiles(sessions);
    if (!captures.isEmpty()) {
        // As the Loco Console's Open replay does: one window, the captures
        // merged by time.
        auto *w = new ReplayWindow(captures, nullptr);
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
        w->raise();
        RecentFiles::addAll(captures);
        notify(NoteLevel::Info, tr("Replaying %1 capture file(s)").arg(captures.size()));
    }
    for (const QString &r : routes) {
        auto *w = new TagBuilderWindow(m_dispatcher, this);
        w->setRunSource(currentTabKey());
        connect(w, &TagBuilderWindow::jumpRequested, this,
                [this](const QString &tabKey, qint64 ms) { jumpToEntry(tabKey, ms); });
        w->loadFile(r);   // adds it to Open Recent when it opens
        w->show();
        w->raise();
    }

    if (!refused.isEmpty()) {
        QStringList names;
        for (const QString &p : refused) names << QFileInfo(p).fileName();
        notify(NoteLevel::Warning,
               tr("Not opened: %1. Open Recent and dropping take recorded sessions (.dlr), "
                  "captures (.cap) and tag routes (.tagroute.xml)").arg(names.join(QStringLiteral(", "))),
               refused.join(QLatin1Char('\n')));
    }
    return refused;
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    const QStringList files = RecentFiles::list();
    if (files.isEmpty()) {
        m_recentMenu->addAction(tr("(none yet)"))->setEnabled(false);
        return;
    }
    for (int i = 0; i < files.size(); ++i) {
        const QString path = files.at(i);
        const QFileInfo fi(path);
        const bool there = fi.isFile();
        // &1..&9, then &0: the first ten reachable by key.
        const QString key = i < 9 ? QString::number(i + 1) : QStringLiteral("0");
        QString text = QStringLiteral("&%1  %2").arg(key, fi.fileName());
        if (!there) text += tr("  (missing)");
        QAction *a = m_recentMenu->addAction(text);
        a->setEnabled(there);
        a->setToolTip(QStringLiteral("%1 — %2").arg(RecentFiles::kindName(RecentFiles::kindOf(path)),
                                                     QDir::toNativeSeparators(path)));
        a->setStatusTip(QDir::toNativeSeparators(path));
        a->setData(path);
        connect(a, &QAction::triggered, this, [this, path]() { openFiles({ path }); });
    }
    m_recentMenu->setToolTipsVisible(true);
    m_recentMenu->addSeparator();
    QAction *clear = m_recentMenu->addAction(tr("Clear list"));
    connect(clear, &QAction::triggered, this, [this]() {
        RecentFiles::clear();
        rebuildRecentMenu();
    });
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept when any dropped file is a kind openFiles() takes, so the
    // cursor says no to a drop that would open nothing.
    for (const QString &p : localFiles(event->mimeData())) {
        if (RecentFiles::kindOf(p) != RecentFiles::Kind::Unknown) {
            event->acceptProposedAction();
            return;
        }
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QStringList files = localFiles(event->mimeData());
    if (files.isEmpty()) {
        QMainWindow::dropEvent(event);
        return;
    }
    event->acceptProposedAction();
    openFiles(files);
}
