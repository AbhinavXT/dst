#include "testutil.h"

#include <QApplication>
#include <QElapsedTimer>

namespace TestHarness {

int checksRun    = 0;
int checksFailed = 0;
const char *currentSuite = nullptr;

QVector<Suite> &registry()
{
    // Function-local static: the registry has to exist before any
    // Registrar constructor runs, and translation-unit initialisation
    // order across the suite files is unspecified.
    static QVector<Suite> reg;
    return reg;
}

int runAll(const QString &only)
{
    QElapsedTimer timer;
    timer.start();

    int suitesRun = 0;
    for (const Suite &s : registry()) {
        if (!only.isEmpty() && only != QLatin1String(s.name)) continue;

        currentSuite = s.name;
        const int before       = checksRun;
        const int beforeFailed = checksFailed;

        s.fn();

        const int ran    = checksRun - before;
        const int failed = checksFailed - beforeFailed;
        ++suitesRun;

        std::printf("%-14s %4d checks  %s\n", s.name, ran,
                    failed == 0 ? "ok" : "FAILED");
    }
    currentSuite = nullptr;

    if (suitesRun == 0) {
        std::printf("No suite matched '%s'. Available:\n", qPrintable(only));
        for (const Suite &s : registry()) std::printf("  %s\n", s.name);
        return 1;
    }

    std::printf("\n%d suite(s), %d checks, %d failed  (%lld ms)\n",
                suitesRun, checksRun, checksFailed,
                static_cast<long long>(timer.elapsed()));
    return checksFailed;
}

}  // namespace TestHarness

int main(int argc, char **argv)
{
    // QApplication, not QCoreApplication: the layout suite constructs real
    // QWidgets, and Qt aborts outright if a widget is created without one.
    // Run headless with QT_QPA_PLATFORM=offscreen on a machine with no
    // display — the suites never show anything.
    QApplication app(argc, argv);

    const QString only = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QString();
    const int failures = TestHarness::runAll(only);

    // Exit code is the failure count, so CI needs no output parsing.
    return failures > 255 ? 255 : failures;
}
