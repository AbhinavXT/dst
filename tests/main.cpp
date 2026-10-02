#include "testutil.h"

#include <QApplication>
#include <QElapsedTimer>
#include <cstdio>

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
    // Offscreen unless told otherwise (session 113). verify.sh always ran
    // the suites offscreen, but one suite run by hand — the way CLAUDE.md
    // says to — used the real display, where the pixel-reading suites
    // (tabmetrics, flasher, presentationmode) fail regardless. A run by
    // hand then disagreed with the gate, and was once read as evidence
    // about it. Set QT_QPA_PLATFORM explicitly to watch on a real screen.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // whole logs, even after a crash (session 116)
    QApplication app(argc, argv);

    const QString only = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QString();
    const int failures = TestHarness::runAll(only);

    // Flushed before exit: on Windows a piped stdout was cut off mid-line,
    // losing the summary and the last FAIL lines from the CI log.
    std::fflush(stdout);
    // Exit code is the failure count, so CI needs no output parsing.
    return failures > 255 ? 255 : failures;
}
