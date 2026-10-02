#include "testutil.h"
#include "layoutaudit.h"

#include "logentry.h"
#include "roundtripwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QListWidget>
#include <QProgressBar>
#include <QSplitter>
#include <QTableWidget>
#include <QTextEdit>

// =============================================================================
//  Session 142 — UI revamp, tool windows 19: the Round-trip validator.
//
//  The results table showed four of its rows: an empty 90-px file list and
//  a detail pane at equal stretch took the rest. The file list shows once it
//  has files; results and detail share a splitter, 3 : 1. Headers left,
//  "45 frames" (was "frame(s)"), no percentage text on the bar, the intro
//  muted. A real run over 400 lines of replay/loco_1_1_27062026_140226.cap.
// =============================================================================

TEST_SUITE(session142)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    QVector<LogEntryPtr> entries;
    QFile cap(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (cap.open(QIODevice::ReadOnly))
        while (!cap.atEnd() && entries.size() < 400) {
            const QString l = QString::fromUtf8(cap.readLine()).trimmed();
            if (!l.startsWith(QLatin1Char('@'))) continue;
            auto e = LogEntryPtr::create();
            e->text = l;
            entries << e;
        }
    CHECK(entries.size() == 400, "fixture: 400 real capture lines as the live log");

    RoundTripWindow w;
    w.setLiveEntries(entries);
    w.resize(1100, 720);
    w.show();
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

    auto *files = w.findChild<QListWidget *>(QStringLiteral("roundtripFiles"));
    CHECK(files && !files->isVisible(), "no files added: the file list takes no room");

    QMetaObject::invokeMethod(&w, "onRun");
    auto *table = w.findChild<QTableWidget *>();
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 10000 && !(table && table->rowCount() > 0)) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(table && table->rowCount() == 11, QByteArray("the run lists 11 packet types (") + QByteArray::number(table ? table->rowCount() : -1) + ")");
    if (table && table->rowCount() > 0) {
        table->selectRow(0);
        QCoreApplication::processEvents();
        const int visible = table->viewport()->height() / qMax(1, table->rowHeight(0));
        CHECK(visible >= 9, QByteArray("the results show most of their rows (") + QByteArray::number(visible) + "; was 4)");
        CHECK(table->horizontalHeader()->defaultAlignment() & Qt::AlignLeft, "headers align left");
        auto *detail = w.findChild<QTextEdit *>();
        CHECK(detail && detail->toPlainText().contains(QLatin1String("45 frames")),
              QByteArray("the detail counts in English (") + (detail ? detail->toPlainText().left(30).toUtf8() : QByteArray()) + ")");
        CHECK(detail && table->height() > 2 * detail->height(), "the results get the larger share of the splitter");
    }
    auto *split = w.findChild<QSplitter *>(QStringLiteral("roundtripSplit"));
    CHECK(split && split->count() == 2, "results and detail share a splitter");
    auto *bar = w.findChild<QProgressBar *>();
    CHECK(bar && !bar->isTextVisible(), "no percentage text on the bar");

    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1366, "fits a laptop");
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
