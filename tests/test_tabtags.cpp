#include "testutil.h"

#include "logmodel.h"
#include "minimizeddock.h"
#include "settings.h"
#include "tabpopoutwindow.h"
#include "tabtags.h"
#include "theme.h"
#include "uicolors.h"

#include <QApplication>
#include <QHeaderView>
#include <QMainWindow>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>

// =============================================================================
//  Tab colour tags, and the pop-out window that shows them.
// =============================================================================

TEST_SUITE(tabtags)
{
    TabTags *tags = TabTags::instance();
    const QString key = QStringLiteral("99_7");   // a key no real test traffic uses
    tags->setTag(key, TabTag());

    QSignalSpy changed(tags, &TabTags::changed);
    tags->setColor(key, 3);
    CHECK(tags->tag(key).color == 3, "a colour tag is stored");
    CHECK(changed.count() == 1 && changed.first().first().toString() == key, "and announced for that key");
    CHECK(!tags->dotIcon(key).isNull(), "a tagged key has a dot");

    tags->setLabel(key, QStringLiteral("  Brake|test and a very long label indeed  "));
    CHECK(tags->tag(key).color == 3, "setting a label keeps the colour");
    CHECK(tags->tag(key).label.size() <= TabTags::kMaxLabelLength && !tags->tag(key).label.contains(QLatin1Char('|')),
          "labels are trimmed, capped and cannot break the stored form");
    tags->setLabel(key, QStringLiteral("Brake test"));
    CHECK(tags->decoratedName(key, QStringLiteral("7_1")) == QStringLiteral("7_1 · Brake test"), "the label follows the name");

    tags->setColor(key, -1);
    CHECK(tags->dotIcon(key).isNull() && tags->tag(key).label == QStringLiteral("Brake test"),
          "no colour: no dot, label kept");
    tags->setLabel(key, QString());
    CHECK(!tags->tag(key).isSet(), "no colour and no label: nothing stored");
    CHECK(tags->decoratedName(key, QStringLiteral("7_1")) == QStringLiteral("7_1"), "and the name is plain again");

    // The tag colours: dots, so 3:1 (WCAG, graphical objects) against the
    // window and the base, in every theme; and eight different colours.
    const QPalette saved = qApp->palette();
    for (Theme t : ThemeUtil::all()) {
        ThemeUtil::apply(t);
        const QPalette p = qApp->palette();
        double worst = 99.0;
        QSet<QRgb> distinct;
        for (int colour = 0; colour < UiColor::kTagCount; ++colour) {
            const QColor c = UiColor::tagColor(colour);
            worst = qMin(worst, UiColor::contrastRatio(c, p.color(QPalette::Window)));
            worst = qMin(worst, UiColor::contrastRatio(c, p.color(QPalette::Base)));
            distinct.insert(c.rgb());
        }
        CHECK(worst >= 3.0, QStringLiteral("%1: every tag colour is at least 3:1 (worst %2:1)")
                                .arg(QString::fromLatin1(ThemeUtil::toString(t))).arg(worst, 0, 'f', 2)
                                .toUtf8().constData());
        CHECK(distinct.size() == UiColor::kTagCount,
              QStringLiteral("%1: eight different tag colours").arg(QString::fromLatin1(ThemeUtil::toString(t)))
                  .toUtf8().constData());
    }
    qApp->setPalette(saved);
    ThemeUtil::apply(Theme::Light);
}

TEST_SUITE(tabpopout)
{
    TabTags *tags = TabTags::instance();
    const QString key = QStringLiteral("99_8");
    tags->setTag(key, TabTag());

    QMainWindow main;
    main.show();
    LogModel model;
    auto *popout = new TabPopoutWindow(key, QStringLiteral("Loco 99"), &model, nullptr, &main);
    popout->show();
    QApplication::processEvents();

    CHECK(MinimizedDock::isToolWindow(popout), "a pop-out is a tool window: it closes with the main window and gets a minimise chip");
    CHECK(popout->windowTitle() == QStringLiteral("Loco 99"), "titled after the tab");
    CHECK(popout->view()->model() == &model, "it shows the tab's own model: a live copy, never out of step");
    CHECK(popout->view()->verticalHeader()->defaultSectionSize() == Settings::rowHeightFor(Settings::rowDensity()),
          "rows follow the Row density setting (the old detach window used a fixed 20 px)");

    tags->setColor(key, 5);
    tags->setLabel(key, QStringLiteral("Night run"));
    CHECK(popout->windowTitle() == QStringLiteral("Loco 99 · Night run"), "a tag label reaches the title at once");
    CHECK(!popout->windowIcon().isNull(), "and the tag colour its icon (which the minimise chip shows)");

    QSignalSpy showInMain(popout, &TabPopoutWindow::showInMainRequested);
    for (QPushButton *button : popout->findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Show in main window")) {
            button->click();
        }
    }
    CHECK(showInMain.count() == 1 && showInMain.first().first().toString() == key, "Show in main window asks for this tab");

    popout->setFollowLatest(true);
    CHECK(popout->followLatest(), "Follow latest can be set (from the tab's scroll lock)");

    QSignalSpy closed(popout, &TabPopoutWindow::closedByUser);
    popout->close();
    CHECK(closed.count() == 1 && closed.first().first().toString() == key, "closing it tells the main window, so it is not reopened next time");

    tags->setTag(key, TabTag());
    delete popout;
}
