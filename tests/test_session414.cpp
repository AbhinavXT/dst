#include "testutil.h"

#include "bookmarks.h"
#include "colorrules.h"
#include "decodeworkbench.h"
#include "fieldcatalog.h"
#include "logentry.h"
#include "logmodel.h"
#include "mainwindow.h"
#include "messagedispatcher.h"
#include "namemap.h"
#include "packetmakerdialog.h"
#include "rowclicks.h"
#include "sessionfile.h"
#include "sessionwindow.h"
#include "settingsdialog.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QMouseEvent>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>

// =============================================================================
//  Session 414 — row clicks, set in Settings ▸ Row clicks. Abhinav: "Add
//  configurable actions like double click, triple click etc that can be
//  mapped by users to things like opening a packet in decode workbench, or
//  open it in packet maker etc." Double, triple, Ctrl/Shift+double and
//  middle click each run one of the row menu's actions, or nothing; in the
//  live window's log tabs and in the .dlr viewer. Real rows: replay/.
// =============================================================================

namespace {

void settle414()
{
    for (int i = 0; i < 6; ++i) {
        QApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

void send414(QWidget *vp, QEvent::Type t, Qt::MouseButton b, const QPoint &p, Qt::KeyboardModifiers m = Qt::NoModifier)
{
    const Qt::MouseButtons held = t == QEvent::MouseButtonRelease ? Qt::MouseButtons() : Qt::MouseButtons(b);
    QMouseEvent e(t, QPointF(p), QPointF(vp->mapToGlobal(p)), b, held, m);
    QApplication::sendEvent(vp, &e);
}

// What Qt delivers for a double-click: press, release, double-click, release.
void double414(QWidget *vp, const QPoint &p, Qt::KeyboardModifiers m = Qt::NoModifier)
{
    send414(vp, QEvent::MouseButtonPress, Qt::LeftButton, p, m);
    send414(vp, QEvent::MouseButtonRelease, Qt::LeftButton, p, m);
    send414(vp, QEvent::MouseButtonDblClick, Qt::LeftButton, p, m);
    send414(vp, QEvent::MouseButtonRelease, Qt::LeftButton, p, m);
}

void middle414(QWidget *vp, const QPoint &p)
{
    send414(vp, QEvent::MouseButtonPress, Qt::MiddleButton, p);
    send414(vp, QEvent::MouseButtonRelease, Qt::MiddleButton, p);
}

void bind414(const QString &d, const QString &t, const QString &cd, const QString &sd, const QString &m)
{
    RowClicks::setBinding(RowClicks::Double, d);
    RowClicks::setBinding(RowClicks::Triple, t);
    RowClicks::setBinding(RowClicks::CtrlDouble, cd);
    RowClicks::setBinding(RowClicks::ShiftDouble, sd);
    RowClicks::setBinding(RowClicks::Middle, m);
}

template <typename T>
int shown414()
{
    int n = 0;
    for (QWidget *w : QApplication::allWidgets())
        if (qobject_cast<T *>(w) && w->isWindow() && w->isVisible()) ++n;
    return n;
}

void closeTools414()
{
    for (QWidget *w : QApplication::allWidgets())
        if ((qobject_cast<DecodeWorkbench *>(w) || qobject_cast<PacketMakerDialog *>(w)) && w->isWindow()) w->close();
    settle414();
}

QVector<QByteArray> realLines414(int max)
{
    QVector<QByteArray> out;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_151052.cap"));
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd() && out.size() < max) {
        const QByteArray l = f.readLine().trimmed();
        if (l.startsWith('@') && l.split(' ').size() >= 3) out << l;
    }
    return out;
}

qint64 ms414(const QByteArray &l)
{
    return QDateTime::fromString(QString::fromLatin1(l.split(' ').at(1)), Qt::ISODate).toMSecsSinceEpoch();
}

QTableView *viewWithRows414(QWidget *w, int rows)
{
    for (QTableView *tv : w->findChildren<QTableView *>())
        if (tv->model() && tv->model()->rowCount() == rows && tv->isVisible()) return tv;
    return nullptr;
}

}  // namespace

TEST_SUITE(session414)
{
    QStringList was;
    for (int g = 0; g < RowClicks::GestureCount; ++g) was << RowClicks::binding(RowClicks::Gesture(g));
    struct Restore {
        QStringList was;
        ~Restore() { for (int g = 0; g < was.size(); ++g) RowClicks::setBinding(RowClicks::Gesture(g), was.at(g)); }
    } restore{ was };

    // ---- the gestures, on a plain table ----------------------------------------------------------
    {
        QStandardItemModel m(6, 2);
        QTableView view;
        view.setModel(&m);
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        view.setSelectionMode(QAbstractItemView::ExtendedSelection);
        view.resize(400, 300);
        view.show();
        settle414();
        QStringList ran;
        RowClickFilter::install(&view, [&ran](QTableView *, const QModelIndex &i, const QString &id) {
            ran << QStringLiteral("%1@%2").arg(id).arg(i.row());
        });
        QWidget *vp = view.viewport();
        auto at = [&view, &m](int row) { return view.visualRect(m.index(row, 1)).center(); };

        bind414(QString(), QString(), QString(), QString(), QString());
        double414(vp, at(1));
        middle414(vp, at(1));
        settle414();
        CHECK(ran.isEmpty(), "every gesture on Nothing (the default): nothing runs");

        bind414(QStringLiteral("workbench"), QString(), QStringLiteral("bookmark"), QStringLiteral("diff"),
                QStringLiteral("copy"));
        double414(vp, at(2));
        settle414();
        CHECK(ran == QStringList{ QStringLiteral("workbench@2") }, "double-click: its action, on the row clicked (" + ran.join(',').toUtf8() + ")");
        CHECK(view.currentIndex().row() == 2, "and that row is the current one");
        ran.clear();
        double414(vp, at(3), Qt::ControlModifier);
        settle414();
        CHECK(ran == QStringList{ QStringLiteral("bookmark@3") }, "Ctrl + double-click: its own (" + ran.join(',').toUtf8() + ")");
        ran.clear();
        // (The Shift span itself is Qt's, from the keyboard's state: a synthetic event cannot hold Shift for it.)
        double414(vp, at(4), Qt::ShiftModifier);
        settle414();
        CHECK(ran == QStringList{ QStringLiteral("diff@4") } && view.currentIndex().row() == 4,
              "Shift + double-click: its own action, on the row clicked");
        ran.clear();
        middle414(vp, at(5));
        settle414();
        CHECK(ran == QStringList{ QStringLiteral("copy@5") } && view.currentIndex().row() == 5,
              "middle-click: its own, on that row");
        ran.clear();
        send414(vp, QEvent::MouseButtonPress, Qt::MiddleButton, at(1));
        send414(vp, QEvent::MouseButtonRelease, Qt::MiddleButton, at(4));
        settle414();
        CHECK(ran.isEmpty(), "a middle press on one row, released on another: nothing");

        // Triple: with it set, the double waits; a third press makes it a triple, the double never runs.
        RowClicks::setBinding(RowClicks::Triple, QStringLiteral("packetMaker"));
        double414(vp, at(2));
        settle414();
        CHECK(ran.isEmpty(), "triple-click set: a double-click waits a moment");
        send414(vp, QEvent::MouseButtonPress, Qt::LeftButton, at(2));
        send414(vp, QEvent::MouseButtonRelease, Qt::LeftButton, at(2));
        settle414();
        CHECK(ran == QStringList{ QStringLiteral("packetMaker@2") }, "a third press: triple-click's action alone (" + ran.join(',').toUtf8() + ")");
        ran.clear();
        double414(vp, at(0));
        QElapsedTimer t;
        t.start();
        while (ran.isEmpty() && t.elapsed() < 3 * QApplication::doubleClickInterval()) QApplication::processEvents();
        CHECK(ran == QStringList{ QStringLiteral("workbench@0") }, "no third press: after the wait, double-click's action");
        CHECK(RowClickFilter::gestureOf(Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier, false) == RowClicks::CtrlDouble,
              "Ctrl and Shift both held: Ctrl wins");
    }

    // ---- Settings ▸ Row clicks ----------------------------------------------------------------
    {
        bind414(QStringLiteral("note"), QString(), QString(), QString(), QStringLiteral("why"));
        SettingsDialog d;
        d.show();
        settle414();
        QTabWidget *tabs = d.findChild<QTabWidget *>();
        CHECK(tabs && tabs->count() == 2 && tabs->tabText(1) == QStringLiteral("Row clicks"), "Settings has a Row clicks tab");
        QList<QComboBox *> boxes;
        for (int g = 0; g < RowClicks::GestureCount; ++g)
            boxes << d.findChild<QComboBox *>(QStringLiteral("rowClick_") + RowClicks::gestureKey(RowClicks::Gesture(g)));
        CHECK(!boxes.contains(nullptr) && boxes.first()->count() == RowClicks::actions().size(),
              "one choice per gesture, each offering every row action and Nothing");
        if (!boxes.contains(nullptr)) {
            CHECK(boxes.at(RowClicks::Double)->currentData().toString() == QStringLiteral("note")
                      && boxes.at(RowClicks::Middle)->currentData().toString() == QStringLiteral("why")
                      && boxes.at(RowClicks::Triple)->currentIndex() == 0,
                  "it opens on what is set");
            boxes.at(RowClicks::Double)->setCurrentIndex(boxes.at(RowClicks::Double)->findData(QStringLiteral("workbench")));
            boxes.at(RowClicks::Middle)->setCurrentIndex(0);
            QMetaObject::invokeMethod(&d, "onApply");
            CHECK(RowClicks::binding(RowClicks::Double) == QStringLiteral("workbench") && RowClicks::binding(RowClicks::Middle).isEmpty(),
                  "Apply: kept in the ini");
        }
    }

    const QVector<QByteArray> lines = realLines414(400);
    CHECK(lines.size() == 400, "fixture: 400 real rows from replay/loco_1_1_27062026_151052.cap");
    if (lines.size() < 400) return;

    // ---- the live window's log tab ------------------------------------------------------------
    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    [&]() {
        MainWindow w;
        w.resize(1400, 900);
        w.show();
        auto *disp = w.findChild<MessageDispatcher *>();
        CHECK(disp, "fixture: the window's dispatcher");
        if (!disp) return;
        for (const QByteArray &l : lines) disp->ingestLocal(21, 1, l, ms414(l), QString());
        disp->drainNow();
        settle414();
        LogModel *model = disp->modelForKey(QStringLiteral("21_1"));
        QTableView *view = viewWithRows414(&w, 400);
        CHECK(model && view, "fixture: the tab and its table");
        if (!model || !view) return;
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
        auto rowAt = [view](int r) { view->scrollTo(view->model()->index(r, 1)); return view->visualRect(view->model()->index(r, 1)).center(); };
        auto entryAt = [model, proxy](int r) { return model->entryAt(proxy ? proxy->mapToSource(proxy->index(r, 0)).row() : r); };
        QWidget *vp = view->viewport();

        BookmarkStore &marks = w.bookmarks();
        const QVector<Bookmark> before = marks.all();
        bind414(QStringLiteral("workbench"), QStringLiteral("packetMaker"), QStringLiteral("bookmark"),
                QStringLiteral("dmi"), QStringLiteral("copy"));
        closeTools414();

        int r = 0;
        while (r < 400 && (entryBufferText(entryAt(r)).isEmpty() || entryAt(r)->text.startsWith("@dmi"))) ++r;
        CHECK(r < 400, "fixture: a row with bytes, not @dmi");
        if (r >= 400) return;

        double414(vp, rowAt(r));
        QElapsedTimer t;
        t.start();
        while (shown414<DecodeWorkbench>() == 0 && t.elapsed() < 3 * QApplication::doubleClickInterval()) QApplication::processEvents();
        settle414();
        CHECK(shown414<DecodeWorkbench>() == 1 && shown414<PacketMakerDialog>() == 0,
              "double-click (Decode Workbench): a workbench of the row, no Packet Maker");
        double414(vp, rowAt(r));
        send414(vp, QEvent::MouseButtonPress, Qt::LeftButton, rowAt(r));
        send414(vp, QEvent::MouseButtonRelease, Qt::LeftButton, rowAt(r));
        settle414();
        CHECK(shown414<PacketMakerDialog>() == 1 && shown414<DecodeWorkbench>() == 1,
              "triple-click (Packet Maker): a Packet Maker, and no second workbench: pm " + QByteArray::number(shown414<PacketMakerDialog>())
                  + " wb " + QByteArray::number(shown414<DecodeWorkbench>()));
        closeTools414();

        const LogEntryPtr e = entryAt(r + 1);
        const bool was = e && e->bookmarked;
        double414(vp, rowAt(r + 1), Qt::ControlModifier);
        settle414();
        CHECK(e && e->bookmarked != was && marks.has(QStringLiteral("21_1"), e->epochMs) == !was,
              "Ctrl + double-click (bookmark): that row's bookmark toggled");
        double414(vp, rowAt(r + 1), Qt::ControlModifier);
        settle414();

        QApplication::clipboard()->clear();
        middle414(vp, rowAt(r + 2));
        settle414();
        CHECK(QApplication::clipboard()->text().contains(entryAt(r + 2)->text.left(20)),
              "middle-click (copy): that row's message on the clipboard");

        // Show on DMI is offered only for an @dmi row: on another, nothing opens.
        const int windowsBefore = QApplication::topLevelWidgets().size();
        double414(vp, rowAt(r), Qt::ShiftModifier);
        settle414();
        CHECK(QApplication::topLevelWidgets().size() == windowsBefore,
              "Shift + double-click (Show on DMI) on a non-@dmi row: nothing opens (a note says why)");
        marks.replaceAll(before);
        closeTools414();
    }();
    if (!catalogWasLoaded) FieldCatalog::instance().clear();

    // ---- the .dlr viewer ---------------------------------------------------------------------
    {
        MessageDispatcher disp;
        for (const QByteArray &l : lines) disp.ingestLocal(21, 1, l, ms414(l), QString());
        disp.drainNow();
        LogModel *m = disp.modelForKey(QStringLiteral("21_1"));
        QTemporaryDir tmp;
        const QString path = tmp.filePath(QStringLiteral("21_1.dlr"));
        QFile f(path);
        CHECK(m && f.open(QIODevice::WriteOnly), "fixture: a .dlr of those rows");
        if (!m) return;
        SessionFile::FileHeader h;
        h.createdMs = ms414(lines.first());
        h.sourceId = 21;
        h.kvchId = 1;
        f.write(SessionFile::encodeHeader(h));
        int records = 0;
        for (int i = 0; i < m->rowCount(); ++i)
            if (!m->entryAt(i)->rawBytes.isEmpty()) {
                f.write(SessionFile::encodeRecord(m->entryAt(i)->epochMs, m->entryAt(i)->rawBytes));
                ++records;
            }
        f.close();

        ColorRules rules;
        NameMap names;
        SessionWindow w(&rules, &names, Theme::Light);
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        CHECK(w.loadFiles({ path }) == records, "the .dlr loads");
        w.show();
        settle414();
        QTableView *view = viewWithRows414(&w, records);
        CHECK(view, "fixture: the viewer's table");
        if (!view) return;
        bind414(QString(), QString(), QStringLiteral("packetMaker"), QString(), QStringLiteral("workbench"));
        closeTools414();
        const QPoint p = view->visualRect(view->model()->index(records / 2, 1)).center();
        middle414(view->viewport(), p);
        settle414();
        CHECK(shown414<DecodeWorkbench>() == 1 && view->currentIndex().row() == records / 2,
              "middle-click (Decode Workbench) in the .dlr viewer: a workbench of the row clicked");
        double414(view->viewport(), p, Qt::ControlModifier);
        settle414();
        CHECK(shown414<PacketMakerDialog>() == 1, "Ctrl + double-click (Packet Maker) there too");
        closeTools414();
    }
}
