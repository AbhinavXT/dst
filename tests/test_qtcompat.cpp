#include "testutil.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include <algorithm>

// Qt 5 / Qt 6 source compatibility.
//
// The project builds on Qt 5.15 here and Qt 6.9 on the deployment machine.
// The breaks between them are not warnings — they are hard compile errors
// that only appear on one of the two, so a change can look completely fine
// locally and fail on the other toolchain. These checks pin the patterns
// that have already bitten, so a reintroduction fails a test rather than
// somebody's build.
TEST_SUITE(qtcompat)
{
    // ---- container sizes -------------------------------------------------
    // QByteArray::size() is int on Qt 5 and qsizetype on Qt 6, so
    // std::min(int, bytes.size()) cannot deduce a common type on Qt 6.
    // Casting both sides is the portable form.
    {
        QByteArray b(40, 'x');
        constexpr int kPerLine = 16;
        const int off = 32;
        const int n = std::min(kPerLine, int(b.size() - off));
        CHECK(n == 8, "min() over a container size compiles and is correct");
        const int full = std::min(kPerLine, int(b.size()));
        CHECK(full == 16, "and clamps to the line width");
    }

    // ---- string sizes ----------------------------------------------------
    {
        QString s(20, QLatin1Char('y'));
        const int half = int(s.size()) / 2;
        CHECK(half == 10, "QString::size() casts cleanly on both versions");
    }

    // ---- timezone-aware construction -------------------------------------
    // fromMSecsSinceEpoch(qint64, Qt::TimeSpec) is deprecated on Qt 6 but
    // still present; the UTC path the log writer depends on must give the
    // same answer on both.
    {
        const qint64 ms = QDateTime(QDate(2026, 8, 19), QTime(12, 0, 0),
                                    Qt::UTC).toMSecsSinceEpoch();
        const QDateTime utc = QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
        CHECK(utc.toString(QStringLiteral("HH:mm:ss")) == "12:00:00",
              "UTC round-trip is identical across Qt versions");
        CHECK(utc.timeSpec() == Qt::UTC, "and keeps its spec");
    }

    // ---- the Qt version actually in use ----------------------------------
    // Not an assertion so much as a record: when a suite run is pasted into
    // a bug report, this says which toolchain produced it.
    printf("      building against Qt %s\n", qVersion());
    CHECK(QT_VERSION >= QT_VERSION_CHECK(5, 15, 0),
          "Qt 5.15 or newer, as the .pro requires");
}
