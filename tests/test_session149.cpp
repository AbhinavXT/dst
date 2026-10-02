#include "testutil.h"
#include "layoutaudit.h"

#include "brakingpanel.h"
#include "messagedispatcher.h"
#include "theme.h"
#include "uistyle.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
#include <QSlider>
#include <QTableWidget>

// =============================================================================
//  Session 149 — UI revamp, tool windows 25: the Braking panel.
//
//  Its minimum was 2042 px wide: the one-line summary under the scrubber set
//  it. The summary wraps; the frame/time readout and its two toggles moved
//  under the slider; the top row is narrower (tab box 120, not 180). Now
//  1036 px on the Mac. The tables get half the height (they showed 3 of the
//  summary's 10 rows) and the summary a share of the width ("Value" was cut
//  and scrolled). Counts in English ("1 frame/s", "up to 1 target").
//
//  SYNTHETIC @uba: schema/fixtures/uba_synthetic.log. No capture in replay/
//  carries braking curves; this checks layout only, not firmware.
// =============================================================================

TEST_SUITE(session149)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    MessageDispatcher disp;
    QFile uba(QStringLiteral(DL_SRC_DIR "/schema/fixtures/uba_synthetic.log"));
    CHECK(uba.open(QIODevice::ReadOnly), "fixture: the synthetic @uba log");
    int frames = 0;
    while (!uba.atEnd()) {
        const QByteArray l = uba.readLine().trimmed();
        if (!l.startsWith('@')) continue;
        const QList<QByteArray> tok = l.split(' ');
        if (tok.size() < 3) continue;
        disp.ingestLocal(21, 1, l, QDateTime::fromString(QString::fromLatin1(tok.at(1)), Qt::ISODate).toMSecsSinceEpoch(), QString());
        ++frames;
    }
    disp.drainNow();
    CHECK(frames == 20, "fixture: 20 frames");

    BrakingPanel w(&disp);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.resize(1100, 720);
    w.show();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    // ---- fits ---------------------------------------------------------------------
    CHECK(w.minimumSizeHint().width() <= 1100 && w.minimumSizeHint().height() <= 700,
          QByteArray("fits a laptop (minimum ") + QByteArray::number(w.minimumSizeHint().width()) + " x "
              + QByteArray::number(w.minimumSizeHint().height()) + "; was 2042 wide)");

    // ---- the summary wraps, and counts in English --------------------------------
    QLabel *summary = nullptr;
    for (QLabel *l : w.findChildren<QLabel *>())
        if (l->text().startsWith(QLatin1String("20 frames"))) summary = l;
    CHECK(summary && summary->wordWrap(), "the summary under the scrubber wraps (on one line it set the width)");
    if (summary) {
        CHECK(summary->text().contains(QLatin1String("up to 1 target each")), "\"up to 1 target each\" (was \"1 targets\")");
        CHECK(summary->text().contains(QLatin1String("1 frame/s")), "\"1 frame/s\" (was \"1 frames/s\")");
        CHECK(summary->height() >= summary->heightForWidth(summary->width()), "and is shown whole");
    }

    // ---- the readout is under the slider ------------------------------------------
    QSlider *slider = w.findChild<QSlider *>();
    QLabel *readout = nullptr;
    for (QLabel *l : w.findChildren<QLabel *>())
        if (l->text().startsWith(QLatin1String("frame "))) readout = l;
    CHECK(slider && readout && readout->mapTo(&w, QPoint()).y() > slider->mapTo(&w, QPoint()).y() + slider->height() / 2,
          "the frame / time readout sits under the slider, not beside it");
    CHECK(slider && slider->width() >= 700, "and the slider has the width");

    // ---- the tables ---------------------------------------------------------------
    QTableWidget *fields = nullptr;
    for (QTableWidget *t : w.findChildren<QTableWidget *>())
        if (t->columnCount() == 2) fields = t;
    CHECK(fields != nullptr, "fixture: the Field / Value table");
    if (fields) {
        CHECK(fields->rowCount() >= 10, "fixture: the summary rows");
        CHECK(fields->viewport()->height() / fields->verticalHeader()->defaultSectionSize() >= 6,
              "at least six summary rows in view (was three)");
        CHECK(!fields->horizontalScrollBar()->isVisible(), "both columns in view, no sideways scroll (\"Valu\" was cut)");
        CHECK(fields->columnWidth(1) >= 200, "and the values have room");
    }

    const QStringList loose = LayoutAudit::orphans(&w);
    CHECK(loose.isEmpty(), QByteArray("no visible widget outside every layout (") + loose.join(QLatin1String(", ")).toUtf8() + ")");
}
