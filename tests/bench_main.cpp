#include "findbar.h"
#include "logmodel.h"
#include "logentry.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QTableView>
#include <QTest>
#include <QDebug>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    LogModel model;
    QVector<LogEntryPtr> v;
    const int N = 100000;
    v.reserve(N);
    for (int i = 0; i < N; ++i) {
        auto e = LogEntryPtr::create();
        e->epochMs = 1000 + i;
        e->text = QStringLiteral("@lsrp_21_1 STN_ID %1 FRAME_NUM %2 AA BB CC")
                      .arg(i % 97).arg(i);

        v.append(e);
    }
    model.appendEntries(v);

    QTableView view;
    view.setModel(&model);
    FindBar bar(&view);
    bar.show();
    QTest::qWait(30);

    const QStringList typed{ "F", "FR", "FRA", "FRAM", "FRAME_NUM 993" };
    QElapsedTimer t;
    qint64 total = 0;
    for (const QString &s : typed) {
        bar.setSearchText(s);
        QTest::qWait(250);                    // let the debounce settle
        t.start();
        QMetaObject::invokeMethod(&bar, "rebuildMatches", Qt::DirectConnection);
        const qint64 el = t.elapsed();
        total += el;
        qInfo("  typed %-10s -> %6d matches, scan %4lld ms",
              qPrintable(s), bar.matchCount(), el);
    }
    qInfo("TOTAL %lld ms of scanning for %d keystrokes over %d rows",
          total, int(typed.size()), N);

    // Correctness: a narrowed result must equal what a full scan finds.
    // Retyping the same pattern from scratch forces the full path, because
    // an empty previous pattern cannot be narrowed from.
    const QVector<int> narrowed = bar.matchRows();
    bar.setSearchText("");
    QTest::qWait(250);
    QMetaObject::invokeMethod(&bar, "rebuildMatches", Qt::DirectConnection);
    bar.setSearchText(typed.last());
    QTest::qWait(250);
    QMetaObject::invokeMethod(&bar, "rebuildMatches", Qt::DirectConnection);
    const QVector<int> full = bar.matchRows();
    qInfo("CORRECTNESS narrowed=%d full=%d identical=%s",
          narrowed.size(), full.size(),
          narrowed == full ? "YES" : "NO !!!");
    return 0;
}
