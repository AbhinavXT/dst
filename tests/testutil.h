#ifndef TESTUTIL_H
#define TESTUTIL_H

// =============================================================================
//  Minimal test harness for DLConsole.
//  -----------------------------------------------------------------------------
//  Deliberately not QtTest. QtTest wants one QObject subclass and one
//  QTEST_MAIN per binary, which for a codebase this size means either a
//  dozen executables or a lot of boilerplate. These suites are plain
//  functions in one binary, registered at static-init time, and the whole
//  thing is about eighty lines.
//
//  Suites that need the network or the event loop (the pipeline suite) are
//  marked async: they get the QCoreApplication event loop and report via
//  the same counters, so a single run covers pure logic and live sockets
//  alike.
//
//  Usage:
//      TEST_SUITE(logquery) {
//          CHECK(2 + 2 == 4, "arithmetic still works");
//      }
//
//  Run everything:            ./dltests
//  Run one suite:             ./dltests logquery
//  Exit code is the number of failures, so CI can just check it.
// =============================================================================

#include <QString>
#include <QVector>
#include <cstdio>

namespace TestHarness {

using SuiteFn = void (*)();

struct Suite {
    const char *name;
    SuiteFn     fn;
};

QVector<Suite> &registry();

// Counters are global rather than per-suite so a helper called from several
// suites doesn't need to thread a context through.
extern int checksRun;
extern int checksFailed;
extern const char *currentSuite;

inline void record(bool ok, const char *what, const char *file, int line)
{
    ++checksRun;
    if (ok) return;
    ++checksFailed;
    std::printf("  FAIL [%s] %s\n        at %s:%d\n",
                currentSuite ? currentSuite : "?", what, file, line);
}

struct Registrar {
    Registrar(const char *name, SuiteFn fn) { registry().push_back({ name, fn }); }
};

int runAll(const QString &only);

}  // namespace TestHarness

#define CHECK(cond, msg) \
    ::TestHarness::record((cond), (msg), __FILE__, __LINE__)

#define TEST_SUITE(suiteName)                                            \
    static void suiteName##_body();                                      \
    static ::TestHarness::Registrar suiteName##_registrar(#suiteName,    \
                                                          suiteName##_body); \
    static void suiteName##_body()

#endif // TESTUTIL_H
