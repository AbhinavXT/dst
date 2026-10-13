#ifndef CONFIGFILES_H
#define CONFIGFILES_H

#include <QString>
#include <QVector>

// =============================================================================
//  Session 271 — the configuration files, built in like kavach.xml.
//
//  color_rules.json, friendly_names.csv, fieldmap.json and testcases.json are
//  compiled into the program (defaults.qrc, ":/defaults/<name>"), so a single
//  .exe / AppImage (patch 269) has them with nothing beside it. Which copy is
//  in force, first match wins:
//    1. the file the operator chose (Tools > Configuration files, remembered
//       in the ini under config/<key>), if it still exists;
//    2. a file of that name beside the program (programDir()), as before;
//    3. the built-in copy.
//  Test cases are the exception: the built-in set is loaded only on request
//  (Tools > Test cases > Load built-in test cases), because loaded test
//  cases post notifications all through a live session; and another set is
//  loaded with Tools > Test cases > Load test cases..., for that session, as
//  before. So for TestCases only step 2 applies at start.
// =============================================================================

enum class ConfigKind { ColorRules, FriendlyNames, FieldMap, TestCases };

enum class ConfigSource { Chosen, BesideProgram, BuiltIn, None };

struct ConfigFileSpec {
    ConfigKind kind;
    QString    key;        // ini key under config/, e.g. "color_rules"
    QString    fileName;   // e.g. "color_rules.json"
    QString    title;      // for menus and messages, e.g. "colour rules"
};

struct ConfigChoice {
    QString      path;           // empty only for ConfigSource::None
    ConfigSource source = ConfigSource::None;
    QString      chosenMissing;  // a chosen file that no longer exists (said, then skipped)
};

const QVector<ConfigFileSpec> &configFileSpecs();
const ConfigFileSpec &configFileSpec(ConfigKind kind);
QString builtinConfigPath(ConfigKind kind);   // ":/defaults/<fileName>"

// The rule itself, without the ini or the environment (tests).
ConfigChoice resolveConfigFor(const QString &chosen, const QString &besideDir,
                              const QString &fileName, const QString &builtin);

// The rule with the ini's choice and programDir(). For TestCases neither a
// choice nor the built-in copy applies at start (source None unless a file
// is beside the program).
ConfigChoice resolveConfig(ConfigKind kind);

QString chosenConfigPath(ConfigKind kind);
void    setChosenConfigPath(ConfigKind kind, const QString &path);   // "" = no choice

// "built in", "beside the program: <path>", "chosen: <path>", "none".
QString describeConfigChoice(const ConfigChoice &c);

#endif // CONFIGFILES_H
