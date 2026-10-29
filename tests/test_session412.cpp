#include "testutil.h"

#include "decodeworkbench.h"
#include "fieldcatalog.h"
#include "logentry.h"
#include "mainwindow.h"
#include "minimizeddock.h"
#include "packetmakerdialog.h"
#include "rawbytespanel.h"

#include <QApplication>
#include <QFile>
#include <QMainWindow>

// =============================================================================
//  Session 412 — a packet opened in the Decode Workbench or the Packet Maker
//  gets a window of its own, every time. Abhinav: one workbench opened,
//  minimised, a second packet opened: the workbench stayed minimised and
//  Maximize did not bring it back. Two causes, both fixed: the row reused
//  the last window, and a window shown again while the minimised-window dock
//  held it came back still minimised. Real rows from replay/.
// =============================================================================

namespace {

void settle412()
{
    for (int i = 0; i < 5; ++i) {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

template <typename T>
QList<T *> shown412(QWidget *owner)
{
    QList<T *> out;
    for (T *w : owner->findChildren<T *>())
        if (w->isWindow() && w->isVisible()) out << w;
    return out;
}

}  // namespace

TEST_SUITE(session412)
{
    // ---- the dock: a window shown again while it held it ---------------------------------------
    {
        QMainWindow main;
        auto *dock = new MinimizedDock(&main);
        main.show();
        auto *tool = new QMainWindow(&main);
        tool->setWindowFlag(Qt::Window);
        tool->setWindowTitle(QStringLiteral("Tool"));
        tool->show();
        settle412();
        tool->setWindowState(Qt::WindowMinimized);
        settle412();
        CHECK(dock->chipCount() == 1 && !tool->isVisible(), "minimised: a chip, the window out of the way");
        tool->show();   // what reopening it from its menu does
        settle412();
        CHECK(dock->chipCount() == 0 && tool->isVisible() && !(tool->windowState() & Qt::WindowMinimized),
              "shown again another way: the chip goes, and the window is no longer minimised");
    }

    // ---- real rows ------------------------------------------------------------------------------
    QStringList lines;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (f.open(QIODevice::ReadOnly))
        while (!f.atEnd() && lines.size() < 3) {
            const QString l = QString::fromUtf8(f.readLine()).trimmed();
            if (l.startsWith(QLatin1Char('@'))) lines << l;
        }
    CHECK(lines.size() == 3, "three real rows from replay/");
    if (lines.size() < 3) return;
    QVector<LogEntryPtr> rows;
    for (int i = 0; i < 3; ++i) {
        LogEntryPtr e(new LogEntry);
        e->text = lines.at(i);
        e->epochMs = 1782551000000LL + i * 1000;
        rows << e;
    }

    // The main window loads the field catalogue: left as it was found (as 287 does).
    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    struct Restore { bool was; ~Restore() { if (!was) FieldCatalog::instance().clear(); } } restore{ catalogWasLoaded };
    MainWindow w;
    w.show();
    RawBytesPanel *panel = w.findChild<RawBytesPanel *>();
    CHECK(panel, "the main window's raw bytes panel, which asks for the windows");
    if (!panel) return;

    // ---- the Decode Workbench -------------------------------------------------------------------
    emit panel->openInDecodeWorkbenchRequested(rows.at(0));
    settle412();
    QList<DecodeWorkbench *> wbs = shown412<DecodeWorkbench>(&w);
    CHECK(wbs.size() == 1 && wbs.first()->windowTitle().startsWith(QStringLiteral("Decode Workbench · @")),
          qPrintable(QStringLiteral("a workbench, its title naming the row (%1)").arg(wbs.isEmpty() ? QString() : wbs.first()->windowTitle())));
    DecodeWorkbench *first = wbs.value(0);
    if (first) first->setWindowState(Qt::WindowMinimized);
    settle412();
    emit panel->openInDecodeWorkbenchRequested(rows.at(1));
    settle412();
    wbs = shown412<DecodeWorkbench>(&w);
    CHECK(wbs.size() == 1 && wbs.first() != first && first && !first->isVisible(),
          "the first minimised, a second row: a second window, shown; the first stays minimised");
    MinimizedDock *dock = w.findChild<MinimizedDock *>();
    const int at = dock ? dock->chipTitles().indexOf(first ? first->windowTitle() : QString()) : -1;
    CHECK(at >= 0, "the first one's chip, by its title");
    if (dock && at >= 0) dock->restoreChip(at);
    settle412();
    CHECK(shown412<DecodeWorkbench>(&w).size() == 2 && first && !(first->windowState() & Qt::WindowMinimized),
          "its chip brings it back: both open");
    wbs = shown412<DecodeWorkbench>(&w);
    for (DecodeWorkbench *wb : wbs) wb->setWindowState(Qt::WindowMinimized);
    settle412();
    CHECK(shown412<DecodeWorkbench>(&w).isEmpty() && dock && dock->chipCount() >= 2, "both minimised: a chip each");
    if (dock) dock->restoreChip(dock->chipCount() - 1);
    settle412();
    CHECK(shown412<DecodeWorkbench>(&w).size() == 1, "and each comes back on its own");

    // ---- the Packet Maker -------------------------------------------------------------------------
    emit panel->openInPacketMakerRequested(rows.at(0));
    emit panel->openInPacketMakerRequested(rows.at(2));
    settle412();
    const QList<PacketMakerDialog *> pms = shown412<PacketMakerDialog>(&w);
    CHECK(pms.size() == 2 && pms.at(0)->windowTitle() != pms.at(1)->windowTitle()
              && pms.at(0)->windowTitle().startsWith(QStringLiteral("Packet Maker · ")),
          "two rows in the Packet Maker: two windows, each titled by its row");
    for (QWidget *top : QApplication::topLevelWidgets())
        if (top->inherits("DecodeWorkbench") || top->inherits("PacketMakerDialog")) top->close();
    settle412();
}
