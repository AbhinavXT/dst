#include "testutil.h"

#include "fieldcatalog.h"
#include "logmodel.h"
#include "mainwindow.h"
#include "messagedispatcher.h"
#include "missionpicker.h"
#include "missionreport.h"
#include "querylineedit.h"

#include <QAbstractProxyModel>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QMenu>
#include <QTabWidget>
#include <QTableView>

// =============================================================================
//  Session 339 — "Mission ▾" in the log header: the missions of the tab on
//  show; choosing one filters every tab to its span (a visible after: /
//  before: query); All missions takes it off. Real capture: the 2026-10-08
//  excerpt (session 219: the stretch before, and three missions), fed as
//  sources 81 and 82.
// =============================================================================

namespace {
void feed(MessageDispatcher &disp, int source)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/2026-10-08/loco_1_1_08102026_105200.cap"));
    if (!f.open(QIODevice::ReadOnly)) return;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3 || !l.startsWith('@')) continue;
        disp.ingestLocal(source, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(),
                         QString());
    }
    disp.drainNow();
    QApplication::processEvents();
}

// Each tab's filter text, by tab title.
QStringList filterTexts(MainWindow &w)
{
    QStringList out;
    auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
    for (int i = 0; tabs && i < tabs->count(); ++i)
        for (QueryLineEdit *e : tabs->widget(i)->findChildren<QueryLineEdit *>())
            if (e->objectName() != QLatin1String("findEdit")) { out << e->text(); break; }
    return out;
}

QAction *item(MissionPicker *p, const QString &startsWith)
{
    emit p->menu()->aboutToShow();
    for (QAction *a : p->menu()->actions())
        if (a->text().startsWith(startsWith)) return a;
    return nullptr;
}
}  // namespace

TEST_SUITE(session339)
{
    const QString t = MissionPicker::itemText(2, QDateTime(QDate(2026, 10, 8), QTime(10, 58)).toMSecsSinceEpoch(),
                                              QDateTime(QDate(2026, 10, 8), QTime(11, 30)).toMSecsSinceEpoch(),
                                              QStringLiteral("2 (On_Sight)"));
    CHECK(t.startsWith(QStringLiteral("Mission 2 · 10:58–11:30 · On_Sight · ")), QByteArray("a mission's line: ") + t.toUtf8());
    CHECK(MissionPicker::itemText(0, 0, 60000, QString()).startsWith(QStringLiteral("Before the first start of mission · ")),
          "mission 0 is the stretch before the first start");

    const bool catalogWasLoaded = FieldCatalog::instance().isLoaded();
    [&]() {
        MainWindow w;
        auto *disp = w.findChild<MessageDispatcher *>();
        auto *picker = w.findChild<MissionPicker *>(QStringLiteral("missionPicker"));
        CHECK(disp && picker, "the log header has Mission ▾");
        if (!disp || !picker) return;
        CHECK(picker->text() == QStringLiteral("All missions"), "it starts on All missions");

        feed(*disp, 81);
        feed(*disp, 82);
        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
        CHECK(tabs && tabs->count() == 2, "fixture: two tabs of the real excerpt");
        if (!tabs || tabs->count() != 2) return;
        tabs->setCurrentIndex(0);
        QApplication::processEvents();

        const QVector<Missions::Mission> ms = Missions::split(disp->modelForKey(QStringLiteral("81_1")));
        CHECK(ms.size() == 4, "fixture: the stretch before and three missions");
        if (ms.size() != 4) return;

        CHECK(item(picker, QStringLiteral("Before the first start")) && item(picker, QStringLiteral("Mission 3 · ")),
              "the menu lists the stretch before and each mission");
        // Fetched last: each item() rebuilds the menu, and clear() deletes
        // the actions it held.
        QAction *m2 = item(picker, QStringLiteral("Mission 2 · "));
        if (!m2) return;
        m2->trigger();
        QApplication::processEvents();
        const QStringList f = filterTexts(w);
        CHECK(f.size() == 2 && f.at(0).startsWith(QStringLiteral("after:")) && f.at(0).contains(QStringLiteral(" before:"))
                  && f.at(0) == f.at(1),
              QByteArray("both tabs filtered to the same span: ") + f.join(QStringLiteral(" | ")).toUtf8());
        CHECK(picker->text() == QStringLiteral("Mission 2"), "the button names the mission");

        // The rows left are mission 2's.
        QTableView *view = tabs->widget(0)->findChild<QTableView *>();
        LogModel *model = disp->modelForKey(QStringLiteral("81_1"));
        int inside = 0, outside = 0;
        for (int r = 0; view && r < view->model()->rowCount(); ++r) {
            const qint64 ms2 = model->entryAt(qobject_cast<QAbstractProxyModel *>(view->model())
                                                  ->mapToSource(view->model()->index(r, 0)).row())->epochMs;
            (ms2 >= ms[2].fromMs && ms2 <= ms[2].toMs ? inside : outside)++;
        }
        CHECK(inside > 0 && outside == 0, QByteArray("only mission 2's rows remain (") + QByteArray::number(inside) + ")");

        // A filter the operator typed is not taken off by All missions.
        QAction *all = item(picker, QStringLiteral("All missions"));
        CHECK(all && all->isChecked() == false, "All missions is not ticked while a mission is shown");
        for (QueryLineEdit *e : tabs->widget(1)->findChildren<QueryLineEdit *>())
            if (e->objectName() != QLatin1String("findEdit")) { e->setText(QStringLiteral("@arp")); emit e->returnPressed(); break; }
        all->trigger();
        QApplication::processEvents();
        const QStringList cleared = filterTexts(w);
        CHECK(cleared.size() == 2 && cleared.at(0).isEmpty() && cleared.at(1) == QStringLiteral("@arp"),
              "All missions: the mission's filter comes off; one typed since stays");
        CHECK(picker->text() == QStringLiteral("All missions"), "and the button says so");
    }();
    if (!catalogWasLoaded) FieldCatalog::instance().clear();
}
