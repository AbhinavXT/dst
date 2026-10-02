#include "testutil.h"
#include "layoutaudit.h"

#include "comparewindow.h"
#include "emptystate.h"
#include "logmodel.h"
#include "messagedispatcher.h"
#include "namemap.h"

#include <QCoreApplication>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>

// =============================================================================
//  Session 129 — UI revamp, tool windows 5: the Compare window.
//
//  "Pick a source for this pane." no longer sits over a full table; the
//  panes get the height; no boxes round boxes. Columns stay the setting's
//  business (test_comparetools), as in the log tabs.
//  Real traffic: replay/loco_1_1_27062026_140226.cap, as two sources.
// =============================================================================

TEST_SUITE(session129)
{
    // ---- EmptyState::refresh: a rebound view re-checks ---------------------
    {
        QTableView view;
        view.resize(400, 300);
        view.show();
        EmptyState::attach(&view, QStringLiteral("Pick a source for this pane."));
        CHECK(EmptyState::isShowing(&view), "no model: the overlay shows");
        QStandardItemModel full(5, 2);
        view.setModel(&full);
        // setModel emits no row signal: this is the state the bug left.
        EmptyState::refresh(&view);
        CHECK(!EmptyState::isShowing(&view), "rebound to a model with rows, refresh hides it");
        view.setModel(nullptr);
        EmptyState::refresh(&view);
        CHECK(EmptyState::isShowing(&view), "and unbound, it shows again");
        QTableView bare;
        EmptyState::refresh(&bare);   // no overlay: must be a no-op
        CHECK(true, "refresh on a view without an overlay is harmless");
    }

    // ---- the window, fed real traffic from two sources ----------------------
    MessageDispatcher disp;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    int n = 0;
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd() && n < 200) {
            const QByteArray l = f.readLine().trimmed();
            if (!l.startsWith('@')) continue;
            disp.ingestLocal(n % 2 ? 21 : 81, 1, l, 1782558147000LL + n * 137, QString());
            ++n;
        }
    }
    disp.drainNow();
    CHECK(n == 200 && disp.knownKeys().size() >= 2, "fixture: real capture lines from two sources");

    NameMap names;
    CompareWindow w(&disp, &names);
    w.resize(1100, 720);
    w.show();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    QList<QTableView *> panes;
    for (QTableView *v : w.findChildren<QTableView *>())
        if (qobject_cast<LogModel *>(v->model())) panes << v;
    CHECK(panes.size() == 2, "two panes, each bound to a source");

    for (QTableView *v : panes) {
        CHECK(v->model()->rowCount() > 0 && !EmptyState::isShowing(v),
              "a pane with rows does not say \"Pick a source\" over them");
        CHECK(!v->isColumnHidden(LogModel::ColMessage), "Message shows in every pane");
    }

    // ---- the panes get the height -----------------------------------------
    if (!panes.isEmpty()) {
        QWidget *detail = nullptr;
        for (QLabel *l : w.findChildren<QLabel *>())
            if (l->text().startsWith(QLatin1String("RAW BYTES"))) detail = l->parentWidget();
        CHECK(detail != nullptr, "the raw-bytes section has a section label");
        CHECK(detail && panes.first()->height() > detail->height(),
              QByteArray("the panes are taller than the details under them (")
                  + QByteArray::number(panes.first()->height()) + " vs "
                  + QByteArray::number(detail ? detail->height() : -1) + " px)");
    }

    // ---- no box round boxes; fits; nothing loose -----------------------------
    bool outerBox = false;
    for (QGroupBox *g : w.findChildren<QGroupBox *>())
        if (g->title().contains(QLatin1String("for selection"))) outerBox = true;
    CHECK(!outerBox, "the details are not boxed round the raw panel's own boxes");
    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366,
          QByteArray("fits a 1366 x 768 laptop (minimum ")
              + QByteArray::number(w.minimumSizeHint().width()) + " x "
              + QByteArray::number(w.minimumSizeHint().height()) + ")");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (")
                               + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
