#include "testutil.h"

#include "capturedecoder.h"
#include "dmipanel.h"
#include "theme.h"
#include "uicolors.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSet>

// =============================================================================
//  Session 89: the DMI's tag IDs and statuses, the signal post with no
//  aspect, and four more themes on Sage's recipe.
// =============================================================================

namespace {

// Render the panel at exactly 800 x 600, so panel units are pixels.
QImage render89(const DmiState &s, bool annexure = true)
{
    DmiView v;
    v.setAnnexureColours(annexure);
    v.resize(800, 600);
    v.setState(s);
    QImage img(800, 600, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    v.render(&img);
    return img;
}

// Pixels in `r` that are not the background colour `bg`.
int inked(const QImage &img, const QRect &r, QColor bg)
{
    int n = 0;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (qAbs(c.red() - bg.red()) + qAbs(c.green() - bg.green()) + qAbs(c.blue() - bg.blue()) > 60) ++n;
        }
    return n;
}

DmiState base89()
{
    DmiState s;
    s.valid = true;
    s.speed = 42;
    s.permitted = 100;
    return s;
}

QStringList realDmiLines89(int limit)
{
    QStringList out;
    const QDir dir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"));
    for (const QString &f : dir.entryList({ QStringLiteral("loco_*.cap") }, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        while (!file.atEnd() && out.size() < limit) {
            const QString l = QString::fromLatin1(file.readLine()).trimmed();
            if (l.startsWith(QLatin1String("@dmi_"))) out << l;
        }
    }
    return out;
}

}  // namespace

TEST_SUITE(session89)
{
    const QColor black(0, 0, 0);   // the annexure background

    // ---- 1. tag IDs and statuses, from real frames --------------------------------------
    {
        const QStringList lines = realDmiLines89(12000);
        int withTags = 0, statusOne = 0, idsMatch = 0;
        for (const QString &l : lines) {
            const DmiState s = dmiStateFromLine(l);
            if (!s.valid || s.tagIds[0] == 0) continue;
            ++withTags;
            if (s.tagStatus[0] == 1) ++statusOne;
            const CaptureLine cap = CaptureDecoder::parseLine(l);
            QHash<QString, qint64> raw;
            CaptureDecoder::describe(cap, nullptr, 0, &raw);
            if (raw.value(QStringLiteral("rc")) == s.tagIds[0] && raw.value(QStringLiteral("rl")) == s.tagIds[1]
                && raw.value(QStringLiteral("rll")) == s.tagIds[2] && raw.value(QStringLiteral("rlls")) == s.tagStatus[2]) {
                ++idsMatch;
            }
        }
        CHECK(withTags > 1000 && statusOne > 1000, QByteArray("real frames carry tag IDs with status 1 (") + QByteArray::number(withTags) + "/" + QByteArray::number(statusOne) + " of " + QByteArray::number(lines.size()) + ")");
        CHECK(idsMatch == withTags, "the panel takes the three IDs and statuses from rc/rl/rll and their status fields");
    }

    const QRect idColumn(704 + 50, 419 + 20, 800 - 704 - 51, 105);   // M, right of the track
    {
        DmiState s = base89();
        s.tagIds[0] = 790; s.tagStatus[0] = 1;
        s.tagIds[1] = 788; s.tagStatus[1] = 2;
        s.tagIds[2] = 786; s.tagStatus[2] = 1;
        const QImage withIds = render89(s);
        DmiState e = base89();                                      // all slots empty
        const QImage empty = render89(e);
        const int a = inked(withIds, idColumn, black), b = inked(empty, idColumn, black);
        CHECK(a > 3 * b && a > 150,
              QByteArray("tag IDs are written beside the track (") + QByteArray::number(a) + " vs "
                  + QByteArray::number(b) + " inked pixels for three empty slots)");
        // Each row carries its own ID: a row with an ID has more ink than the same row empty.
        for (int k = 0; k < 3; ++k) {
            const QRect row(idColumn.left(), 419 + 20 + 22 + k * 30 - 7, idColumn.width(), 22);
            CHECK(inked(withIds, row, black) > inked(empty, row, black) + 30,
                  QByteArray("row ") + QByteArray::number(k) + " shows its ID");
        }
        // Status in colour: the read tag's ID uses the read green, the missed one the red.
        const DmiColours c = DmiColours::annexure();
        auto hasColour = [&](const QRect &r, const QColor &want) {
            for (int y = r.top(); y <= r.bottom(); ++y)
                for (int x = r.left(); x <= r.right(); ++x) {
                    const QColor px = withIds.pixelColor(x, y);
                    if (qAbs(px.red() - want.red()) + qAbs(px.green() - want.green()) + qAbs(px.blue() - want.blue()) < 40)
                        return true;
                }
            return false;
        };
        const QRect row0(idColumn.left(), 419 + 20 + 22 - 7, idColumn.width(), 22);
        const QRect row1(idColumn.left(), 419 + 20 + 22 + 30 - 7, idColumn.width(), 22);
        CHECK(hasColour(row0, c.dgr) && !hasColour(row0, c.brd), "status 1: the ID is in the read colour");
        CHECK(hasColour(row1, c.brd), "status 2: the ID is in the missed colour");
    }

    // ---- 2. the signal post with no aspect ------------------------------------------------
    {
        const QRect postLeftEdge(648, 70, 6, 170);                    // the post's left side
        const QRect lamps(666, 60, 40, 200);                          // the lamp column
        DmiState none = base89();                                     // aspect 0: unidentified
        const QImage img0 = render89(none);
        CHECK(inked(img0, postLeftEdge, black) > 100, "aspect 0: the signal post is drawn");
        DmiState red = base89();
        red.aspect = 1;
        const QImage img1 = render89(red);
        const QColor lampRed = UiColor::signalLamp(QStringLiteral("red"));
        auto count = [](const QImage &img, const QRect &r, const QColor &want) {
            int n = 0;
            for (int y = r.top(); y <= r.bottom(); ++y)
                for (int x = r.left(); x <= r.right(); ++x) {
                    const QColor px = img.pixelColor(x, y);
                    if (qAbs(px.red() - want.red()) + qAbs(px.green() - want.green()) + qAbs(px.blue() - want.blue()) < 30) ++n;
                }
            return n;
        };
        CHECK(count(img0, lamps, lampRed) == 0 && count(img1, lamps, lampRed) > 300,
              "with no aspect every lamp is unlit; a red aspect lights the red lamp");
        // Session 92 reversed this: the distance is printed with no aspect
        // too ("0000 m"), as the project asked.
        CHECK(inked(img0, QRect(569, 360, 231, 26), black) > 30,
              "the signal distance is printed without an aspect as well (session 92)");
        // A real unidentified-aspect frame draws the post too.
        for (const QString &l : realDmiLines89(12000)) {
            const DmiState s = dmiStateFromLine(l);
            if (s.valid && s.aspect == 0) {
                CHECK(inked(render89(s), postLeftEdge, black) > 100, "a real frame with aspect 0: the post is drawn");
                break;
            }
        }
    }

    // ---- 3. themes --------------------------------------------------------------------------
    {
        const QVector<Theme> all = ThemeUtil::all();
        CHECK(all.size() == 11, "eleven themes");
        for (Theme t : { Theme::Ocean, Theme::Lavender, Theme::Rose, Theme::Amber }) {
            const QString key = QString::fromLatin1(ThemeUtil::toString(t));
            CHECK(all.contains(t) && ThemeUtil::fromString(key) == t && !ThemeUtil::isDark(t),
                  QByteArray("a light theme, saved and read back as \"") + key.toUtf8() + "\"");
        }
        QSet<QString> labels, highlights;
        for (Theme t : all) {
            labels.insert(ThemeUtil::label(t));
            if (t != Theme::Light && t != Theme::Dark) highlights.insert(ThemeUtil::colorsFor(t).highlight.name());
        }
        CHECK(labels.size() == all.size(), "every theme has its own name");
        CHECK(highlights.size() == all.size() - 2, "every palette theme has its own accent");
        // Like Sage: tinted greys, no pure white window or base, one accent.
        for (Theme t : { Theme::Ocean, Theme::Lavender, Theme::Rose, Theme::Amber }) {
            const ThemeUtil::ThemeColors c = ThemeUtil::colorsFor(t);
            CHECK(c.window != QColor(Qt::white) && c.base != QColor(Qt::white)
                      && c.highlight.hsvSaturation() > 80 && c.window.hsvSaturation() < 40,
                  QByteArray(ThemeUtil::toString(t)) + ": tinted greys, a saturated accent");
            CHECK(UiColor::contrastRatio(c.text, c.base) >= 7.0 && UiColor::contrastRatio(c.highlightedText, c.highlight) >= 4.5,
                  QByteArray(ThemeUtil::toString(t)) + ": body text at least 7:1, selection at least 4.5:1");
        }
        // The accents really are other colours: hue away from Sage's green.
        const int sage = ThemeUtil::colorsFor(Theme::Sage).highlight.hsvHue();
        for (Theme t : { Theme::Ocean, Theme::Lavender, Theme::Rose, Theme::Amber }) {
            const int h = ThemeUtil::colorsFor(t).highlight.hsvHue();
            const int d = qMin(qAbs(h - sage), 360 - qAbs(h - sage));
            CHECK(d > 40, QByteArray(ThemeUtil::toString(t)) + ": its accent is not Sage's green");
        }
        ThemeUtil::apply(Theme::Light);
    }
}
