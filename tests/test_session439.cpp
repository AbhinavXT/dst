#include "testutil.h"

#include "stationlayoutwindow.h"
#include "theme.h"
#include "uicolors.h"
#include "uistyle.h"

#include <QApplication>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

// =============================================================================
//  Session 439 — Export ▸ Drawing as a sheet like the SIP / tag-TIN layouts:
//  black on white whatever the theme (Light for the moment of the export,
//  the theme back after), the signals as real signals (432), and a title
//  block under the drawing (station, centre km, file, date, Checked /
//  Approved for the signatories). On station_527.xml, from Gruvbox Dark.
// =============================================================================

TEST_SUITE(session439)
{
    const QPalette saved = qApp->palette();
    ThemeUtil::apply(Theme::GruvboxDark);
    UiStyle::apply();
    StationLayoutWindow w;
    CHECK(w.openFile(QStringLiteral(DL_SRC_DIR "/tests/fixtures/station_527.xml")), "station_527.xml, in Gruvbox Dark");
    QTemporaryDir dir;
    const QString png = dir.filePath(QStringLiteral("sheet.png")), pdf = dir.filePath(QStringLiteral("sheet.pdf"));
    CHECK(w.exportDrawing(png), "Export ▸ PNG");
    const QImage img(png);
    CHECK(!img.isNull() && img.pixelColor(8, img.height() / 2).lightness() > 220, "on white, not the dark theme's sheet");
    CHECK(UiColor::activeTheme() == int(Theme::GruvboxDark) && qApp->palette().color(QPalette::Window) == QColor(0x28, 0x28, 0x28),
          "the theme back after");
    CHECK(w.exportDrawing(pdf) && QFileInfo(pdf).size() > 20000, "Export ▸ PDF, A3, with the title block");
    CHECK(UiColor::activeTheme() == int(Theme::GruvboxDark), "and back after that too");
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    qApp->setPalette(saved);
}
