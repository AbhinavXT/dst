#include "testutil.h"

#include "filterbar.h"
#include "findbar.h"
#include "logmodel.h"
#include "settings.h"
#include "theme.h"
#include "uistyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTableView>
#include <QVBoxLayout>

// =============================================================================
//  Session 258 — glyph buttons drawn as empty boxes. The general button rule
//  pads 14 px a side; the docked find bar's ◀ ▶ ✕ ⧉ (24-28 px) and the
//  filter bar's ✕ (24 px) had no width left for the glyph. And the docked
//  find bar in a narrow Compare pane squeezed every control to a sliver.
// =============================================================================

namespace {
// The width the style leaves for the label, against the label's own width.
bool glyphFits(QPushButton *b, QByteArray *why)
{
    QStyleOptionButton opt;
    opt.initFrom(b);
    opt.text = b->text();
    const QRect c = b->style()->subElementRect(QStyle::SE_PushButtonContents, &opt, b);
    const int need = QFontMetrics(b->font()).horizontalAdvance(b->text());
    *why += b->objectName().toUtf8() + " '" + b->text().toUtf8() + "' " + QByteArray::number(c.width()) + " for "
            + QByteArray::number(need) + "; ";
    return !b->text().isEmpty() && c.width() >= need && b->width() + 1 >= b->sizeHint().width();
}
}  // namespace

TEST_SUITE(session258)
{
    const bool savedPref = Settings::findDetached();
    Settings::setFindDetached(false);
    ThemeUtil::apply(Theme::Light);
    UiStyle::apply();

    LogModel model(nullptr, 1000);
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    // A pane placed by hand, as a Compare pane is by its splitter: its width
    // is given, not negotiated from the find bar's minimum.
    QWidget page;
    page.resize(1200, 500);
    auto *pane = new QWidget(&page);
    pane->setGeometry(0, 0, 1200, 500);
    auto *lay = new QVBoxLayout(pane);
    QWidget filterHost;                      // its own host: its minimum width is not the find bar's
    auto *filter = new FilterBar(&model, &filterHost);
    (new QVBoxLayout(&filterHost))->addWidget(filter);
    filterHost.show();
    auto *view = new QTableView(pane);
    view->setModel(&proxy);
    auto *bar = new FindBar(view, pane);
    lay->addWidget(bar);
    lay->addWidget(view);
    page.show();
    bar->activate();
    QCoreApplication::processEvents();
    CHECK(!bar->isDetached() && bar->isVisible(), "the find bar opens docked");

    const QStringList glyphs{ QStringLiteral("findPrevBtn"), QStringLiteral("findNextBtn"),
                              QStringLiteral("findCloseBtn"), QStringLiteral("findDetachBtn") };
    {
        bool fit = true;
        QByteArray why;
        for (const QString &name : glyphs) {
            auto *b = bar->findChild<QPushButton *>(name);
            fit = fit && b && glyphFits(b, &why);
        }
        CHECK(fit, "docked: ◀ ▶ ✕ ⧉ each have room for their glyph: " + why);
    }
    {
        QPushButton *clear = nullptr;
        for (QPushButton *b : filter->findChildren<QPushButton *>())
            if (b->toolTip() == QLatin1String("Clear filter")) clear = b;
        QByteArray why;
        CHECK(clear && glyphFits(clear, &why), "the filter bar's ✕ has room for its glyph: " + why);
    }
    auto *modeBox = bar->findChild<QComboBox *>(QStringLiteral("findModeBox"));
    auto *allTabs = bar->findChild<QPushButton *>(QStringLiteral("findAllTabsBtn"));
    CHECK(modeBox && modeBox->isVisible() && allTabs && allTabs->isVisible(), "wide: the mode box and All tabs are shown");

    // A Compare pane's width.
    pane->resize(540, 500);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    CHECK(modeBox && !modeBox->isVisible() && allTabs && !allTabs->isVisible(),
          "narrow: the mode box and All tabs step out (they are in the ⧉ window); bar " + QByteArray::number(bar->width())
              + " hint " + QByteArray::number(bar->sizeHint().width()));
    {
        bool fit = true;
        QByteArray why;
        for (QWidget *w : bar->findChildren<QWidget *>()) {
            if (!w->isVisible() || w->parentWidget() != bar) continue;
            if (!qobject_cast<QCheckBox *>(w) && !qobject_cast<QPushButton *>(w)) continue;
            if (w->width() + 1 < w->sizeHint().width()) {
                fit = false;
                why += w->objectName().toUtf8() + " " + QByteArray::number(w->width()) + "<"
                       + QByteArray::number(w->sizeHint().width()) + "; ";
            }
        }
        CHECK(fit, "narrow: no button or tick box is squeezed below its size " + why);
        auto *edit = bar->findChild<QLineEdit *>();
        CHECK(edit && edit->width() >= 120, "narrow: the input keeps 120 px at least");
    }

    // The window form: worded buttons with the general padding back.
    bar->setDetached(true);
    QCoreApplication::processEvents();
    {
        auto *prev = bar->findChild<QPushButton *>(QStringLiteral("findPrevBtn"));
        CHECK(prev && prev->text() == QLatin1String("Previous") && prev->property("dlRole").toString().isEmpty(),
              "undocked: Previous is a worded button, not a glyph one");
        CHECK(modeBox && modeBox->isVisible() && allTabs && allTabs->isVisible(),
              "undocked: the mode box and All tabs are back");
    }
    bar->setDetached(false);
    Settings::setFindDetached(savedPref);
}
