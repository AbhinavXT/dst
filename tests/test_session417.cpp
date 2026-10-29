#include "testutil.h"

#include "fieldcatalog.h"
#include "locosimwindow.h"
#include "mainwindow.h"
#include "rfidtag.h"
#include "rfidworkspacewindow.h"
#include "settings.h"
#include "stationlayout.h"
#include "stationlayoutwindow.h"
#include "tageditdialog.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTimer>

// =============================================================================
//  Session 417 — Tools ▸ RFID Workspace…: the Station Layout, a tag editor
//  and the Loco Simulator in one window, in step. A tag picked in any of
//  them shows in the others; Apply in the editor puts it in the layout and
//  a line the simulator runs follows. The simulator is the main window's,
//  held while the workspace is open and handed back after. The built-in
//  station.xlsx (the Python tool's), its real tags.
// =============================================================================

namespace {

void settle417()
{
    for (int i = 0; i < 6; ++i) {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

QString simCurrentTag(LocoSimWindow *sim)
{
    auto *t = sim ? sim->findChild<QTableWidget *>(QStringLiteral("locoSimTags")) : nullptr;
    const int r = t ? t->currentRow() : -1;
    return r >= 0 && t->item(r, 1) ? t->item(r, 1)->text() : QString();
}

QByteArray layoutTag(StationLayoutWindow *w, const QString &name)
{
    for (const StationLayout::Tag &t : w->station().tags)
        if (t.name == name) return RfidTag::fromPages(t.pageX, t.pageY);
    return QByteArray();
}

QByteArray simTag(LocoSimWindow *sim, const QString &name)
{
    for (const auto &p : sim->routeTags())
        if (p.first == name) return p.second;
    return QByteArray();
}

}  // namespace

TEST_SUITE(session417)
{
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
    const QString lastLayout = Settings::stationLayoutLastFile();
    Settings::setStationLayoutLastFile(QString());
    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    [&]() {
        MainWindow main;
        QAction *open = main.findChild<QAction *>(QStringLiteral("actRfidWorkspace"));
        CHECK(open, "Tools ▸ RFID Workspace…");
        if (!open) return;
        open->trigger();
        settle417();
        RfidWorkspaceWindow *ws = main.findChild<RfidWorkspaceWindow *>();
        CHECK(ws && ws->isVisible() && ws->layoutWindow()->station().tags.size() == 211,
              "the workspace, the built-in layout in it (211 tags)");
        if (!ws) return;
        StationLayoutWindow *layout = ws->layoutWindow();
        LocoSimWindow *sim = ws->simulator();
        CHECK(sim && !sim->isWindow() && sim->window() == ws && main.findChild<LocoSimWindow *>() == sim,
              "the main window's Loco Simulator, inside it (not a window of its own)");
        if (!sim) return;
        sim->setSendSink([](const QByteArray &) {});
        sim->setTagSink([](int, const QByteArray &) {});

        // Tools ▸ Simulators ▸ Loco Simulator… while it is held: the workspace, no second window.
        if (QAction *simAct = main.findChild<QAction *>(QStringLiteral("actLocoSim"))) simAct->trigger();
        settle417();
        CHECK(!sim->isWindow() && main.findChildren<LocoSimWindow *>().size() == 1, "Loco Simulator… meanwhile: the same one, still inside");

        // ---- a line of the layout, run in the simulator here ----------------------------------------
        QString line;
        int most = 0;
        for (const StationLayout::Line &ln : layout->station().lines)
            if (ln.tags.size() > most) { most = ln.tags.size(); line = ln.line; }
        CHECK(layout->runLineInSimulator(line, RfidTag::DirNominal) == sim && sim->followsLayout(),
              qPrintable(QStringLiteral("Simulator ▸ Line %1: run in the workspace's simulator").arg(line)));
        const RfidTag::Route route = layout->lineAsSimRoute(line, RfidTag::DirNominal);
        CHECK(route.tags.size() > 3, "fixture: the line's tags");
        if (route.tags.size() < 4) return;
        const QString a = route.tags.at(1).name, b = route.tags.at(3).name;

        // ---- in step --------------------------------------------------------------------------------
        layout->selectTags({ a });
        settle417();
        CHECK(ws->editedTag() == a && ws->editor() && ws->editor()->tag() == layoutTag(layout, a),
              qPrintable(QStringLiteral("a tag picked in the layout (%1): its fields in the editor").arg(a)));
        CHECK(simCurrentTag(sim) == a, qPrintable(QStringLiteral("and its row in the simulator's Tags (%1)").arg(simCurrentTag(sim))));
        auto *simTable = sim->findChild<QTableWidget *>(QStringLiteral("locoSimTags"));
        int rowB = -1;
        for (int r = 0; simTable && r < simTable->rowCount(); ++r)
            if (simTable->item(r, 1) && simTable->item(r, 1)->text() == b) rowB = r;
        CHECK(rowB >= 0, "fixture: the other tag's row in the simulator");
        if (rowB < 0) return;
        simTable->selectRow(rowB);
        settle417();
        const QStringList sel = layout->selectedTags();
        CHECK(ws->editedTag() == b && sel == QStringList{ b },
              qPrintable(QStringLiteral("picked in the simulator (%1): selected in the layout, in the editor").arg(b)));

        // ---- an edit, applied: into the layout, and the running line follows ------------------------
        const QByteArray before = layoutTag(layout, b);
        const RfidTag::Summary s = RfidTag::summary(before);
        CHECK(ws->editor() && ws->editor()->setField(QStringLiteral("tin_nom"), s.tinNom == 5 ? 6 : 5), "the editor: TIN nominal changed");
        CHECK(ws->applyEditor(), "Apply");
        settle417();
        const QByteArray after = layoutTag(layout, b);
        CHECK(after != before && RfidTag::summary(after).tinNom == (s.tinNom == 5 ? 6 : 5) && RfidTag::summary(after).crcOk,
              "the layout has the tag as edited, its CRC-30 made");
        CHECK(simTag(sim, b) == after, "the simulator's route has it too (the line follows the layout)");
        CHECK(layout->undo() && layoutTag(layout, b) == before, "one Undo step in the layout");
        settle417();
        CHECK(ws->editor() && ws->editor()->tag() == before, "the editor shows the tag as it is again");

        // ---- closed: the simulator handed back ------------------------------------------------------
        // The layout holds an edit (Apply, then Undo): closing asks first. Discard.
        bool asked = false;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, [&asked]() {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                asked = true;
                if (QAbstractButton *discard = box->button(QMessageBox::Discard)) discard->click();
            }
        });
        poll.start(20);
        ws->close();
        poll.stop();
        settle417();
        CHECK(asked, "edits not saved in the layout: closing asks first");
        CHECK(!main.findChild<RfidWorkspaceWindow *>(), "the workspace closes");
        LocoSimWindow *back = main.findChild<LocoSimWindow *>();
        CHECK(back == sim && back->isWindow() && !back->isVisible() && back->parentWidget() == &main,
              "the simulator is the main window's own window again, hidden");
        if (QAction *simAct = main.findChild<QAction *>(QStringLiteral("actLocoSim"))) simAct->trigger();
        settle417();
        CHECK(back->isVisible() && back->isWindow(), "Loco Simulator… shows it, as before");
        back->close();
        settle417();
    }();
    if (!catalogWasLoaded) FieldCatalog::instance().clear();
    Settings::setStationLayoutLastFile(lastLayout);
}
