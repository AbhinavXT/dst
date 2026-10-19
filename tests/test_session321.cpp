#include "testutil.h"

#include "lococonfigmodel.h"
#include "lococonfigwindow.h"
#include "settings.h"
#include "statusline.h"

#include <QTemporaryDir>

// =============================================================================
//  Session 321 — Loco Configuration opens when the chosen kavach.xml is gone.
//
//  Abhinav's screenshot: "Cannot open /Users/abhinavpathak/Desktop/kavach.xml:
//  No such file or directory", and nothing else in the window. The window now
//  falls back to the built-in schema, as Packet Maker does, and says so.
// =============================================================================

TEST_SUITE(session321)
{
    const QString kept = Settings::schemaPath();
    QTemporaryDir temp;
    Settings::setSchemaPath(temp.filePath(QStringLiteral("gone/kavach.xml")));
    {
        LocoConfigWindow window(nullptr, temp.path());
        CHECK(window.isUsable(), "a missing chosen schema: the window still opens, on the built-in copy");
        const StatusLine *status = window.findChild<StatusLine *>();
        CHECK(status && status->text().contains(QStringLiteral("using the built-in copy"))
                  && status->text().contains(QStringLiteral("gone/kavach.xml")),
              "and says which file could not be read");
        CHECK(window.fieldModel() && window.fieldModel()->rowCount() > 0, "the fields are listed");
    }
    Settings::setSchemaPath(QString());
    {
        LocoConfigWindow window(nullptr, temp.path());
        const StatusLine *status = window.findChild<StatusLine *>();
        CHECK(window.isUsable() && status && !status->text().contains(QStringLiteral("built-in copy")),
              "the built-in schema chosen: no note");
    }
    Settings::setSchemaPath(kept);
}
