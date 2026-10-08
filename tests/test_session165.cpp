#include "testutil.h"

#include "settingsbundle.h"

#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

// =============================================================================
//  Session 165 — Export / Import settings carries everything set up since:
//  the Loco Console's layout, lanes and custom lanes, watches and saved
//  filters, log columns / Find / DMI / display options, the serial
//  terminal's own options. And still not what belongs to the machine.
// =============================================================================

using namespace SettingsBundle;

TEST_SUITE(session165)
{
    QTemporaryDir from, to;
    const QString iniA = from.filePath(QStringLiteral("dlconsole.ini"));
    const QString iniB = to.filePath(QStringLiteral("dlconsole.ini"));

    // A console set up by hand: one value per key, in the shapes the code writes.
    const QList<QPair<QString, QVariant>> carried = {
        // Pins: the Loco Console's layout
        { QStringLiteral("lococonsole/bigNumbersShown"), true },
        { QStringLiteral("lococonsole/cabFields"), QStringList{ QStringLiteral("dmi|speed"), QStringLiteral("lsrp|LOCO_MODE") } },
        { QStringLiteral("lococonsole/cabShown"), false },
        { QStringLiteral("lococonsole/highlightChanges"), true },
        // Lanes
        { QStringLiteral("ui/laneBand"), false },
        { QStringLiteral("ui/customLanes"), QStringList{ QStringLiteral("lsrp\tTRAIN_SPEED"), QStringLiteral("dmi\tbrake_type") } },
        // Watches and saved filters
        { QStringLiteral("ui/watches"), QStringList{ QStringLiteral("EB applied\t@dmi field:brake_type=4\t1\t0\t0") } },
        { QStringLiteral("ui/pin_freeze_on_watch"), true },
        { QStringLiteral("query/preset_names"), QStringList{ QStringLiteral("errors"), QStringLiteral("trip") } },
        { QStringLiteral("query/preset_queries"), QStringList{ QStringLiteral("sev:error"), QStringLiteral("LOCO_MODE=7") } },
        // View
        { QStringLiteral("ui/visible_columns"), QStringList{ QStringLiteral("0"), QStringLiteral("3") } },
        { QStringLiteral("ui/hidden_columns"), QStringList{ QStringLiteral("4") } },
        { QStringLiteral("ui/column_widths"), QStringList{ QStringLiteral("120"), QStringLiteral("80") } },
        { QStringLiteral("ui/row_density"), 2 },
        { QStringLiteral("ui/show_utc"), true },
        { QStringLiteral("ui/find_advanced"), true },
        { QStringLiteral("ui/find_detached"), false },
        { QStringLiteral("ui/find_mode"), 1 },
        { QStringLiteral("ui/find_scan_limit"), 50000 },
        { QStringLiteral("ui/find_wrap"), false },
        { QStringLiteral("dmi/annexureColours"), true },
        { QStringLiteral("dmi/followCursor"), true },
        { QStringLiteral("dmi/twoLocos"), true },
        { QStringLiteral("ui/offline_warn_seconds"), 7 },
        { QStringLiteral("ui/offline_err_seconds"), 21 },
        { QStringLiteral("ui/two_loco_warn_apart_km"), 1.5 },
        // Serial: the terminal's own options
        { QStringLiteral("serial/echo"), true },
        { QStringLiteral("serial/ending"), 2 },
        { QStringLiteral("serial/hexView"), true },
        { QStringLiteral("serial/timestamps"), false },
        { QStringLiteral("serial/sendHex"), true },
    };
    // The machine's, or the record's: never carried.
    const QList<QPair<QString, QVariant>> kept = {
        { QStringLiteral("flasher/mode"), QStringLiteral("engineer") },
        { QStringLiteral("network/udp_port"), 50002 },
        { QStringLiteral("disk/log_root"), QStringLiteral("D:/logs") },
        { QStringLiteral("ui/per_tab_capacity"), 99999 },
        { QStringLiteral("query/recent"), QStringList{ QStringLiteral("x") } },
        { QStringLiteral("serial/lastPort"), QStringLiteral("COM9") },
    };
    {
        QSettings a(iniA, QSettings::IniFormat);
        for (const auto &kv : carried) a.setValue(kv.first, kv.second);
        for (const auto &kv : kept) a.setValue(kv.first, kv.second);
    }

    CHECK(allSections().contains(Section::Lanes) && allSections().contains(Section::Watches)
              && allSections().contains(Section::View),
          "the new sections are offered: lanes, watches, view");
    CHECK(label(Section::Pins).contains(QStringLiteral("Loco Console")), "Pins names the Loco Console too");

    const QString file = from.filePath(QStringLiteral("settings.json"));
    CHECK(writeFile(file, capture(allSections(), from.path(), iniA)), "exported");
    Bundle read;
    // The file sections (layouts, flasher profiles, loco configurations) are
    // absent: this fixture has no such files, and capture() leaves them out.
    const QList<Section> iniSections{ Section::Appearance, Section::Tags, Section::Pins, Section::Lanes,
                                      Section::Watches, Section::View, Section::Serial };
    CHECK(readFile(file, &read) && read.present() == iniSections, "read back, every INI section present");
    CHECK(read.summary(Section::Lanes).contains(QStringLiteral("lanes off"))
              && read.summary(Section::Lanes).contains(QStringLiteral("lsrp \u25B8 TRAIN_SPEED")),
          QByteArray("the chooser says what the lanes section holds (") + read.summary(Section::Lanes).toUtf8() + ")");
    CHECK(read.summary(Section::Watches) == QStringLiteral("1 watches, 2 saved filters"), "and the watches section");

    {
        QSettings b(iniB, QSettings::IniFormat);
        b.setValue(QStringLiteral("flasher/mode"), QStringLiteral("operator"));   // the target station's own
    }
    QString error;
    CHECK(apply(read, read.present(), to.path(), iniB, &error), QByteArray("imported (") + error.toUtf8() + ")");

    QSettings b(iniB, QSettings::IniFormat);
    QStringList wrong;
    for (const auto &kv : carried) {
        const QVariant got = b.value(kv.first);
        const bool same = kv.second.type() == QVariant::StringList
                              ? got.toStringList() == kv.second.toStringList()
                              : got.toString() == kv.second.toString();
        if (!same) wrong << kv.first + QStringLiteral(" = ") + got.toString();
    }
    CHECK(wrong.isEmpty(), QByteArray("every carried key arrives with its value (wrong: ") + wrong.join(QStringLiteral(", ")).toUtf8() + ")");
    CHECK(b.value(QStringLiteral("ui/laneBand")).toBool() == false && b.value(QStringLiteral("ui/row_density")).toInt() == 2
              && qAbs(b.value(QStringLiteral("ui/two_loco_warn_apart_km")).toDouble() - 1.5) < 1e-9,
          "and reads back as the type the code reads (bool, int, double)");
    CHECK(b.value(QStringLiteral("flasher/mode")).toString() == QStringLiteral("operator"),
          "the Flasher's engineer/operator mode is the station's own: not carried, not overwritten");
    QStringList leaked;
    for (const auto &kv : kept)
        if (kv.first != QLatin1String("flasher/mode") && b.contains(kv.first)) leaked << kv.first;
    CHECK(leaked.isEmpty(), QByteArray("nothing of the machine's is carried (leaked: ") + leaked.join(QStringLiteral(", ")).toUtf8() + ")");

    // A file from before this patch (no new sections) still imports.
    Bundle old = read;
    for (const QString &sid : { QStringLiteral("lanes"), QStringLiteral("watches"), QStringLiteral("view") })
        old.sections.remove(sid);
    CHECK(apply(old, old.present(), to.path(), iniB, &error) && !old.has(Section::Lanes),
          "an older file, without the new sections, imports what it has");
}
