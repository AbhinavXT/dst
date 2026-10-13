#include "configfiles.h"

#include "programdir.h"
#include "settings.h"

#include <QDir>
#include <QFileInfo>

const QVector<ConfigFileSpec> &configFileSpecs()
{
    static const QVector<ConfigFileSpec> specs = {
        { ConfigKind::ColorRules,    QStringLiteral("color_rules"),    QStringLiteral("color_rules.json"),
          QStringLiteral("colour rules") },
        { ConfigKind::FriendlyNames, QStringLiteral("friendly_names"), QStringLiteral("friendly_names.csv"),
          QStringLiteral("friendly names") },
        { ConfigKind::FieldMap,      QStringLiteral("fieldmap"),       QStringLiteral("fieldmap.json"),
          QStringLiteral("field catalogue") },
        { ConfigKind::TestCases,     QStringLiteral("testcases"),      QStringLiteral("testcases.json"),
          QStringLiteral("test cases") },
    };
    return specs;
}

const ConfigFileSpec &configFileSpec(ConfigKind kind)
{
    for (const ConfigFileSpec &s : configFileSpecs())
        if (s.kind == kind) return s;
    return configFileSpecs().first();   // unreachable: every kind is listed
}

QString builtinConfigPath(ConfigKind kind)
{
    return QStringLiteral(":/defaults/") + configFileSpec(kind).fileName;
}

ConfigChoice resolveConfigFor(const QString &chosen, const QString &besideDir,
                              const QString &fileName, const QString &builtin)
{
    ConfigChoice c;
    if (!chosen.isEmpty()) {
        if (QFileInfo(chosen).isFile()) {
            c.path = chosen;
            c.source = ConfigSource::Chosen;
            return c;
        }
        c.chosenMissing = chosen;
    }
    const QString beside = QDir(besideDir).filePath(fileName);
    if (!besideDir.isEmpty() && QFileInfo(beside).isFile()) {
        c.path = beside;
        c.source = ConfigSource::BesideProgram;
        return c;
    }
    if (!builtin.isEmpty()) {
        c.path = builtin;
        c.source = ConfigSource::BuiltIn;
    }
    return c;
}

ConfigChoice resolveConfig(ConfigKind kind)
{
    if (kind == ConfigKind::TestCases)
        return resolveConfigFor(QString(), programDir(), configFileSpec(kind).fileName, QString());
    return resolveConfigFor(chosenConfigPath(kind), programDir(), configFileSpec(kind).fileName,
                            builtinConfigPath(kind));
}

QString chosenConfigPath(ConfigKind kind)
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    return s.value(QStringLiteral("config/") + configFileSpec(kind).key).toString();
}

void setChosenConfigPath(ConfigKind kind, const QString &path)
{
    QSettings s(Settings::iniPath(), QSettings::IniFormat);
    const QString key = QStringLiteral("config/") + configFileSpec(kind).key;
    if (path.isEmpty()) s.remove(key);
    else s.setValue(key, path);
}

QString describeConfigChoice(const ConfigChoice &c)
{
    switch (c.source) {
    case ConfigSource::Chosen:        return QStringLiteral("chosen: %1").arg(QDir::toNativeSeparators(c.path));
    case ConfigSource::BesideProgram: return QStringLiteral("beside the program: %1").arg(QDir::toNativeSeparators(c.path));
    case ConfigSource::BuiltIn:       return QStringLiteral("built in");
    case ConfigSource::None:          break;
    }
    return QStringLiteral("none");
}
