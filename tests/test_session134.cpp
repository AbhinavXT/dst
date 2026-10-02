#include "testutil.h"
#include "layoutaudit.h"

#include "flashercore.h"
#include "flasherflashingpage.h"
#include "flasherqueuemodel.h"
#include "flasherqueuepage.h"
#include "flashersummarypage.h"
#include "flasherwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

// =============================================================================
//  Session 134 — UI revamp, tool windows 10: the Firmware Flasher.
//
//  It did not fit a laptop: 1255 x 863 at least, 1350 x 863 while flashing.
//  Now 1090 x 694. The stat cards are one line shorter, the log six lines,
//  the block map's caption short, the mode toggle hidden mid-run, the SHA
//  on a line of its own. The queue: columns measured, size / blocks / CRC
//  under the file name, no empty badge, no detail strip over the last row,
//  pre-flight above the route, no stray pre-flight row at the card's corner.
//  The summary says a one-card failure once. A real run is started against
//  an address nothing answers, then aborted.
// =============================================================================

namespace {

QString writeImage(const QString &dir, const QString &name, int size)
{
    QFile f(QDir(dir).filePath(name));
    if (f.open(QIODevice::WriteOnly)) {
        QByteArray b(size, '\0');
        for (int i = 0; i < size; ++i) b[i] = char((i * 31 + 7) & 0xFF);
        f.write(b);
    }
    return f.fileName();
}

void settle(int ms = 50)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

QToolButton *buttonNamed(QWidget *root, const QString &text)
{
    for (QToolButton *b : root->findChildren<QToolButton *>())
        if (b->text() == text) return b;
    return nullptr;
}

}  // namespace

TEST_SUITE(session134)
{
    // The app's own stylesheet, as the app runs: without it the queue had
    // 4 px to spare, with it the detail strip sat on the last row.
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    QTemporaryDir temp;
    CHECK(temp.isValid(), "fixture: a temporary data directory");
    {
        Flasher::ProfileStore store(QDir(temp.path()).filePath(QStringLiteral("flasher_profiles.json")));
        Flasher::FlashProfile profile;
        profile.name = QStringLiteral("Bench 2");
        profile.vccIp = QStringLiteral("127.0.0.1");
        profile.port = 9;   // discard: nothing answers
        profile.defaultImages.insert(Flasher::CardInput, writeImage(temp.path(), QStringLiteral("KAVACH_Input_Card_v5.appimage"), 40 * 1450));
        profile.defaultImages.insert(Flasher::CardOutput, writeImage(temp.path(), QStringLiteral("KAVACH_Output_Card_v5.appimage"), 25 * 1450));
        profile.defaultImages.insert(Flasher::CardVcc, writeImage(temp.path(), QStringLiteral("LKAVACH_v1.2.9.appimage"), 30 * 1450));
        profile.updaterWaitSeconds = Flasher::kMinUpdaterWaitSeconds;
        store.upsert(profile, store.names().first());
        store.setActiveName(profile.name);
        store.save();
    }

    FlasherWindow w(nullptr, temp.path());
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.queuePage()->selectCard(Flasher::CardInput);
    w.resize(1100, 720);
    w.show();
    settle(200);

    // ---- fits a laptop ---------------------------------------------------------------------
    CHECK(w.minimumSizeHint().height() <= 700 && w.minimumSizeHint().width() <= 1100,
          QByteArray("fits a 1366 x 768 laptop (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
              + QByteArray::number(w.minimumSizeHint().height()) + "; was 1255 x 863)");

    // ---- the queue ------------------------------------------------------------------------------
    auto *table = w.findChild<QTableView *>(QStringLiteral("flasherQueueTable"));
    auto *strip = w.findChild<QFrame *>(QStringLiteral("flasherDetailStrip"));
    CHECK(table && strip, "the queue table and its detail strip");
    if (table && strip) {
        CHECK(table->isColumnHidden(FlasherQueueModel::ColumnSize) && table->isColumnHidden(FlasherQueueModel::ColumnCrc),
              "size and CRC have no columns of their own");
        CHECK(table->columnWidth(FlasherQueueModel::ColumnImage) >= 300,
              QByteArray("the file name gets the width (") + QByteArray::number(table->columnWidth(FlasherQueueModel::ColumnImage))
                  + " px; was ~100)");
        const QAbstractItemModel *m = table->model();
        int inputRow = -1, analogRow = -1;
        for (int r = 0; r < m->rowCount(); ++r) {
            const QString card = m->index(r, FlasherQueueModel::ColumnCard).data().toString();
            if (card.startsWith(QLatin1String("Input"))) inputRow = r;
            if (card.startsWith(QLatin1String("Analog"))) analogRow = r;
        }
        CHECK(inputRow >= 0 && analogRow >= 0, "fixture: the Input and Analog rows");
        if (inputRow >= 0 && analogRow >= 0) {
            const QString second = m->index(inputRow, FlasherQueueModel::ColumnImage).data(FlasherQueueModel::SecondaryTextRole).toString();
            CHECK(second == QStringLiteral("56.6 KB · 40 blocks · CRC ") + m->index(inputRow, FlasherQueueModel::ColumnCrc).data().toString(),
                  QByteArray("size, blocks and CRC under the name (") + second.toUtf8() + ")");
            CHECK(m->index(inputRow, FlasherQueueModel::ColumnCheck).data().toString() == QStringLiteral("Name matches"),
                  "the badge does not repeat the card's name");
            CHECK(m->index(inputRow, FlasherQueueModel::ColumnCheck).data(Qt::ToolTipRole).toString().contains(QLatin1String("Input")),
                  "the tooltip still says which card");
            CHECK(m->index(analogRow, FlasherQueueModel::ColumnCheck).data().toString().isEmpty(),
                  "an empty row has no badge text (the delegate draws no pill for it)");
        }
        const QPoint tableBottom = table->mapTo(&w, QPoint(0, table->height()));
        const QPoint stripTop = strip->mapTo(&w, QPoint(0, 0));
        CHECK(stripTop.y() >= tableBottom.y() - 1,
              QByteArray("the detail strip starts below the table, not over its last row (")
                  + QByteArray::number(stripTop.y()) + " vs " + QByteArray::number(tableBottom.y()) + ")");
        CHECK(table->viewport()->height() >= 4 * table->rowHeight(0), "all four cards are in view");
    }
    auto *blocker = w.findChild<QLabel *>(QStringLiteral("flasherBlocker"));
    CHECK(blocker && !blocker->isVisible(), "ready to flash: the empty blocker label takes no room");

    auto *preflight = w.findChild<QFrame *>(QStringLiteral("flasherPreflightCard"));
    auto *route = w.findChild<QFrame *>(QStringLiteral("flasherRouteCard"));
    CHECK(preflight && route && preflight->y() < route->y(), "pre-flight comes before the route diagram");

    // Several refreshes, then nothing stray: old pre-flight rows are hidden.
    if (auto *ip = w.findChild<QLineEdit *>()) {
        ip->setText(QStringLiteral("127.0.0.2"));
        ip->setText(QStringLiteral("127.0.0.1"));
    }
    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no stray widget after pre-flight refreshes (")
                               + QByteArray::number(loose.size()) + ": " + loose.join(QLatin1String(", ")).left(120).toUtf8() + ")");

    // ---- flashing ---------------------------------------------------------------------------------
    QToolButton *engineer = buttonNamed(&w, QStringLiteral("Engineer"));
    CHECK(engineer && engineer->isVisible(), "the mode toggle shows on the queue");
    CHECK(w.startBatch(false), "a run starts");
    settle(800);
    CHECK(w.isFlashing() && w.currentPage() == FlasherWindow::FlashingPageIndex, "on the flashing page");
    CHECK(engineer && !engineer->isVisible() && !engineer->isEnabled(), "the mode toggle is hidden (and disabled) mid-run");
    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700,
          QByteArray("still fits while flashing (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
              + QByteArray::number(w.minimumSizeHint().height()) + "; was 1350 x 863)");
    CHECK(w.flashingPage()->waitingForUpdater(), "the power-cycle prompt is up");
    if (auto *file = w.findChild<QLabel *>(QStringLiteral("flasherBatchFile"))) {
        CHECK(file->fontMetrics().horizontalAdvance(file->text()) <= file->width(),
              QByteArray("the card's file name is elided to its own width, not clipped (") + file->text().toUtf8() + ")");
    } else {
        CHECK(false, "the card row's file label");
    }
    {
        QEventLoop loop;
        bool done = false;
        QObject::connect(&w, &FlasherWindow::batchFinished, &loop, [&]() { done = true; loop.quit(); });
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        w.abortCurrent();
        if (!done) loop.exec();
        CHECK(done && w.currentPage() == FlasherWindow::SummaryPageIndex, "aborted: the run lands on the summary");
    }
    CHECK(engineer && engineer->isVisible(), "the mode toggle is back");

    // ---- the summary says it once -------------------------------------------------------------------
    {
        const Flasher::BatchEntry entry = w.plan().entries().value(0);
        const QString why = Flasher::failureExplanation(entry.result);
        int times = 0;
        for (QLabel *l : w.summaryPage()->findChildren<QLabel *>())
            if (l->isVisible() && l->text() == why) ++times;
        CHECK(!why.isEmpty() && times == 1, QByteArray("a one-card failure's explanation shows once (")
                                                + QByteArray::number(times) + ")");
    }
    const QStringList looseAfter = LayoutAudit::orphans(&w);
    CHECK(looseAfter.isEmpty(), "nothing stray on the summary either");
}
