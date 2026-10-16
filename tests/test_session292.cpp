#include "testutil.h"
#include "flasherfakeboard.h"

#include "flashercore.h"
#include "flasherqueuepage.h"
#include "vcclistedit.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>

// =============================================================================
//  Session 292 — the Firmware Flasher's VCC list as rows: a tick box, the
//  address and a × per VCC, "+ Add VCC" (the next address), "Add range…",
//  wrong and duplicate rows marked, a summary of what is ticked. The rest
//  of the flasher still reads it as session 291's list text.
// =============================================================================

using namespace FlasherTest;

namespace {
void settle()
{
    for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
}  // namespace

TEST_SUITE(session292)
{
    // ---- the widget --------------------------------------------------------------------
    {
        VccListEdit list;
        int changes = 0;
        QObject::connect(&list, &VccListEdit::changed, [&]() { ++changes; });
        CHECK(list.rowCount() == 1 && list.text().isEmpty() && list.summary().isEmpty(),
              "starts as one empty row, no summary: the single-VCC flasher");

        list.edit(0)->setText(QStringLiteral("192.168.25.168"));
        CHECK(list.text() == QLatin1String("192.168.25.168") && changes > 0, "one address: its text, and changed()");

        list.addNext();
        CHECK(list.rowCount() == 2 && list.edit(1)->text() == QLatin1String("192.168.25.169"),
              "+ Add VCC adds the next address");
        list.addNext();
        CHECK(list.text() == QLatin1String("192.168.25.168, 192.168.25.169, 192.168.25.170"),
              "the rows read as session 291's list");
        list.show();
        settle();
        CHECK(list.summary() == QStringLiteral("3 of 3 VCCs ticked — flashed at once"), "the summary counts them");

        list.tick(1)->setChecked(false);
        CHECK(list.text() == QLatin1String("192.168.25.168, 192.168.25.170")
                  && list.summary() == QStringLiteral("2 of 3 VCCs ticked — flashed at once"),
              "an unticked VCC is left out of the run, and kept in the list");

        list.edit(2)->setText(QStringLiteral("192.168.25.168"));
        CHECK(list.rowIsMarked(2) && list.rowIsMarked(0) && list.edit(2)->toolTip().contains(QLatin1String("twice")),
              "the same VCC twice: both rows marked, the tooltip says why");
        list.edit(2)->setText(QStringLiteral("192.168.25"));
        CHECK(list.rowIsMarked(2) && !list.rowIsMarked(0) && list.edit(2)->toolTip().contains(QLatin1String("IPv4")),
              "a bad address marked");
        list.edit(2)->setText(QStringLiteral("192.168.25.168-170"));
        CHECK(list.rowIsMarked(2) && list.edit(2)->toolTip().contains(QLatin1String("Add range")),
              "a range typed into one row: pointed at Add range…");

        list.removeRow(2);
        settle();
        CHECK(list.rowCount() == 2, "× removes a row");

        QString error;
        CHECK(!list.addRange(QStringLiteral("192.168.25.180-187"), &error) && error.contains(QLatin1String("at most 8")),
              QByteArray("a range past eight is refused: ") + error.toUtf8());
        CHECK(list.addRange(QStringLiteral("192.168.25.180-182"), &error) && list.rowCount() == 5
                  && list.edit(4)->text() == QLatin1String("192.168.25.182"),
              "Add range adds a row per VCC");
        CHECK(!list.addRange(QStringLiteral("192.168.25"), &error), "a bad range refused");

        list.addRange(QStringLiteral("192.168.25.190-192"), &error);
        settle();
        auto *add = list.findChild<QPushButton *>(QStringLiteral("vccAddButton"));
        CHECK(list.rowCount() == 8 && add && !add->isEnabled(), "eight rows: + Add VCC is off");
        list.addNext();
        CHECK(list.rowCount() == 8, "and adds nothing");

        list.setText(QStringLiteral("10.0.0.5-7, 127.0.0.1:50012, oops"));
        settle();
        CHECK(list.rowCount() == 5 && list.edit(3)->text() == QLatin1String("127.0.0.1:50012"),
              "setText opens a range into rows and keeps ports");
        CHECK(list.edit(4)->text() == QLatin1String("oops") && list.rowIsMarked(4),
              "a profile's typo keeps its row, marked, not dropped");

        list.setText(QString());
        settle();
        CHECK(list.rowCount() == 1 && list.text().isEmpty(), "empty text: one empty row");
        list.edit(0)->setText(QStringLiteral("192.168.25.168"));
        list.removeRow(0);
        CHECK(list.rowCount() == 1 && list.edit(0)->text().isEmpty(), "× on the last row clears it");
    }

    // ---- in the queue page ---------------------------------------------------------------
    {
        QTemporaryDir temp;
        FlasherQueuePage page;
        Flasher::FlashProfile profile;
        profile.vccIp = QStringLiteral("192.168.25.168-171");
        profile.defaultImages.insert(Flasher::CardVcc, writeFile(temp.path(), QStringLiteral("LKAVACH_v1.2.9.appimage"),
                                                                 patternBytes(30 * 1450, 292)));
        page.applyProfile(profile);
        page.selectCard(Flasher::CardVcc);
        CHECK(page.vccList()->rowCount() == 4, "a profile's range: four rows");
        CHECK(page.batchEntries().size() == 4, "four VCCs in the batch");
        QPushButton *flash = nullptr;
        for (QPushButton *b : page.findChildren<QPushButton *>())
            if (b->text().startsWith(QLatin1String("Flash"))) flash = b;
        CHECK(flash && flash->text() == QLatin1String("Flash 4 VCCs"), "the button counts them");

        page.vccList()->tick(2)->setChecked(false);
        CHECK(page.batchEntries().size() == 3 && flash && flash->text() == QLatin1String("Flash 3 VCCs"),
              "unticking one: three in the batch, and the button says so");
        bool skipped = true;
        for (const Flasher::BatchEntry &entry : page.batchEntries())
            if (entry.targetIp == QLatin1String("192.168.25.170")) skipped = false;
        CHECK(skipped, "the unticked VCC is not flashed");

        page.vccList()->edit(0)->setText(QStringLiteral("192.168.25.169"));
        CHECK(!page.preflight().ready && page.preflight().firstBlocker.contains(QLatin1String("listed twice")),
              "a duplicate keeps Flash off, saying why");
    }
}
