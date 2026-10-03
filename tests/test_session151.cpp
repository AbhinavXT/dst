#include "testutil.h"
#include "layoutaudit.h"

#include "exportdialog.h"
#include "exporter.h"
#include "gototimestampdialog.h"
#include "sessionkeydialog.h"
#include "settingsdialog.h"
#include "theme.h"
#include "uistyle.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFontMetrics>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

// =============================================================================
//  Session 151 — UI revamp, the small dialogs: Session key, Settings, Export,
//  Go to timestamp.
//
//  Session key: the two key sets side by side (stacked, the dialog was 744 px
//  tall); hex in UiStyle's mono, wide enough for 32 digits (QFont("monospace")
//  came out proportional); "Randoms & ids" (the & was a mnemonic and showed
//  as "Randoms _ids"); Derive is the primary button. Settings: the disk-log
//  root takes the row's width ("S/LOGS"), and the wrapper rows lose their
//  11-px margins. Export: column names and notes in two columns (they were
//  padded with spaces). Go to timestamp: unchanged, checked to fit.
// =============================================================================

namespace {

void settle()
{
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
}

QStringList loose(QWidget *w)
{
    return LayoutAudit::orphans(w);
}

}  // namespace

TEST_SUITE(session151)
{
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    // ---- Session key ----------------------------------------------------------------
    {
        SessionKeyDialog d;
        d.show();
        settle();
        CHECK(d.minimumSizeHint().height() <= 700 && d.minimumSizeHint().width() <= 1100,
              QByteArray("Session key fits a laptop (minimum ") + QByteArray::number(d.minimumSizeHint().width()) + " x "
                  + QByteArray::number(d.minimumSizeHint().height()) + "; was 533 x 744)");
        QGroupBox *s1 = nullptr, *s2 = nullptr, *ids = nullptr;
        for (QGroupBox *g : d.findChildren<QGroupBox *>()) {
            if (g->title() == QLatin1String("Key set 1")) s1 = g;
            if (g->title() == QLatin1String("Key set 2")) s2 = g;
            if (g->title().startsWith(QLatin1String("Randoms"))) ids = g;
        }
        CHECK(s1 && s2 && s1->y() == s2->y() && s2->x() > s1->x(), "the two key sets sit side by side");
        CHECK(ids && ids->title() == QLatin1String("Randoms && ids"), "\"Randoms & ids\": the & escaped, not a mnemonic");
        int hex = 0;
        bool allMono = true, allFit = true;
        for (QLineEdit *e : d.findChildren<QLineEdit *>()) {
            if (e->placeholderText() != QLatin1String("32 hex chars")) continue;
            ++hex;
            allMono = allMono && e->font().family() == UiStyle::monoFont().family();
            allFit = allFit && e->width() >= QFontMetrics(e->font()).horizontalAdvance(QString(32, QLatin1Char('8'))) + 8;
        }
        CHECK(hex == 4, "fixture: four key fields");
        CHECK(allMono, "keys in UiStyle's mono");
        CHECK(allFit, "each shows all 32 digits");
        QPushButton *derive = nullptr;
        for (QPushButton *b : d.findChildren<QPushButton *>())
            if (b->text() == QLatin1String("Derive session key")) derive = b;
        CHECK(derive && derive->property("dlRole").toString() == QLatin1String("primary"), "Derive is the primary button");
        CHECK(loose(&d).isEmpty(), "Session key: layout audit");
    }

    // ---- Settings -----------------------------------------------------------------------
    {
        SettingsDialog d;
        d.show();
        settle();
        CHECK(d.minimumSizeHint().height() <= 700 && d.minimumSizeHint().width() <= 1100, "Settings fits a laptop");
        QLineEdit *root = nullptr;
        for (QLineEdit *e : d.findChildren<QLineEdit *>())
            if (!qobject_cast<QSpinBox *>(e->parentWidget())) root = e;
        CHECK(root && root->width() >= 240, "the disk-log root has room for a path (it showed \"S/LOGS\")");
        // The wrapped rows' first field starts where the plain rows' does.
        QList<int> xs;
        for (QSpinBox *sb : d.findChildren<QSpinBox *>()) xs << sb->mapTo(&d, QPoint()).x();
        std::sort(xs.begin(), xs.end());
        CHECK(!xs.isEmpty() && xs.last() - xs.first() <= 1,
              QByteArray("every field starts at the same x (") + QByteArray::number(xs.isEmpty() ? 0 : xs.first()) + " - "
                  + QByteArray::number(xs.isEmpty() ? 0 : xs.last()) + ")");
        CHECK(loose(&d).isEmpty(), "Settings: layout audit");
    }

    // ---- Export -------------------------------------------------------------------------
    {
        ExportDialog d(QStringLiteral("/tmp/loco_1_1_export.csv"), Exporter::CSV, Exporter::ColAll);
        d.show();
        settle();
        int boxes = 0;
        bool noPadding = true, notesBeside = true;
        for (QCheckBox *cb : d.findChildren<QCheckBox *>()) {
            ++boxes;
            noPadding = noPadding && !cb->text().contains(QLatin1String("  "));
            bool beside = false;
            for (QLabel *l : d.findChildren<QLabel *>())
                if (qAbs(l->mapTo(&d, QPoint()).y() + l->height() / 2 - (cb->mapTo(&d, QPoint()).y() + cb->height() / 2)) <= 3
                    && l->mapTo(&d, QPoint()).x() > cb->mapTo(&d, QPoint()).x())
                    beside = true;
            notesBeside = notesBeside && beside;
        }
        CHECK(boxes == 7, "fixture: seven columns");
        CHECK(noPadding, "no column name padded with spaces");
        CHECK(notesBeside, "each has its note beside it, in its own column");
        CHECK(loose(&d).isEmpty(), "Export: layout audit");
    }

    // ---- Go to timestamp ------------------------------------------------------------------
    {
        const qint64 t0 = QDateTime::fromString(QStringLiteral("2026-06-27T14:02:26"), Qt::ISODate).toMSecsSinceEpoch();
        GotoTimestampDialog d(t0 + 60000, t0, t0 + 1800000);
        d.show();
        settle();
        CHECK(d.minimumSizeHint().width() <= 700 && d.minimumSizeHint().height() <= 300, "Go to timestamp stays small");
        CHECK(loose(&d).isEmpty(), "Go to timestamp: layout audit");
    }
}
