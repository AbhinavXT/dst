#ifndef PROGRAMDIR_H
#define PROGRAMDIR_H

#include <QString>

// =============================================================================
//  Session 269 — the folder the operator sees the program in.
//
//  Files "beside the program" (color_rules.json, friendly_names.csv,
//  fieldmap.json, testcases.json) are looked for here, and file dialogs start
//  here. Usually that is QCoreApplication::applicationDirPath(), but not for
//  the single-file packages (packaging/), which run the program from a
//  temporary folder:
//   - Windows single .exe: its unpacker sets DLCONSOLE_PACKAGE to the .exe the
//     operator started (packaging/windows/make_single_exe.ps1);
//   - Linux AppImage: the AppImage runtime sets APPIMAGE (the .AppImage file)
//     and APPDIR (where it is mounted). APPIMAGE counts only while the program
//     runs from inside APPDIR: another AppImage's variables, inherited by a
//     DLConsole started from it, do not.
// =============================================================================

QString programDir();

// The rule itself, without the environment (tests).
QString programDirFor(const QString &appDirPath, const QString &windowsPackage,
                      const QString &appImage, const QString &appImageMount);

#endif // PROGRAMDIR_H
