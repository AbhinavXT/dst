#include "testutil.h"

#include "settingsbundle.h"
#include "workspacesnapshot.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>

// =============================================================================
//  Settings export / import (session 79). Two "machines", each a temp folder
//  with its own INI and JSON stores; settings go from one to the other.
// =============================================================================

namespace {

void writeJson(const QString &path, const QJsonObject &o)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(o).toJson());
}

QJsonObject readJson(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return QJsonDocument::fromJson(f.readAll()).object();
}

QJsonObject flasherFile(const QStringList &names)
{
    QJsonArray profiles;
    for (const QString &n : names) {
        QJsonObject p;
        p[QStringLiteral("name")] = n;
        profiles.append(p);
    }
    QJsonObject o;
    o[QStringLiteral("profiles")] = profiles;
    o[QStringLiteral("active")]   = names.value(0);
    return o;
}

QJsonObject locoFile(const QStringList &names)
{
    QJsonArray configs;
    for (const QString &n : names) {
        QJsonObject c;
        c[QStringLiteral("name")] = n;
        configs.append(c);
    }
    QJsonObject o;
    o[QStringLiteral("format")]  = 1;
    o[QStringLiteral("active")]  = names.value(0);
    o[QStringLiteral("configs")] = configs;
    return o;
}

}  // namespace

using namespace SettingsBundle;

TEST_SUITE(settingsbundle)
{
    QTemporaryDir from, to;
    const QString iniA = from.filePath(QStringLiteral("dlconsole.ini"));
    const QString iniB = to.filePath(QStringLiteral("dlconsole.ini"));

    // ---- machine A: a set-up console ----------------------------------------
    {
        QSettings a(iniA, QSettings::IniFormat);
        a.setValue(QStringLiteral("ui/theme"), QStringLiteral("nord"));
        a.setValue(QStringLiteral("ui/colorBlindSafe"), true);
        a.setValue(QStringLiteral("ui/textZoom"), 125);
        a.setValue(QStringLiteral("ui/tabTags/21_1"), QStringLiteral("5|bench"));
        a.setValue(QStringLiteral("ui/tabTags/81_1"), QStringLiteral("0|"));
        a.setValue(QStringLiteral("ui/statusPins"), QStringList({ "21_1\x1f" "lsrp|TRAIN_SPEED|0|Speed", "x" }));
        a.setValue(QStringLiteral("lococonsole/bigNumbers"), QStringList());   // deliberately none
        a.setValue(QStringLiteral("network/udp_port"), 50099);                 // machine-specific
        a.sync();
        WindowLayoutStore layouts(QDir(from.path()).filePath(QStringLiteral("window_layouts.json")));
        WorkspaceSnapshot snap;
        snap.tabs = { { QStringLiteral("21_1"), QStringLiteral("L1"), true } };
        layouts.put(QStringLiteral("Bench"), snap);
        layouts.save();
        writeJson(QDir(from.path()).filePath(QStringLiteral("flasher_profiles.json")), flasherFile({ "Lab", "Shed" }));
        writeJson(QDir(from.path()).filePath(QStringLiteral("loco_configs.json")), locoFile({ "Loco 7" }));
    }

    // ---- export ---------------------------------------------------------------
    const Bundle exported = capture(allSections(), from.path(), iniA);
    CHECK(exported.present() == allSections(), "every section is captured from a set-up console");
    CHECK(exported.summary(Section::Appearance).contains(QLatin1String("nord"))
              && exported.summary(Section::Appearance).contains(QLatin1String("125")),
          "the appearance summary names the theme and text size");
    CHECK(exported.summary(Section::Tags) == QLatin1String("2 tagged tabs"), "tags are counted");
    CHECK(exported.summary(Section::FlasherProfiles) == QLatin1String("2 profiles: Lab, Shed"), "profiles are named");
    CHECK(exported.summary(Section::Pins).startsWith(QLatin1String("2 status-bar, 0 big numbers")),
          "an empty big-number list is counted as none, not as the defaults");
    const QString file = from.filePath(QStringLiteral("settings.dlsettings.json"));
    CHECK(writeFile(file, exported), "the bundle writes");
    CHECK(!QString::fromUtf8(QJsonDocument(exported.toJson()).toJson()).contains(QLatin1String("udp_port")),
          "machine-specific settings are not carried");

    Bundle read;
    CHECK(readFile(file, &read) && read.present() == allSections(), "and reads back");

    // ---- machine B: blank, one thing of its own --------------------------------
    {
        QSettings b(iniB, QSettings::IniFormat);
        b.setValue(QStringLiteral("ui/tabTags/stale"), QStringLiteral("2|old"));
        b.setValue(QStringLiteral("network/udp_port"), 50002);
        b.sync();
        writeJson(QDir(to.path()).filePath(QStringLiteral("flasher_profiles.json")), flasherFile({ "Mine" }));
    }
    const QList<Section> chosen{ Section::Appearance, Section::Tags, Section::Pins, Section::FlasherProfiles };
    const Bundle backup = capture(chosen, to.path(), iniB);
    CHECK(apply(read, chosen, to.path(), iniB), "the chosen sections import");
    {
        QSettings b(iniB, QSettings::IniFormat);
        CHECK(b.value(QStringLiteral("ui/theme")).toString() == QLatin1String("nord")
                  && b.value(QStringLiteral("ui/colorBlindSafe")).toBool()
                  && b.value(QStringLiteral("ui/textZoom")).toInt() == 125,
              "appearance arrives");
        CHECK(b.value(QStringLiteral("ui/tabTags/21_1")).toString() == QLatin1String("5|bench")
                  && !b.contains(QStringLiteral("ui/tabTags/stale")),
              "tags replace the old set whole, not merged");
        CHECK(b.value(QStringLiteral("ui/statusPins")).toStringList().size() == 2, "status pins arrive");
        CHECK(b.contains(QStringLiteral("lococonsole/bigNumbers"))
                  && b.value(QStringLiteral("lococonsole/bigNumbers")).toStringList().isEmpty(),
              "an empty tile list stays empty (not reset to the defaults)");
        CHECK(b.value(QStringLiteral("network/udp_port")).toInt() == 50002, "the port is untouched");
    }
    CHECK(readJson(QDir(to.path()).filePath(QStringLiteral("flasher_profiles.json")))
              .value(QStringLiteral("profiles")).toArray().size() == 2,
          "flasher profiles are replaced");
    CHECK(!QFile::exists(QDir(to.path()).filePath(QStringLiteral("loco_configs.json"))),
          "an unticked section is not imported");

    // ---- undo -------------------------------------------------------------------
    CHECK(restore(backup, chosen, to.path(), iniB), "the backup taken before the import restores");
    {
        QSettings b(iniB, QSettings::IniFormat);
        CHECK(!b.contains(QStringLiteral("ui/theme")) && b.contains(QStringLiteral("ui/tabTags/stale"))
                  && !b.contains(QStringLiteral("ui/tabTags/21_1")),
              "INI sections are back exactly as they were");
    }
    CHECK(readJson(QDir(to.path()).filePath(QStringLiteral("flasher_profiles.json")))
              .value(QStringLiteral("profiles")).toArray().size() == 1,
          "so is the flasher file");
    apply(read, { Section::LocoConfigs }, to.path(), iniB);
    const Bundle noLoco;   // captured when there was no file: section absent
    CHECK(restore(noLoco, { Section::LocoConfigs }, to.path(), iniB)
              && !QFile::exists(QDir(to.path()).filePath(QStringLiteral("loco_configs.json"))),
          "undo of a file that did not exist before removes it");

    // ---- refusals ----------------------------------------------------------------
    {
        Bundle bad = read;
        QJsonObject section;
        section[QStringLiteral("file")] = flasherFile({});
        bad.sections[id(Section::FlasherProfiles)] = section;
        QString why;
        QSettings b(iniB, QSettings::IniFormat);
        const QString themeBefore = b.value(QStringLiteral("ui/theme")).toString();
        CHECK(!apply(bad, { Section::Appearance, Section::FlasherProfiles }, to.path(), iniB, &why),
              "a flasher section with no profiles is refused");
        CHECK(why.contains(QLatin1String("Nothing was imported")), "and says nothing was imported");
        b.sync();
        CHECK(b.value(QStringLiteral("ui/theme")).toString() == themeBefore,
              "including the section that was fine: all or nothing");

        Bundle wrongLoco = read;
        QJsonObject lo;
        QJsonObject content = locoFile({ "x" });
        content[QStringLiteral("format")] = 7;
        lo[QStringLiteral("file")] = content;
        wrongLoco.sections[id(Section::LocoConfigs)] = lo;
        CHECK(!apply(wrongLoco, { Section::LocoConfigs }, to.path(), iniB, &why), "an unknown loco_configs format is refused");

        Bundle sneaky = read;
        QJsonObject tags;
        QJsonObject ini;
        ini[QStringLiteral("network/udp_port")] = QStringLiteral("1");
        ini[QStringLiteral("ui/tabTags/ok")] = QStringLiteral("1|fine");
        tags[QStringLiteral("ini")] = ini;
        sneaky.sections[id(Section::Tags)] = tags;
        apply(sneaky, { Section::Tags }, to.path(), iniB);
        QSettings c(iniB, QSettings::IniFormat);
        CHECK(c.value(QStringLiteral("network/udp_port")).toInt() == 50002 && c.contains(QStringLiteral("ui/tabTags/ok")),
              "a section can only set the keys it owns");

        CHECK(!apply(Bundle(), { Section::Tags }, to.path(), iniB, &why), "a section the file lacks is an error");

        QFile other(to.filePath(QStringLiteral("other.json")));
        other.open(QIODevice::WriteOnly);
        other.write("{\"format\":\"dlconsole-layouts\",\"version\":1}");
        other.close();
        Bundle none;
        CHECK(!readFile(other.fileName(), &none, &why) && why.contains(QLatin1String("not a DLConsole settings")),
              "another DLConsole file is not mistaken for a settings bundle");
    }
}
