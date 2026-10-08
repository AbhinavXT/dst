#include "testutil.h"

#include "dmipanel.h"
#include "theme.h"
#include "uistyle.h"

#include <QFile>
#include <QImage>
#include <QPainter>

// =============================================================================
//  Session 166 — DMI: a cross over the antenna when there is no radio
//  (signal_strength 0). Real frames: replay/loco_1_1_26062026_162418.cap,
//  @dmi #0 (signal_strength 0) and #289 (signal_strength 5).
// =============================================================================

namespace {

QString nthDmi166(const QString &file, int n)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/") + file);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    int seen = 0;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.startsWith("@dmi_") && seen++ == n) return QString::fromLatin1(l);
    }
    return QString();
}

// Pure red (Annexure-B "bright red") pixels over the antenna, region J.
int redOverAntenna(const DmiState &s, bool stale = false)
{
    DmiView v;
    v.setAnnexureColours(true);
    v.setState(s);
    v.setStale(stale);
    v.resize(800, 600);
    QImage img(800, 600, QImage::Format_ARGB32);
    img.fill(Qt::black);
    v.render(&img);
    const QByteArray dir = qgetenv("DL_SHOTS");
    if (!dir.isEmpty())
        img.save(QString::fromLocal8Bit(dir) + QStringLiteral("/dmi_radio_%1%2.png").arg(s.rfBars).arg(stale ? "_stale" : ""));
    int n = 0;
    for (int y = 421; y < 472; ++y)
        for (int x = 591; x < 643; ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 200 && c.green() < 60 && c.blue() < 60) ++n;
        }
    return n;
}

}  // namespace

TEST_SUITE(session166)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    const QString file = QStringLiteral("loco_1_1_26062026_162418.cap");
    const DmiState none = dmiStateFromLine(nthDmi166(file, 0));
    const DmiState full = dmiStateFromLine(nthDmi166(file, 289));
    CHECK(none.valid && full.valid, "fixture: two real @dmi frames");
    CHECK(none.rfBars == 0, "frame #0: signal_strength 0");
    CHECK(full.rfBars == 5, "frame #289: signal_strength 5");

    const int redNone = redOverAntenna(none);
    const int redFull = redOverAntenna(full);
    CHECK(redNone > 60, QByteArray("no radio: a red cross over the antenna (red px ") + QByteArray::number(redNone) + ")");
    CHECK(redFull == 0, QByteArray("five bars: no cross (red px ") + QByteArray::number(redFull) + ")");
    CHECK(redOverAntenna(none, true) == 0, "stale panel: the cross greys out with the rest");

    DmiState one = full;
    one.rfBars = 1;
    CHECK(redOverAntenna(one) == 0, "one bar is still a signal: no cross");

    bool noted = false;
    for (const QString &a : dmiAssumptions()) noted |= a.contains(QStringLiteral("crosses out the antenna"));
    CHECK(noted, "the Fields notes say what the cross means");
}
