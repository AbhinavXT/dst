#include "testutil.h"

#include "fieldcatalog.h"
#include "laneband.h"
#include "logmodel.h"
#include "mainwindow.h"
#include "messagedispatcher.h"
#include "settings.h"
#include "theme.h"
#include "uistyle.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>

// =============================================================================
//  Session 294 — the Lanes ▾ menu. A button on the band (and right-click)
//  opens one menu: the five built-in lanes as tick boxes, a submenu per
//  packet the tab carries ("DOP1 — Digital outputs, bank 1") with its
//  fields ticked when shown, a search box over every packet's fields, and
//  Reset to default lanes. Ticking keeps the menu open. Hidden built-in
//  lanes are one list for every tab, like the custom lanes.
//  Real run: replay/loco_1_1_27062026_170217.cap.
// =============================================================================

namespace {

QPoint at(const LaneBand &b, int row, qint64 ms)
{
    const QRect r = b.laneRect(row);
    const double t = double(ms - b.fromMs()) / double(qMax<qint64>(1, b.toMs() - b.fromMs()));
    return QPoint(r.left() + int(t * r.width()), r.center().y());
}

QAction *named(QMenu *menu, const QString &objectName)
{
    for (QAction *a : menu->actions())
        if (a->objectName() == objectName) return a;
    return nullptr;
}

QAction *texted(QMenu *menu, const QString &text)
{
    for (QAction *a : menu->actions())
        if (a->text() == text) return a;
    return nullptr;
}

QMenu *submenu(QMenu *menu, const QString &objectName)
{
    for (QAction *a : menu->actions())
        if (a->menu() && a->menu()->objectName() == objectName) return a->menu();
    return nullptr;
}

void feed(MessageDispatcher &disp, int source, int maxLines = -1)
{
    QFile run(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_170217.cap"));
    if (!run.open(QIODevice::ReadOnly)) return;
    int n = 0;
    while (!run.atEnd() && (maxLines < 0 || n < maxLines)) {
        const QByteArray l = run.readLine().trimmed();
        if (!l.startsWith('@')) continue;
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3) continue;
        disp.ingestLocal(source, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(),
                         QString());
        ++n;
    }
    disp.drainNow();
}

}  // namespace

TEST_SUITE(session294)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    if (!catalogWasLoaded) FieldCatalog::instance().load(QStringLiteral(DL_SRC_DIR "/fieldmap.json"));

    CHECK(LaneBand::packetTitle(QStringLiteral("dop1")) == QStringLiteral("DOP1 — Digital outputs, bank 1"),
          QByteArray("a packet's menu title: its name and what it is: ") + LaneBand::packetTitle(QStringLiteral("dop1")).toUtf8());

    {
        MessageDispatcher disp;
        feed(disp, 21);
        LogModel *model = disp.modelForKey(QStringLiteral("21_1"));
        CHECK(model && model->count() > 1000, "fixture: the tab's rows");

        QWidget host;
        auto *lay = new QVBoxLayout(&host);
        auto *band = new LaneBand;
        lay->addWidget(band);
        lay->addStretch(1);
        host.resize(1200, 400);
        host.show();
        band->setModel(model, QStringLiteral("21_1"), QStringLiteral("21_1"));
        QCoreApplication::processEvents();

        // ---- the button ----------------------------------------------------------------
        QToolButton *button = band->menuButton();
        CHECK(button && button->isVisible() && button->text() == QStringLiteral("Lanes ▾"),
              "a Lanes ▾ button on the band");
        CHECK(button && button->geometry().bottom() < band->laneRect(0).top(), "in a header row above the lanes");

        // ---- the menu --------------------------------------------------------------------
        QString toggledName;
        bool toggledShown = true;
        QVector<CustomLane> added, removed;
        int resets = 0;
        QObject::connect(band, &LaneBand::builtInLaneToggled, [&](const QString &n, bool shown) { toggledName = n; toggledShown = shown; });
        QObject::connect(band, &LaneBand::addLaneRequested, [&](const CustomLane &l) { added << l; });
        QObject::connect(band, &LaneBand::removeLaneRequested, [&](const CustomLane &l) { removed << l; });
        QObject::connect(band, &LaneBand::resetLanesRequested, [&]() { ++resets; });

        std::unique_ptr<QMenu> menu(band->buildLaneMenu(&host));
        QStringList builtIns;
        bool allTicked = true;
        for (const QString &l : LaneBand::builtInLanes()) {
            QAction *a = named(menu.get(), QStringLiteral("laneBuiltIn_") + l);
            if (a) builtIns << a->text();
            allTicked = allTicked && a && a->isCheckable() && a->isChecked();
        }
        CHECK(builtIns == QStringList({ "Mode", "Safety", "RFID", "Link", "Faults" }) && allTicked,
              "the five built-in lanes, ticked");
        QMenu *lsrp = submenu(menu.get(), QStringLiteral("laneMenu_lsrp"));
        CHECK(lsrp && lsrp->title().startsWith(QLatin1String("LSRP")), "a submenu per packet the tab carries: LSRP");
        QAction *speed = lsrp ? texted(lsrp, QStringLiteral("TRAIN_SPEED")) : nullptr;
        CHECK(speed && speed->isCheckable() && !speed->isChecked(), "its fields as tick boxes, TRAIN_SPEED not shown yet");
        QAction *reset = named(menu.get(), QStringLiteral("laneReset"));
        CHECK(reset && !reset->isEnabled(), "Reset is off while the lanes are the default");

        // Hide Link: the band asks; the owner (MainWindow) hands the list back.
        if (QAction *link = named(menu.get(), QStringLiteral("laneBuiltIn_Link"))) link->trigger();
        CHECK(toggledName == QLatin1String("Link") && !toggledShown, "unticking Link asks for it hidden");
        band->setHiddenLanes({ QStringLiteral("Link") });
        QCoreApplication::processEvents();
        CHECK(band->laneNames() == QStringList({ "Mode", "Safety", "RFID", "Faults" }), "Link is gone from the band");

        // Hide Mode too: RFID moves up a row, and still describes itself there.
        band->setHiddenLanes({ QStringLiteral("Mode"), QStringLiteral("Link") });
        QCoreApplication::processEvents();
        const QVector<RunReport::Change> &tags = band->summary().tagReads;
        if (tags.size() >= 2) {
            const qint64 mid = (tags.at(0).ms + tags.at(1).ms) / 2;
            CHECK(band->describeAt(at(*band, 1, mid)).startsWith(QLatin1String("RFID tag")),
                  "with Mode hidden, RFID is the second row and hovers as RFID");
        }

        // A field: ticked in its packet's submenu.
        if (speed) speed->trigger();
        CHECK(added.size() == 1 && added.first() == (CustomLane{ QStringLiteral("lsrp"), QStringLiteral("TRAIN_SPEED") }),
              "ticking TRAIN_SPEED asks for its lane");
        band->setCustomLanes(added);
        QCoreApplication::processEvents();
        CHECK(band->laneNames().contains(QStringLiteral("TRAIN_SPEED")), "and the band shows it");

        // ---- a fresh menu: the state, the search, kept in step ------------------------------
        menu.reset(band->buildLaneMenu(&host));
        QMenu *lsrp2 = submenu(menu.get(), QStringLiteral("laneMenu_lsrp"));
        CHECK(lsrp2 && lsrp2->title().endsWith(QLatin1String("(1)")), "the packet's title counts its shown fields");
        QAction *speed2 = lsrp2 ? texted(lsrp2, QStringLiteral("TRAIN_SPEED")) : nullptr;
        CHECK(speed2 && speed2->isChecked(), "TRAIN_SPEED ticked");
        QAction *mode = named(menu.get(), QStringLiteral("laneBuiltIn_Mode"));
        CHECK(mode && !mode->isChecked(), "Mode unticked");
        QAction *reset2 = named(menu.get(), QStringLiteral("laneReset"));
        CHECK(reset2 && reset2->isEnabled(), "Reset is on once something differs");

        auto *search = menu->findChild<QLineEdit *>(QStringLiteral("laneFieldSearch"));
        CHECK(search != nullptr, "a search box at the top");
        if (search) {
            search->setText(QStringLiteral("train_sp"));
            QAction *hit = texted(menu.get(), QStringLiteral("LSRP ▸ TRAIN_SPEED"));
            CHECK(hit && hit->isChecked(), "searching \"train_sp\" lists LSRP ▸ TRAIN_SPEED, ticked");
            if (hit) hit->trigger();
            CHECK(removed.size() == 1 && speed2 && !speed2->isChecked(),
                  "unticking the search result asks for the lane removed, and unticks the submenu's too");
            search->setText(QStringLiteral("zzzz_nothing"));
            CHECK(texted(menu.get(), QStringLiteral("No field matches “zzzz_nothing”")) != nullptr,
                  "no match says so");
            search->clear();
            CHECK(!texted(menu.get(), QStringLiteral("LSRP ▸ TRAIN_SPEED")), "cleared: the results go");
        }

        // ---- a click ticks and keeps the menu open -------------------------------------------
        menu->popup(host.mapToGlobal(QPoint(20, 20)));
        QCoreApplication::processEvents();
        QAction *safety = named(menu.get(), QStringLiteral("laneBuiltIn_Safety"));
        if (safety && menu->isVisible()) {
            const QPointF p = menu->actionGeometry(safety).center();
            QMouseEvent release(QEvent::MouseButtonRelease, p, menu->mapToGlobal(p.toPoint()), Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(menu.get(), &release);
            CHECK(!safety->isChecked() && toggledName == QLatin1String("Safety") && menu->isVisible(),
                  "a click unticks Safety and the menu stays open");
        } else {
            CHECK(false, "fixture: the menu shows");
        }
        menu->hide();

        if (reset2) reset2->trigger();
        CHECK(resets == 1, "Reset to default lanes asks for it");

        // ---- every lane hidden: the band stays, with the way back --------------------------
        band->setCustomLanes({});
        band->setHiddenLanes(LaneBand::builtInLanes());
        QCoreApplication::processEvents();
        CHECK(band->laneNames().isEmpty() && band->isVisible() && band->menuButton()->isVisible(),
              "all lanes hidden: the band and its Lanes ▾ button stay, so they can come back");
    }

    // ---- MainWindow: one list for every tab, saved -----------------------------------------
    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");
    QTemporaryDir where;
    const QString exe = where.filePath(QStringLiteral("DLConsole.exe"));
    { QFile f(exe); CHECK(f.open(QIODevice::WriteOnly), "a stand-in for the .exe"); }
    qputenv("DLCONSOLE_PACKAGE", exe.toLocal8Bit());
    {
        MainWindow w;
        w.resize(1200, 800);
        w.show();
        MessageDispatcher *disp = w.findChild<MessageDispatcher *>();
        CHECK(disp != nullptr, "fixture: the dispatcher");
        if (disp) {
            feed(*disp, 21, 400);
            feed(*disp, 22, 400);
        }
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
        const QList<LaneBand *> bands = w.findChildren<LaneBand *>();
        CHECK(bands.size() >= 2, QByteArray("fixture: a band per tab (") + QByteArray::number(bands.size()) + ")");
        if (bands.size() >= 2) {
            emit bands.first()->builtInLaneToggled(QStringLiteral("Faults"), false);
            emit bands.first()->builtInLaneToggled(QStringLiteral("Mode"), false);
            bool everywhere = true;
            for (LaneBand *b : bands) everywhere = everywhere && b->hiddenLanes() == QStringList({ "Mode", "Faults" });
            QSettings ini(Settings::iniPath(), QSettings::IniFormat);
            CHECK(everywhere && ini.value(QStringLiteral("ui/hiddenLanes")).toStringList() == QStringList({ "Mode", "Faults" }),
                  "hidden on one tab: hidden on every tab, in the band's order, and saved (ui/hiddenLanes)");
            emit bands.last()->addLaneRequested(CustomLane{ QStringLiteral("lsrp"), QStringLiteral("TRAIN_SPEED") });
            emit bands.last()->resetLanesRequested();
            bool reset = true;
            for (LaneBand *b : bands) reset = reset && b->hiddenLanes().isEmpty() && b->customLanes().isEmpty();
            QSettings again(Settings::iniPath(), QSettings::IniFormat);
            CHECK(reset && !again.contains(QStringLiteral("ui/hiddenLanes"))
                      && again.value(QStringLiteral("ui/customLanes")).toStringList().isEmpty(),
                  "Reset to default lanes: every band back to the five, nothing saved");
        }
    }
    if (!catalogWasLoaded) FieldCatalog::instance().clear();
    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);
}
