# Build speed: why changes take long, and what to do

Measured on 2026-10-13 at patch 267.

## 1. Windows PC (Qt 6): the whole project rebuilds every time

Nothing in the project forces a full rebuild. The `.pro` / `.pri` files have
no `system()` calls, no files generated on every build, and no `__DATE__` /
`__TIME__`. On the Mac, editing one `.cpp` recompiles only that file (4 s).
If Windows rebuilds everything each time, the cause is almost certainly on the
Windows machine. Most likely first:

### Cause 1: how the code reaches the PC

If you copy the folder from the Mac by zip, USB, OneDrive or AirDrop, every
file gets a new timestamp. The build then sees all 167 sources as changed and
recompiles them all.

A worse case: if the copied files carry timestamps **later than the Windows
clock** (zip stores times without a time zone), the build recompiles them
**every time, even when nothing changed**.

**Fix:** clone the repo once on Windows and use `git pull` after that. Git only
touches files that actually changed.

### Cause 2: the folder is on OneDrive

On Windows, Desktop and Documents are often synced to OneDrive. The sync keeps
touching files and locking build output.

**Fix:** keep the source and the build folder outside OneDrive, for example
`C:\dev\DLConsole`.

### Cause 3: Windows Defender scans every file the compiler writes

This can make builds 2–3× slower.

**Fix:** add Defender exclusions for the source folder, the build folder and
`C:\Qt`.

### Cause 4: building on a single core

In Qt Creator, go to **Projects → Build → Make** step:

- With **MSVC**, it should use `jom`, not `nmake` (`nmake` builds one file at a
  time).
- With **MinGW**, it should pass `-j8` or similar.

### Quick test to find which one it is

Build in Qt Creator, then press **Build** again straight away without changing
anything.

| Second build… | Meaning | Look at |
|---|---|---|
| compiles files again | timestamp problem | Causes 1, 2: check whether file dates are in the future |
| finishes at once | builds are incremental; the compiler itself is slow | Causes 3, 4 |

### Possible code-side help

A **precompiled header** for the Qt includes. MSVC re-reads the Qt 6 headers
for every one of the 167 files, so this often makes full Windows builds 2–3×
faster. This would be its own patch, and CI (Windows MSVC) would check it.

## 2. Mac: the full gate is the slow part, not the compile

| What | Time |
|---|---|
| Qt Creator build after editing one `.cpp` | **4 s** |
| … after editing a widely used header (`logmodel.h`) | 29 s |
| `./verify.sh` after editing one `.cpp` | **327 s** |
| … of which the unit suite (308 suites, run one after another) | **191 s** |
| … of which the 3 slowest suites (`session161rules`, `session156`, `flasherrun`) | 47 s |
| … of which the 25 suites over 2 s each | 123 s |

What would help, biggest gain first:

1. **Use a short loop while working; run the full gate only before committing.**
   Build in Qt Creator, then run only the suite you're working on:
   `cd tests && ../build-verify/tests/dltests session267`.
   Run `./verify.sh` (both Qt 5 and Qt 6) once, before tagging a patch.
   This needs no code change.
2. **Run the Qt 5 and Qt 6 gates at the same time.** They use separate build
   folders, and the test run uses only one of the 11 cores. This saves about
   5 minutes per patch.
3. **Run the unit suite in parallel.** Add a `--shard i/n` option to
   `tests/main.cpp` and have `verify.sh` start 4–6 test processes. 191 s should
   drop to about 40–50 s. The risk is suites that share temp files or
   QSettings; anything that breaks shows us hidden coupling.
4. **Speed up the slowest suites.** `session156` uses real `msleep` and polling
   (`tests/test_session156.cpp:184`, `:199`). The flasher and rules suites
   probably wait in real time too. Shorter timeouts or an injected clock could
   save about 40 s.
5. **Smaller wins:** `ccache` (helps clean builds and Qt 5/Qt 6 switches) and a
   precompiled header. `uicolors.h` is included by 88 files and `logmodel.h` by
   53 sources plus 92 tests, so editing those triggers a large rebuild.
