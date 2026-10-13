#include "testutil.h"

#include "colorrules.h"
#include "configfiles.h"
#include "fieldcatalog.h"
#include "namemap.h"
#include "settings.h"
#include "testassertions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

// =============================================================================
//  Session 271 — colour rules, friendly names, field catalogue and test cases
//  built into the program like kavach.xml; a file the operator chooses, or
//  one beside the program, comes first (configfiles.cpp).
// =============================================================================

namespace {
QByteArray bytesOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}

TEST_SUITE(session271)
{
    // ---- The built-in copies are the repository's files, byte for byte ------
    for (const ConfigFileSpec &spec : configFileSpecs()) {
        const QByteArray builtin = bytesOf(builtinConfigPath(spec.kind));
        const QByteArray repo = bytesOf(QStringLiteral("../") + spec.fileName);   // run from tests/
        CHECK(!builtin.isEmpty(), qPrintable(spec.fileName + QStringLiteral(" is built into the program")));
        CHECK(builtin == repo, qPrintable(spec.fileName + QStringLiteral(": the built-in copy is the repository's file")));
    }

    // ---- Each built-in copy loads with its own loader -----------------------
    {
        ColorRules rules;
        CHECK(rules.loadFromFile(builtinConfigPath(ConfigKind::ColorRules)), "built-in colour rules load");
        NameMap names;
        CHECK(names.loadFromFile(builtinConfigPath(ConfigKind::FriendlyNames)) && names.size() > 0,
              "built-in friendly names load, with names in them");
        QString err;
        FieldCatalog catalog;
        CHECK(catalog.loadFromJson(bytesOf(builtinConfigPath(ConfigKind::FieldMap)), &err) && catalog.fieldCount() > 0,
              "built-in field catalogue loads, with fields in it");
        TestAssertionEngine tests;
        CHECK(tests.load(builtinConfigPath(ConfigKind::TestCases), &err) && tests.assertions().size() > 10,
              "built-in test cases load (the SIF set)");
    }

    // ---- Which copy is in force ----------------------------------------------
    QTemporaryDir beside, elsewhere;
    CHECK(beside.isValid() && elsewhere.isValid(), "two folders");
    const QString builtin = QStringLiteral(":/defaults/color_rules.json");
    const QString name = QStringLiteral("color_rules.json");
    {
        const ConfigChoice c = resolveConfigFor(QString(), beside.path(), name, builtin);
        CHECK(c.source == ConfigSource::BuiltIn && c.path == builtin, "nothing chosen, nothing beside: built in");
    }
    QFile(beside.filePath(name)).open(QIODevice::WriteOnly);
    {
        const ConfigChoice c = resolveConfigFor(QString(), beside.path(), name, builtin);
        CHECK(c.source == ConfigSource::BesideProgram && c.path == beside.filePath(name),
              "a file beside the program comes before the built-in copy (as before)");
    }
    const QString chosen = elsewhere.filePath(QStringLiteral("mine.json"));
    QFile(chosen).open(QIODevice::WriteOnly);
    {
        const ConfigChoice c = resolveConfigFor(chosen, beside.path(), name, builtin);
        CHECK(c.source == ConfigSource::Chosen && c.path == chosen, "a chosen file comes first");
    }
    QFile::remove(chosen);
    {
        const ConfigChoice c = resolveConfigFor(chosen, beside.path(), name, builtin);
        CHECK(c.source == ConfigSource::BesideProgram && c.chosenMissing == chosen,
              "a chosen file that is gone: said, then the next one is used");
    }
    QFile::remove(beside.filePath(name));
    {
        const ConfigChoice c = resolveConfigFor(QString(), beside.path(), QStringLiteral("testcases.json"), QString());
        CHECK(c.source == ConfigSource::None && c.path.isEmpty(),
              "test cases: no built-in copy at start");
    }

    // ---- The choice is remembered in the ini (a package's, via patch 270) ----
    const QByteArray savedPkg = qgetenv("DLCONSOLE_PACKAGE");
    const QString pkg = beside.filePath(QStringLiteral("DLConsole.exe"));
    QFile(pkg).open(QIODevice::WriteOnly);
    qputenv("DLCONSOLE_PACKAGE", pkg.toLocal8Bit());
    {
        CHECK(chosenConfigPath(ConfigKind::FriendlyNames).isEmpty(), "a fresh ini has no choice");
        CHECK(resolveConfig(ConfigKind::FriendlyNames).source == ConfigSource::BuiltIn,
              "so the built-in friendly names are in force");
        const QString mine = elsewhere.filePath(QStringLiteral("names.csv"));
        QFile(mine).open(QIODevice::WriteOnly);
        setChosenConfigPath(ConfigKind::FriendlyNames, mine);
        CHECK(chosenConfigPath(ConfigKind::FriendlyNames) == mine, "the choice is saved");
        CHECK(resolveConfig(ConfigKind::FriendlyNames).path == mine, "and is in force");
        CHECK(resolveConfig(ConfigKind::ColorRules).source == ConfigSource::BuiltIn,
              "the other files keep their own setting");
        setChosenConfigPath(ConfigKind::FriendlyNames, QString());
        CHECK(chosenConfigPath(ConfigKind::FriendlyNames).isEmpty()
                  && resolveConfig(ConfigKind::FriendlyNames).source == ConfigSource::BuiltIn,
              "revert: back to the built-in copy");
        // Test cases: a choice does not load them at start; only beside the program does.
        CHECK(resolveConfig(ConfigKind::TestCases).source == ConfigSource::None,
              "test cases: nothing loaded at start unless beside the program");
        QFile(beside.filePath(QStringLiteral("testcases.json"))).open(QIODevice::WriteOnly);
        CHECK(resolveConfig(ConfigKind::TestCases).source == ConfigSource::BesideProgram,
              "test cases beside the package are loaded at start, as before");
    }
    if (savedPkg.isNull()) qunsetenv("DLCONSOLE_PACKAGE");
    else qputenv("DLCONSOLE_PACKAGE", savedPkg);

    CHECK(describeConfigChoice(resolveConfigFor(QString(), QString(), name, builtin)) == QLatin1String("built in"),
          "described as \"built in\"");
}
