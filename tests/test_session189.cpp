#include "testutil.h"

#include "dmipanel.h"
#include "theme.h"
#include "uistyle.h"

#include <QImage>

// =============================================================================
//  Session 189 — DMI: the speed in the dial's hub can be read in a light theme.
//
//  The hub takes the pointer's colour: white in Annexure-B colours and in a
//  dark theme, but the theme's ink (black) in a light theme, and the digits
//  were always black. Rendered at 800 x 600 (scale 1, the hub at 274.5, 186,
//  radius 26): the pixels in the digits' band that stand out from the hub's
//  own colour are counted. Black on black left none.
// =============================================================================

namespace {

int digitPixels(DmiView &v, QColor *hubOut)
{
    DmiState st;
    st.valid = true;
    st.speed = 45;
    st.hasPermitted = true;
    st.permitted = 60;
    st.mode = 4;
    v.resize(800, 600);
    v.setState(st);
    const QImage img = v.grab().toImage().convertToFormat(QImage::Format_RGB32);
    const QColor hub = img.pixelColor(274 - 22, 186);      // inside the hub, left of the digits
    if (hubOut) *hubOut = hub;
    int n = 0;
    for (int y = 176; y <= 196; ++y)
        for (int x = 258; x <= 291; ++x)
            if (std::abs(qGray(img.pixel(x, y)) - qGray(hub.rgb())) > 100) ++n;
    return n;
}

}  // namespace

TEST_SUITE(session189)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
    {
        DmiView v;
        QColor hub;
        const int n = digitPixels(v, &hub);
        CHECK(qGray(hub.rgb()) < 80, QByteArray("light theme: the hub is dark (gray ") + QByteArray::number(qGray(hub.rgb())) + ")");
        CHECK(n >= 25, QByteArray("and the speed on it stands out (") + QByteArray::number(n) + " pixels; was about 0)");
    }
    {
        DmiView v;
        v.setAnnexureColours(true);
        QColor hub;
        const int n = digitPixels(v, &hub);
        CHECK(qGray(hub.rgb()) > 200 && n >= 25, "Annexure-B colours: white hub, black digits, as before");
    }
    ThemeUtil::apply(Theme::Dark);
    UiStyle::apply();
    {
        DmiView v;
        QColor hub;
        const int n = digitPixels(v, &hub);
        CHECK(qGray(hub.rgb()) > 150 && n >= 25, "dark theme: light hub, dark digits");
    }
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();
}
