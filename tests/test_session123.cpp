#include "testutil.h"
#include "layoutaudit.h"

#include "messagedispatcher.h"
#include "searchwindow.h"
#include "uistyle.h"

#include <QFile>
#include <QFontInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

// =============================================================================
//  Session 123 — UI revamp, tool windows 2: Search all sources.
//  Real traffic: replay/loco_1_1_27062026_140226.cap.
// =============================================================================

TEST_SUITE(session123)
{
    MessageDispatcher disp;
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    int n = 0;
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd() && n < 300) {
            const QByteArray l = f.readLine().trimmed();
            if (!l.startsWith('@')) continue;
            disp.ingestLocal(n % 3 ? 21 : 81, 1, l, 1782558147000LL + n * 137, QString());
            ++n;
        }
    }
    disp.drainNow();
    CHECK(n > 100, "fixture: real capture lines");

    SearchWindow w(&disp, nullptr);
    w.resize(1100, 720);
    w.show();
    w.setQueryText(QStringLiteral("@lsrp OR @dmi"), true);

    CHECK(LayoutAudit::orphans(&w).isEmpty(), "no visible widget outside every layout");
    QWidget *panel = w.findChild<QWidget *>(QStringLiteral("searchQueryPanel"));
    CHECK(panel && panel->property("dlRole").toString() == QLatin1String("panel"), "the query is one panel");
    QPushButton *run = nullptr;
    for (QPushButton *b : w.findChildren<QPushButton *>()) if (b->text() == QLatin1String("Search")) run = b;
    CHECK(run && run->property("dlRole").toString() == QLatin1String("primary") && panel && panel->isAncestorOf(run),
          "Search is the primary action, inside it");

    QTableWidget *t = w.findChild<QTableWidget *>();
    CHECK(t && t->rowCount() > 10, "the search found real rows");
    // Every timestamp fits its column, in the font it is drawn in.
    bool fits = t && t->rowCount() > 0;
    for (int r = 0; t && r < t->rowCount(); ++r) {
        QTableWidgetItem *it = t->item(r, 0);
        if (!it || it->text().isEmpty() || t->columnSpan(r, 0) > 1) continue;
        if (QFontMetrics(it->font()).horizontalAdvance(it->text()) + 8 > t->columnWidth(0)) fits = false;
        if (!QFontInfo(it->font()).fixedPitch()) fits = false;
    }
    CHECK(fits, "every timestamp fits its column, in the mono face");

    bool explainMono = false;
    for (QLabel *l : w.findChildren<QLabel *>())
        if (l->text().startsWith(QLatin1String("OR"))) explainMono = QFontInfo(l->font()).fixedPitch();
    CHECK(explainMono, "the parse tree is in the real mono face (\"monospace\" does not resolve on Windows)");
}
