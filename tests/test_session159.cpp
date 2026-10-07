#include "testutil.h"

#include "lococonfigcomparedialog.h"
#include "lococonfigcore.h"
#include "lococonfigwindow.h"
#include "theme.h"
#include "uistyle.h"

#include <QAction>
#include <QMenu>
#include <QTemporaryDir>
#include <QToolButton>

// =============================================================================
//  Session 159 — Loco Configuration: compare two configurations.
//  Values from the real defaults (loco_defaults.json, the loco_config tool's).
// =============================================================================

TEST_SUITE(session159)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    LocoInfo::Layout layout;
    QString error;
    CHECK(layout.load(QStringLiteral(":/schema/kavach.xml"), &error), "the LOCO_INFO layout loads");
    LocoInfo::Values defaults;
    CHECK(LocoInfo::loadDefaults(QStringLiteral(":/lococonfig/loco_defaults.json"), layout, &defaults, &error),
          "the defaults load");
    LocoInfo::completeValues(layout, LocoInfo::Values(), &defaults);
    const LocoInfo::Presentation presentation = LocoInfo::Presentation::fromLayout(layout);

    // Loco 9 made by duplicating loco 7, then its own unit id and vcc_crc.
    LocoInfo::LocoConfig seven;
    seven.name = QStringLiteral("Loco 7");
    seven.values = defaults;
    seven.values.insert(QStringLiteral("loco_unit_id"), 30407);
    LocoInfo::LocoConfig nine = seven;
    nine.name = QStringLiteral("Loco 9");
    nine.values.insert(QStringLiteral("loco_unit_id"), 30409);
    nine.values.insert(QStringLiteral("vcc_crc"), 0x0BADCAFELL);
    nine.locked = QStringList{ QStringLiteral("vcc_crc") };
    LocoInfo::LocoConfig twin = seven;
    twin.name = QStringLiteral("Loco 7 copy");

    LocoConfigCompareDialog dialog(&layout, &presentation, { seven, nine, twin }, QStringLiteral("Loco 7"));
    CHECK(dialog.rowCount() == 2, "A = the open configuration, B = the next: the two differing fields only");
    QStringList keys;
    for (int r = 0; r < dialog.rowCount(); ++r) keys << dialog.fieldAt(r);
    CHECK(keys.contains(QStringLiteral("loco_unit_id")) && keys.contains(QStringLiteral("vcc_crc")),
          "loco_unit_id and vcc_crc");
    const int vccRow = keys.indexOf(QStringLiteral("vcc_crc"));
    CHECK(dialog.valueAt(vccRow, false) == QStringLiteral("0xF6134AC1") &&
              dialog.valueAt(vccRow, true) == QStringLiteral("0x0BADCAFE"),
          "values formatted as the editor shows them (hex), A beside B");
    CHECK(dialog.summary().startsWith(QStringLiteral("2 of ")) && dialog.summary().contains(QStringLiteral("differ")),
          "the summary counts them");

    dialog.setShowAll(true);
    CHECK(dialog.rowCount() == layout.fields().size() - 1, "Show all fields: every field but the computed CRC");
    dialog.setShowAll(false);

    dialog.setPair(QStringLiteral("Loco 7"), QStringLiteral("Loco 7 copy"));
    CHECK(dialog.rowCount() == 0 && dialog.summary().contains(QStringLiteral("send the same bytes")),
          "identical configurations: nothing listed, and it says they are the same");
    dialog.setPair(QStringLiteral("Loco 7"), QStringLiteral("Loco 7"));
    CHECK(dialog.summary().contains(QStringLiteral("same configuration")), "A = B is said, not shown as 'all the same'");

    dialog.setPair(QStringLiteral("Loco 9"), QStringLiteral("Loco 7"));
    const QString text = dialog.asText();
    CHECK(text.startsWith(QStringLiteral("Field\tGroup\tLoco 9\tLoco 7")) &&
              text.contains(QStringLiteral("vcc_crc\t")) && text.contains(QStringLiteral("0x0BADCAFE\t0xF6134AC1")),
          "Copy: tab-separated, headed by the two names, swapped order respected");

    // Reachable from the window: Manage ▸ Compare configurations…
    QTemporaryDir temp;
    LocoConfigWindow window(nullptr, temp.path());
    bool found = false;
    for (QToolButton *b : window.findChildren<QToolButton *>())
        if (b->menu())
            for (QAction *a : b->menu()->actions())
                if (a->text() == QStringLiteral("Compare configurations…")) found = true;
    CHECK(found, "Manage ▸ Compare configurations…");
}
