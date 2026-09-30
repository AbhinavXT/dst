#include "testutil.h"

#include "flashercore.h"
#include "flasherwindow.h"
#include "lococonfigcore.h"
#include "lococonfigwindow.h"
#include "profileio.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

// =============================================================================
//  Session 94: export / import of Loco Configuration configurations (values +
//  send targets) and Firmware Flasher profiles, to move them between PCs.
// =============================================================================

namespace {

QByteArray read94(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void write94(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(bytes);
}

}  // namespace

TEST_SUITE(session94)
{
    // ---- the naming rule ------------------------------------------------------------------
    {
        QStringList taken{ QStringLiteral("Loco 7"), QStringLiteral("Loco 7 (2)") };
        auto isTaken = [&](const QString &n) { return taken.contains(n); };
        CHECK(uniqueImportName(QStringLiteral("Loco 9"), isTaken) == QLatin1String("Loco 9"), "a free name is kept");
        CHECK(uniqueImportName(QStringLiteral("Loco 7"), isTaken) == QLatin1String("Loco 7 (3)"),
              "a used one becomes the first free 'Name (n)'");
    }

    // ---- Loco Configuration, through two windows (two PCs) ------------------------------------
    {
        QTemporaryDir pcA, pcB, share;
        const QString file = share.filePath(QStringLiteral("locos.dlloco"));
        LocoConfigWindow a(nullptr, pcA.path());
        CHECK(a.isUsable(), "PC A: the loco configuration window opens");
        if (!a.isUsable()) return;

        // Two configurations on PC A, with their own targets.
        LocoInfo::LocoConfig base = a.configNamed(a.configNames().first());
        QTemporaryDir scratch;
        {
            LocoInfo::Layout layout;
            QString err;
            CHECK(layout.load(QStringLiteral(":/schema/kavach.xml"), &err), "the LOCO_INFO layout loads");
            LocoInfo::LocoConfig l7 = base, l9 = base;
            l7.name = QStringLiteral("Loco 30007");
            l7.targets[0].ip = QStringLiteral("10.0.7.1"); l7.targets[0].port = 50011;
            l7.targets[1].ip = QStringLiteral("10.0.7.2");
            l7.lastSentBody = QByteArray("sent-on-A");
            l7.lastSentTarget = QStringLiteral("10.0.7.1:50011");
            l9.name = QStringLiteral("Loco 30009");
            l9.targets[0].ip = QStringLiteral("10.0.9.1");
            // The file format itself.
            const QByteArray bytes = LocoInfo::exportConfigs(layout, { l7, l9 });
            const QJsonObject root = QJsonDocument::fromJson(bytes).object();
            CHECK(root.value(QStringLiteral("format")).toString() == QLatin1String("dlconsole-loco-configs")
                      && root.value(QStringLiteral("version")).toInt() == 1,
                  "the file names its format and version");
            CHECK(!bytes.contains("last_sent") && !bytes.contains("sent-on-A"),
                  "what was last sent is PC A's history and is not exported");
            const LocoInfo::ImportedConfigs in = LocoInfo::importConfigs(layout, LocoInfo::Values(), bytes);
            CHECK(in.ok && in.configs.size() == 2 && in.configs[0].targets[0].ip == QLatin1String("10.0.7.1")
                      && in.configs[0].targets[0].port == 50011 && in.configs[0].targets[1].ip == QLatin1String("10.0.7.2")
                      && in.configs[0].values == l7.values,
                  "read back: names, all targets (IP and port) and every value");
            CHECK(!LocoInfo::importConfigs(layout, {}, Flasher::exportProfiles({ Flasher::FlashProfile() })).ok,
                  "a flasher-profile file is refused, by name");
            CHECK(LocoInfo::importConfigs(layout, {}, Flasher::exportProfiles({ Flasher::FlashProfile() })).error.contains(QLatin1String("dlconsole-flasher-profiles")),
                  "and the error says what it is");
            CHECK(!LocoInfo::importConfigs(layout, {}, QByteArray("{ not json")).ok, "broken JSON is refused");
            QJsonObject newer = root; newer.insert(QStringLiteral("version"), 2);
            CHECK(!LocoInfo::importConfigs(layout, {}, QJsonDocument(newer).toJson()).ok, "a file from a newer DLConsole is refused");
            QJsonObject bare = root; bare.remove(QStringLiteral("format")); bare.remove(QStringLiteral("version"));
            CHECK(LocoInfo::importConfigs(layout, {}, QJsonDocument(bare).toJson()).ok,
                  "a plain copy of loco_configs.json is accepted too");
            write94(scratch.filePath(QStringLiteral("two.dlloco")), bytes);
        }
        // Get them into PC A's store by importing, then export from the window.
        QString err;
        QStringList got = a.importConfigsFrom(scratch.filePath(QStringLiteral("two.dlloco")),
                                              [](const QString &, bool *) { return ImportClash::KeepBoth; }, &err);
        CHECK(got.size() == 2 && err.isEmpty(), "PC A: importing into an empty set stores both under their own names");
        CHECK(a.exportConfigsTo(file, { QStringLiteral("Loco 30007"), QStringLiteral("Loco 30009") }, &err),
              QByteArray("PC A: export two configurations: ") + err.toUtf8());
        CHECK(!a.exportConfigsTo(file, {}, &err), "exporting nothing is refused");

        LocoConfigWindow b(nullptr, pcB.path());
        const int before = b.configNames().size();
        got = b.importConfigsFrom(file, [](const QString &, bool *) { return ImportClash::Replace; }, &err);
        CHECK(got == QStringList({ QStringLiteral("Loco 30007"), QStringLiteral("Loco 30009") })
                  && b.configNames().size() == before + 2,
              "PC B: both arrive");
        const LocoInfo::LocoConfig on7 = b.configNamed(QStringLiteral("Loco 30007"));
        CHECK(on7.targets[0].ip == QLatin1String("10.0.7.1") && on7.targets[0].port == 50011
                  && on7.targets[1].ip == QLatin1String("10.0.7.2") && on7.lastSentBody.isEmpty(),
              "PC B: with its targets, and no send history");

        // Importing again: each clash answer.
        QStringList asked;
        got = b.importConfigsFrom(file, [&](const QString &n, bool *) { asked << n; return ImportClash::KeepBoth; }, &err);
        CHECK(asked.size() == 2 && got.contains(QStringLiteral("Loco 30007 (2)")), "clash, keep both: stored as 'Name (2)'");
        const int count = b.configNames().size();
        got = b.importConfigsFrom(file, [](const QString &, bool *) { return ImportClash::Skip; }, &err);
        CHECK(got.isEmpty() && b.configNames().size() == count, "clash, skip: nothing changes");
        got = b.importConfigsFrom(file, [](const QString &, bool *stop) { *stop = true; return ImportClash::Skip; }, &err);
        CHECK(got.isEmpty() && b.configNames().size() == count, "cancel: the import stops");

        // PC B's file on disk has them (a restart keeps them).
        const QByteArray onDisk = read94(QDir(pcB.path()).filePath(QStringLiteral("loco_configs.json")));
        CHECK(onDisk.contains("Loco 30007") && onDisk.contains("10.0.7.2"), "PC B: saved to its loco_configs.json");
    }

    // ---- Firmware Flasher profiles ------------------------------------------------------------------
    {
        QTemporaryDir pcA, pcB, share, images;
        const QString vccImage = images.filePath(QStringLiteral("vcc.appimage"));
        write94(vccImage, QByteArray(64, 'x'));
        Flasher::FlashProfile bench;
        bench.name = QStringLiteral("Bench 3");
        bench.vccIp = QStringLiteral("10.1.3.168");
        bench.port = 50021;
        bench.defaultImages.insert(Flasher::CardVcc, vccImage);
        bench.defaultImages.insert(Flasher::CardInput, QStringLiteral("D:/nowhere/input.appimage"));
        bench.updaterWaitSeconds = 42;
        const QByteArray bytes = Flasher::exportProfiles({ bench });
        const Flasher::ImportedProfiles in = Flasher::importProfiles(bytes);
        CHECK(in.ok && in.profiles.size() == 1 && in.profiles[0].vccIp == QLatin1String("10.1.3.168")
                  && in.profiles[0].port == 50021 && in.profiles[0].updaterWaitSeconds == 42
                  && in.profiles[0].defaultImages.value(Flasher::CardVcc) == vccImage,
              "a profile reads back: VCC address, port, images and settings");
        const QStringList problems = Flasher::importedImageProblems(in.profiles[0]);
        CHECK(problems.size() == 1 && problems[0].contains(QLatin1String("input.appimage")),
              "an image path this PC does not have is named; one it has is not");
        CHECK(!Flasher::importProfiles(QByteArray("{\"format\":\"dlconsole-loco-configs\",\"configs\":[]}")).ok,
              "a loco-configuration file is refused");

        FlasherWindow a(nullptr, pcA.path());
        write94(share.filePath(QStringLiteral("in.dlflash")), bytes);
        QString err;
        QStringList imageProblems;
        QStringList got = a.importProfilesFrom(share.filePath(QStringLiteral("in.dlflash")),
                                               [](const QString &, bool *) { return ImportClash::KeepBoth; }, &err, &imageProblems);
        CHECK(got == QStringList({ QStringLiteral("Bench 3") }) && a.profileNames().contains(QStringLiteral("Bench 3")),
              "PC A: the profile is imported");
        CHECK(imageProblems.size() == 1 && imageProblems[0].startsWith(QLatin1String("Bench 3")),
              "and the missing image is reported against it");
        const QString file = share.filePath(QStringLiteral("out.dlflash"));
        CHECK(a.exportProfilesTo(file, { QStringLiteral("Bench 3") }, &err), "PC A: export it");

        FlasherWindow b(nullptr, pcB.path());
        got = b.importProfilesFrom(file, [](const QString &, bool *) { return ImportClash::KeepBoth; }, &err);
        const Flasher::FlashProfile onB = b.profileNamed(QStringLiteral("Bench 3"));
        CHECK(!got.isEmpty() && onB.vccIp == QLatin1String("10.1.3.168") && onB.port == 50021,
              "PC B: it arrives with its VCC address and port");
        got = b.importProfilesFrom(file, [](const QString &, bool *) { return ImportClash::KeepBoth; }, &err);
        CHECK(got == QStringList({ QStringLiteral("Bench 3 (2)") }), "again, keep both: 'Bench 3 (2)'");
        Flasher::FlashProfile changed = bench;
        changed.vccIp = QStringLiteral("10.1.3.200");
        write94(share.filePath(QStringLiteral("changed.dlflash")), Flasher::exportProfiles({ changed }));
        got = b.importProfilesFrom(share.filePath(QStringLiteral("changed.dlflash")),
                                   [](const QString &, bool *) { return ImportClash::Replace; }, &err);
        CHECK(got == QStringList({ QStringLiteral("Bench 3") }) && b.profileNamed(QStringLiteral("Bench 3")).vccIp == QLatin1String("10.1.3.200"),
              "replace: the existing profile takes the imported address");
    }
}
