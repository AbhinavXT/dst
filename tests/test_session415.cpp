#include "testutil.h"

#include "capturedecoder.h"
#include "decodeworkbench.h"
#include "fieldcatalog.h"
#include "locosim.h"
#include "locosimwindow.h"
#include "mainwindow.h"
#include "rfidtag.h"
#include "settings.h"
#include "simsentlog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>

// =============================================================================
//  Session 415 — the RFID frames the Loco Simulator sent, listed. Abhinav:
//  "Also show rfid frames sent in the loco simulator"; scope as confirmed: a
//  Sent frames list on the RFID page, tags and health frames, double-click a
//  tag to open it in the Decode Workbench. Frames from the simulator's own
//  route (tag_scenarios/LocoTcasSimulator/Configuration1.xml).
// =============================================================================

TEST_SUITE(session415)
{
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("locosim"));
    }
    LocoSimWindow window(nullptr);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    qint64 t = 1782558147000LL;
    window.setClock([&t]() { return t; });
    window.setSendSink([](const QByteArray &) {});
    QVector<QByteArray> tagsOut, healthOut;
    window.setTagSink([&](int, const QByteArray &f) { tagsOut << f; });
    window.setHealthSink([&](int, const QByteArray &f) { healthOut << f; });
    CHECK(window.loadRouteFile(QStringLiteral(":/tag_scenarios/LocoTcasSimulator/Configuration1.xml")), "the route");
    window.setRoute(0);

    SimSentLog *log = window.sentLog();
    QTabWidget *tabs = window.findChild<QTabWidget *>();
    int at = -1;
    for (int i = 0; tabs && i < tabs->count(); ++i)
        if (tabs->widget(i) == log) at = i;
    CHECK(log && at >= 0 && tabs->tabText(at) == QLatin1String("RFID frames")
              && tabs->tabText(at - 1) == QLatin1String("Frames") && log->count() == 0,
          "an RFID frames tab, after Frames, empty");
    if (!log || at < 0) return;

    // ---- a run: the route's tags and the health frames -------------------------------------------
    window.setReaderOn(1, false);
    window.setReaderOn(2, false);
    window.setReaderOn(1, true);
    window.setReaderOn(2, true);
    window.placeAtStart();
    window.motion().setSpeed(72);
    window.start();
    for (int i = 0; i < 300; ++i) {
        t += 100;
        window.tick();
        window.healthTick();
    }
    window.stop();
    QTableWidget *table = log->table();
    int tagRows = 0, healthRows = 0;
    bool bytesMatch = true;
    int ti = 0, hi = 0;
    for (int r = 0; r < table->rowCount(); ++r) {
        const bool health = table->item(r, SimSentLog::ColKind)->text() == QLatin1String("health");
        const QByteArray want = health ? healthOut.value(hi++) : tagsOut.value(ti++);
        bytesMatch = bytesMatch && table->item(r, SimSentLog::ColBytes)->text() == QString::fromLatin1(want.toHex(' ')).toUpper();
        (health ? healthRows : tagRows) += 1;
    }
    CHECK(tagRows == tagsOut.size() && healthRows == healthOut.size() && tagRows >= 4 && healthRows >= 40,
          qPrintable(QStringLiteral("30 s: a row per frame sent, %1 tags and %2 health frames").arg(tagRows).arg(healthRows)));
    CHECK(bytesMatch, "in the order sent, each row's bytes the frame written");
    CHECK(tabs->tabText(at) == QStringLiteral("RFID frames (%1)").arg(tagRows + healthRows), "the tab counts them");

    int firstTag = -1;
    for (int r = 0; r < table->rowCount() && firstTag < 0; ++r)
        if (table->item(r, SimSentLog::ColKind)->text() == QLatin1String("tag")) firstTag = r;
    const QByteArray tag = tagsOut.value(0).mid(4, 16);
    CHECK(firstTag >= 0 && table->item(firstTag, SimSentLog::ColWhat)->text() == RfidTag::nameOf(tag)
              && table->item(firstTag, SimSentLog::ColReader)->text() == QLatin1String("1")
              && table->item(firstTag, SimSentLog::ColPort)->text() == QLatin1String("port A")
              && table->item(firstTag, SimSentLog::ColAt)->text().endsWith(QLatin1String(" m")),
          "a tag row: the tag's name, reader 1, port A, where the loco was");
    CHECK(firstTag >= 0 && table->item(firstTag, SimSentLog::ColWhat)->toolTip().contains(QStringLiteral("abs_loc %1").arg(RfidTag::summary(tag).absLoc)),
          "its tooltip: the tag's fields");
    int h = 0;
    while (h < table->rowCount() && table->item(h, SimSentLog::ColKind)->text() != QLatin1String("health")) ++h;
    CHECK(h < table->rowCount() && table->item(h, SimSentLog::ColWhat)->text() == QLatin1String("OK (1)")
              && table->item(h, SimSentLog::ColAt)->text().isEmpty(),
          "a health row: OK (1)");

    // ---- Health frames off: hidden, not dropped ---------------------------------------------------
    auto *showHealth = log->findChild<QCheckBox *>(QStringLiteral("simSentShowHealth"));
    if (showHealth) showHealth->setChecked(false);
    int hidden = 0;
    for (int r = 0; r < table->rowCount(); ++r) hidden += table->isRowHidden(r) ? 1 : 0;
    CHECK(showHealth && hidden == healthRows && log->count() == tagRows + healthRows,
          "Health frames unticked: those rows hidden, still held");
    if (showHealth) showHealth->setChecked(true);

    // ---- Double-click a tag: the Workbench's @rfid line, as the decoder reads it ---------------------
    QString opened, label;
    window.setOpenInWorkbench([&](const QString &line, const QString &l) { opened = line; label = l; });
    log->openRow(firstTag);
    const CaptureLine c = CaptureDecoder::parseLine(opened);
    CHECK(c.valid && c.type == CapType::Rfid && c.bytes == tagsOut.value(0).mid(3, 17),
          qPrintable(QStringLiteral("a tag row double-clicked: an @rfid line, the reader byte and the tag (%1)").arg(opened)));
    CHECK(label.contains(RfidTag::nameOf(tag)), "the workbench named by the tag");
    opened.clear();
    log->openRow(h);
    CHECK(opened.isEmpty(), "a health row: nothing to decode it with, nothing opens");

    // ---- a fault: as it went out ------------------------------------------------------------------
    {
        window.setTagFault(RfidTag::nameOf(tag), LocoSim::TagFault::Twice, 1);
        log->clear();
        window.placeAtStart();
        window.start();
        for (int i = 0; i < 50; ++i) { t += 100; window.tick(); }
        window.stop();
        QStringList notes;
        for (int r = 0; r < table->rowCount(); ++r)
            if (table->item(r, SimSentLog::ColWhat)->text() == RfidTag::nameOf(tag)
                && table->item(r, SimSentLog::ColReader)->text() == QLatin1String("1"))
                notes << table->item(r, SimSentLog::ColNote)->text();
        CHECK(notes == QStringList({ QStringLiteral("sent twice (1st)"), QStringLiteral("sent twice (2nd)") }),
              qPrintable(QStringLiteral("Sent twice on reader 1: two rows, noted (%1)").arg(notes.join(QLatin1String(" | ")))));
        window.setTagFault(RfidTag::nameOf(tag), LocoSim::TagFault::None);
    }

    // ---- by hand, faults, not sent ----------------------------------------------------------------
    const int before = log->count();
    CHECK(window.sendTagByHand(2, RfidTag::pageX(tag), RfidTag::pageY(tag)), "fixture: a tag by hand");
    CHECK(log->count() == before + 1 && table->item(table->rowCount() - 1, SimSentLog::ColNote)->text() == QLatin1String("by hand")
              && table->item(table->rowCount() - 1, SimSentLog::ColReader)->text() == QLatin1String("2"),
          "by hand: a row, reader 2, noted \"by hand\"");
    window.setTagSink({});
    window.setHealthSink({});
    {
        LocoSimWindow closed(nullptr);
        closed.setAttribute(Qt::WA_DeleteOnClose, false);
        closed.sendTagByHand(1, RfidTag::pageX(tag), RfidTag::pageY(tag));
        QTableWidget *ct = closed.sentLog()->table();
        CHECK(ct->rowCount() == 1 && ct->item(0, SimSentLog::ColNote)->text().contains(QLatin1String("not sent")),
              "port A closed: the row says not sent");
    }

    // ---- the cap, and Clear --------------------------------------------------------------------------
    SimSentLog::Frame f;
    f.bytes = LocoSim::healthFrame(1, true);
    f.health = true;
    for (int i = 0; i < SimSentLog::kMaxRows + 10; ++i) log->add(f);
    CHECK(log->count() == SimSentLog::kMaxRows && table->rowCount() == SimSentLog::kMaxRows, "kept to the last 5000");
    auto *clear = log->findChild<QToolButton *>(QStringLiteral("simSentClear"));
    if (clear) clear->click();
    CHECK(clear && log->count() == 0 && table->rowCount() == 0 && tabs->tabText(at) == QLatin1String("RFID frames"), "Clear empties it");

    // ---- from the main window: a sent tag in a Decode Workbench -------------------------------------
    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    [&]() {
        MainWindow main;
        QAction *open = main.findChild<QAction *>(QStringLiteral("actLocoSim"));
        CHECK(open, "Tools ▸ Simulators ▸ Loco Simulator…");
        if (!open) return;
        open->trigger();
        LocoSimWindow *sim = main.findChild<LocoSimWindow *>();
        CHECK(sim && sim->sentLog(), "the main window's simulator");
        if (!sim) return;
        sim->setTagSink([](int, const QByteArray &) {});
        sim->sendTagByHand(1, RfidTag::pageX(tag), RfidTag::pageY(tag));
        sim->sentLog()->openRow(0);
        for (int i = 0; i < 5; ++i) QApplication::processEvents();
        DecodeWorkbench *wb = nullptr;
        for (DecodeWorkbench *w : main.findChildren<DecodeWorkbench *>())
            if (w->isVisible()) wb = w;
        CHECK(wb && wb->windowTitle().contains(RfidTag::nameOf(tag)),
              qPrintable(QStringLiteral("double-clicked there: a Decode Workbench of the tag (%1)").arg(wb ? wb->windowTitle() : QString())));
        if (wb) wb->close();
        sim->close();
    }();
    if (!catalogWasLoaded) FieldCatalog::instance().clear();
}
