#include "testutil.h"
#include <QSignalSpy>

#include "bignumberpanel.h"
#include "livefields.h"
#include "lococonfigcore.h"
#include "lococonfigmodel.h"
#include "lococonfigwindow.h"
#include "lococonsolewindow.h"
#include "logentry.h"
#include "settings.h"
#include "statuspins.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QUdpSocket>

// =============================================================================
//  Live data: big numbers, change highlighting, status-bar pins, and the
//  Loco Configuration window's check against what the loco reports.
//
//  Driven with REAL capture lines: the first two @lsrp lines of the first
//  replay file differ in TRAIN_SPEED (50 -> 45 km/h), LOCO_MODE
//  (2 Staff_Responsible -> 6 On_Sight) and Brake_Applied (0 -> 4).
// =============================================================================

namespace {

QStringList firstLsrpLines(int count)
{
    QStringList lines;
    const QStringList caps = QDir(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay"))
                                 .entryList({ QStringLiteral("*.cap") }, QDir::Files, QDir::Name);
    if (caps.isEmpty()) {
        return lines;
    }
    QFile file(QStringLiteral(DL_SRC_DIR) + QStringLiteral("/replay/") + caps.first());
    file.open(QIODevice::ReadOnly);
    while (!file.atEnd() && lines.size() < count) {
        const QString line = QString::fromLatin1(file.readLine()).trimmed();
        if (line.startsWith(QLatin1String("@lsrp"))) {
            lines.append(line);
        }
    }
    return lines;
}

void feed(LocoConsoleWindow &window, const QString &line)
{
    LogEntryPtr entry(new LogEntry);
    entry->text = line;
    QMetaObject::invokeMethod(&window, "onEntryAppended", Q_ARG(QString, QStringLiteral("1_1")),
                              Q_ARG(LogEntryPtr, entry));
    QMetaObject::invokeMethod(&window, "onRefreshTick");
}

int rowOf(QTableWidget *table, const QString &field)
{
    for (int row = 0; table && row < table->rowCount(); ++row) {
        if (table->item(row, 0) && table->item(row, 0)->text().trimmed() == field) {
            return row;
        }
    }
    return -1;
}

QString linfoLine(const QString &key, const QByteArray &body)
{
    return QStringLiteral("@linfo_%1 2026-09-25T10:00:00 1 %2").arg(key, QString::fromLatin1(body.toHex()));
}

}  // namespace

// =============================================================================
TEST_SUITE(livefields)
{
    const LiveFieldRef speed{ QStringLiteral("Speed"), { QStringLiteral("lsrp:TRAIN_SPEED"), QStringLiteral("arp:TRAIN_SPEED") } };
    const LiveFieldRef back = LiveFieldRef::parse(speed.serialise());
    CHECK(back.label == speed.label && back.sources == speed.sources, "a field reference round-trips through settings text");

    CapType type = CapType::Unknown;
    QString field;
    CHECK(LiveFields::parseSource(QStringLiteral("lsrp:TRAIN_SPEED"), &type, &field)
              && type == CapType::LSRP && field == QStringLiteral("TRAIN_SPEED"),
          "a source names a capture type and a field");
    CHECK(!LiveFields::parseSource(QStringLiteral("nonsense"), &type, &field), "a malformed source is refused");
    bool roundTrips = true;
    for (CapType each : { CapType::LSRP, CapType::ARP, CapType::Linfo, CapType::NmsHlth, CapType::Dip1 }) {
        CapType got = CapType::Unknown;
        QString gotField;
        if (!LiveFields::parseSource(LiveFields::sourceFor(each, QStringLiteral("X")), &got, &gotField) || got != each) {
            roundTrips = false;
        }
    }
    CHECK(roundTrips, "every type's source text reads back as that type (including 'nms hlth')");

    const QVector<FieldRow> rows{ { QStringLiteral("  TRAIN_SPEED"), QStringLiteral("50 km/h") } };
    CHECK(LiveFields::valueIn(rows, QStringLiteral("TRAIN_SPEED")) == QStringLiteral("50 km/h"),
          "fields are found despite the indentation of nested rows");
    CHECK(LiveFields::valueIn(rows, QStringLiteral("LOCO_MODE")).isNull(), "a missing field is null, not empty");
    CHECK(LiveFields::frameClock(QStringLiteral("59357")) == QStringLiteral("16:29:16"),
          "FRAME_NUM 59357 is 16:29:16 (seconds since midnight + 1)");
    CHECK(LiveFields::frameClock(QStringLiteral("1")) == QStringLiteral("00:00:00")
              && LiveFields::frameClock(QStringLiteral("86400")) == QStringLiteral("23:59:59"),
          "the ends of the day");
    CHECK(LiveFields::frameClock(QStringLiteral("0")).isEmpty() && LiveFields::frameClock(QStringLiteral("x")).isEmpty(),
          "not a frame number: no clock");
    const QVector<LiveFieldRef> defaults = LiveFields::defaultBigNumbers();
    CHECK(defaults.size() == 4 && defaults.at(0).label == QStringLiteral("Speed") && defaults.at(3).label == QStringLiteral("Brake"),
          "the default tiles: Speed, Mode, Frame clock, Brake");
}

// =============================================================================
TEST_SUITE(lococonsolelive)
{
    const QStringList lines = firstLsrpLines(2);
    CHECK(lines.size() == 2, "two real @lsrp lines from the replay files");
    if (lines.size() < 2) {
        return;
    }
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("lococonsole/bigNumbers"));
        settings.setValue(QStringLiteral("lococonsole/bigNumbersShown"), false);
        settings.setValue(QStringLiteral("lococonsole/highlightChanges"), true);
    }

    LocoConsoleWindow window(nullptr);
    window.resize(1000, 700);
    window.show();
    QApplication::processEvents();

    QPushButton *bigButton = nullptr;
    for (QPushButton *button : window.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Big numbers")) {
            bigButton = button;
        }
    }
    CHECK(bigButton != nullptr && !window.bigNumbers()->isVisible(), "big numbers are off until asked for");
    if (bigButton) {
        bigButton->click();
    }
    CHECK(window.bigNumbers()->isVisible(), "the Big numbers button shows them");

    feed(window, lines.at(0));
    BigNumberPanel *big = window.bigNumbers();
    CHECK(big->valueTextAt(0) == QStringLiteral("50 km/h"), "Speed: 50 km/h, from the real frame");
    CHECK(big->valueTextAt(1) == QStringLiteral("2 (Staff_Responsible)"), "Mode: 2 (Staff_Responsible)");
    CHECK(big->valueTextAt(2) == QStringLiteral("16:29:16") && big->detailTextAt(2).contains(QStringLiteral("FRAME_NUM 59357")),
          "Frame clock: 16:29:16, with the raw FRAME_NUM underneath");
    CHECK(big->valueTextAt(3) == QStringLiteral("0"), "Brake: the raw Brake_Applied value");
    CHECK(!big->isStaleAt(0), "fresh values are not muted");

    // ---- change highlighting ------------------------------------------------------
    QTableWidget *lsrp = window.tableForLabel(QStringLiteral("lsrp"));
    const int speedRow = rowOf(lsrp, QStringLiteral("TRAIN_SPEED"));
    CHECK(speedRow >= 0, "the lsrp table has TRAIN_SPEED");
    CHECK(!window.isRowHighlighted(QStringLiteral("lsrp"), speedRow), "the first frame marks nothing (there is no 'before')");

    feed(window, lines.at(1));
    CHECK(big->valueTextAt(0) == QStringLiteral("45 km/h") && big->valueTextAt(1) == QStringLiteral("6 (On_Sight)"),
          "the tiles follow the next frame");
    const int speedRowNow = rowOf(lsrp, QStringLiteral("TRAIN_SPEED"));
    CHECK(window.isRowHighlighted(QStringLiteral("lsrp"), speedRowNow), "a field that changed is highlighted");
    CHECK(lsrp->item(speedRowNow, 1)->font().bold(), "and in bold while fresh");
    int unchangedRow = -1;
    const QVector<FieldRow> first = CaptureDecoder::describe(CaptureDecoder::parseLine(lines.at(0)));
    for (int row = 0; row < lsrp->rowCount() && unchangedRow < 0; ++row) {
        const QString field = lsrp->item(row, 0)->text().trimmed();
        if (!field.isEmpty() && LiveFields::valueIn(first, field) == lsrp->item(row, 1)->text()
            && rowOf(lsrp, field) == row) {
            unchangedRow = row;
        }
    }
    CHECK(unchangedRow >= 0 && !window.isRowHighlighted(QStringLiteral("lsrp"), unchangedRow),
          "a field that did not change is not");

    QCheckBox *highlight = nullptr;
    for (QCheckBox *box : window.findChildren<QCheckBox *>()) {
        if (box->text() == QStringLiteral("Highlight changes")) {
            highlight = box;
        }
    }
    CHECK(highlight != nullptr, "the Highlight changes switch is there");
    if (highlight) {
        highlight->setChecked(false);
        CHECK(!window.isRowHighlighted(QStringLiteral("lsrp"), speedRowNow), "switched off: no marks");
        highlight->setChecked(true);
    }

    // ---- big numbers are configurable ---------------------------------------------
    big->addTile({ QStringLiteral("Seq"), { QStringLiteral("lsrp:FRAME_NUM") } });
    CHECK(big->tiles().size() == 5, "a tile can be added");
    CHECK(BigNumberPanel::loadSaved().size() == 5, "and the tile set is remembered");
    big->setTiles(LiveFields::defaultBigNumbers());
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        CHECK(settings.value(QStringLiteral("lococonsole/bigNumbersShown")).toBool(), "showing them is remembered too");
        settings.remove(QStringLiteral("lococonsole/bigNumbers"));
        settings.setValue(QStringLiteral("lococonsole/bigNumbersShown"), false);
    }
}

// =============================================================================
TEST_SUITE(statuspins)
{
    const QStringList lines = firstLsrpLines(1);
    CHECK(!lines.isEmpty(), "a real @lsrp line");
    if (lines.isEmpty()) {
        return;
    }
    {
        QSettings settings(Settings::iniPath(), QSettings::IniFormat);
        settings.remove(QStringLiteral("ui/statusPins"));
    }
    QWidget host;
    {
        StatusPins pins(&host);
        CHECK(StatusPins::instance() == &pins, "the pins are reachable from the Loco Console");
        CHECK(pins.addPin(QStringLiteral("1_1"), { QStringLiteral("Mode"), { QStringLiteral("lsrp:LOCO_MODE") } }),
              "a field can be pinned");
        CHECK(pins.textAt(0) == QStringLiteral("1_1 · Mode: —") && pins.isStaleAt(0),
              "before any frame it shows — and is muted");
        pins.observeLine(lines.at(0));
        CHECK(pins.textAt(0) == QStringLiteral("1_1 · Mode: 2 (Staff_Responsible)") && !pins.isStaleAt(0),
              "a frame from that loco fills it in, live");

        QString otherLoco = lines.at(0);
        otherLoco.replace(0, otherLoco.indexOf(QLatin1Char(' ')), QStringLiteral("@lsrp_9_9"));
        CHECK(pins.addPin(QStringLiteral("1_1"), { QStringLiteral("Frame"), { QStringLiteral("lsrp:FRAME_NUM") } }),
              "a second pin");
        pins.observeLine(lines.at(0));
        CHECK(pins.textAt(1) == QStringLiteral("1_1 · Frame: 16:29:16"), "FRAME_NUM is shown as a clock");
        CHECK(!pins.addPin(QStringLiteral("1_1"), { QStringLiteral("Mode"), { QStringLiteral("lsrp:LOCO_MODE") } }),
              "the same pin twice is refused");
        int added = pins.count();
        while (pins.addPin(QStringLiteral("2_%1").arg(added), { QStringLiteral("Mode"), { QStringLiteral("lsrp:LOCO_MODE") } })) {
            ++added;
        }
        CHECK(pins.count() == StatusPins::kMaxPins, "at most six pins");
        pins.removePin(pins.count() - 1);
        CHECK(pins.count() == StatusPins::kMaxPins - 1, "✕ removes one");
    }
    {
        StatusPins reloaded(&host);
        CHECK(reloaded.count() == StatusPins::kMaxPins - 1 && reloaded.textAt(0).startsWith(QStringLiteral("1_1 · Mode")),
              "pins are remembered between runs");
    }
    QSettings settings(Settings::iniPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("ui/statusPins"));
}

// =============================================================================
TEST_SUITE(locoinfolivecheck)
{
    QTemporaryDir temp;
    QUdpSocket vcc;
    CHECK(vcc.bind(QHostAddress(QHostAddress::LocalHost), 0), "a stand-in VCC");
    LocoConfigWindow window(nullptr, temp.path());
    window.setTarget(QStringLiteral("127.0.0.1"), vcc.localPort());
    CHECK(window.liveCheckText().contains(QStringLiteral("no @linfo")), "nothing from the loco yet: says so");

    // Never sent from here: compare with the configuration itself.
    const QByteArray original = window.currentBody();
    window.observeCaptureLine(linfoLine(QStringLiteral("7_1"), original));
    CHECK(window.liveCheckText().startsWith(QStringLiteral("✓ Loco 7_1 holds exactly this configuration")),
          "the loco reports exactly this configuration");

    LocoFieldModel *model = window.fieldModel();
    model->setData(model->index(model->rowForKey(QStringLiteral("shunt_speed")), LocoFieldModel::ColumnValue),
                   QStringLiteral("20"), Qt::EditRole);
    CHECK(window.liveCheckText().contains(QStringLiteral("differs from this configuration in 1 field")),
          "after an edit here: 1 field differs, not sent yet");

    // Sent: the @linfo from BEFORE the send must not be reported as a failure.
    QThread::msleep(5);
    QSignalSpy verifiedSpy(&window, &LocoConfigWindow::verified);
    CHECK(window.sendNow(false), "sent");
    CHECK(window.liveCheckText().startsWith(QStringLiteral("Waiting for loco 7_1's next @linfo")),
          "right after sending: waiting for the loco's next @linfo");

    const QByteArray sent = window.currentBody();
    window.observeCaptureLine(linfoLine(QStringLiteral("7_1"), sent));
    CHECK(window.liveCheckText().startsWith(QStringLiteral("✓ Loco 7_1 holds exactly what was last sent")),
          "the loco's next @linfo matches the send: confirmed");

    // Session 81: the confirmation is recorded with the send, once.
    {
        LocoInfo::SendHistory history(window.historyPath());
        const QList<LocoInfo::Verification> vs = history.readVerifications();
        const QList<LocoInfo::SendRecord> sends = history.readAll();
        CHECK(vs.size() == 1 && vs.first().match && vs.first().locoKey == QLatin1String("7_1"),
              "the confirmation is written to the send history");
        CHECK(!sends.isEmpty() && qAbs(vs.first().sentAt.msecsTo(sends.last().when)) < 1000,
              "against the send it confirms");
        int skipped = -1;
        history.readAll(&skipped);
        CHECK(skipped == 0, "a verification line is not counted as a damaged send");
        CHECK(verifiedSpy.count() == 1 && verifiedSpy.first().at(1).toBool(),
              "and announced once, for the main window's notification");
        window.observeCaptureLine(linfoLine(QStringLiteral("7_1"), sent));
        CHECK(LocoInfo::SendHistory(window.historyPath()).readVerifications().size() == 1 && verifiedSpy.count() == 1,
              "a second @linfo for the same send is not recorded again");
    }

    LocoInfo::Layout layout;
    QString error;
    layout.load(QStringLiteral(":/schema/kavach.xml"), &error);
    LocoInfo::Parsed parsed = LocoInfo::parseBody(layout, sent);
    parsed.values.insert(QStringLiteral("frame_cycle"), static_cast<qint64>(7));
    const QByteArray other = LocoInfo::encodeBody(layout, parsed.values);
    window.observeCaptureLine(linfoLine(QStringLiteral("7_1"), other));
    CHECK(window.liveCheckText().startsWith(QStringLiteral("✗ Loco 7_1 holds something else: 1 field differ")),
          "a later @linfo that differs: flagged, with the count");

    // Session 81: a send the loco answers with something else is recorded as such.
    {
        window.observeCaptureLine(linfoLine(QStringLiteral("7_1"), other));   // a valid one, from before
        QThread::msleep(5);
        CHECK(window.sendNow(false), "sent again");
        window.setConfirmMinWaitMs(0);
        // Past three of this loco's own intervals (in this test, the few ms
        // between the @linfo lines fed above).
        QThread::msleep(400);
        window.recheck();   // no frame: only the clock has moved
        CHECK(window.liveCheckText().startsWith(QStringLiteral("⚠ No @linfo from loco 7_1")),
              "overdue: the check says no @linfo since the send, and why that may be");
        window.setConfirmMinWaitMs(LocoConfigWindow::kConfirmMinWaitMs);
        window.observeCaptureLine(linfoLine(QStringLiteral("7_1"), other));
        const QList<LocoInfo::Verification> vs = LocoInfo::SendHistory(window.historyPath()).readVerifications();
        CHECK(vs.size() == 2 && !vs.last().match && vs.last().differences.size() == 1
                  && vs.last().differences.first().startsWith(QLatin1String("frame_cycle")),
              "the loco's differing answer is recorded, naming the field");
        CHECK(verifiedSpy.count() == 2 && !verifiedSpy.last().at(1).toBool(), "and announced as a mismatch");
    }

    parsed.values.insert(QStringLiteral("loco_unit_id"), static_cast<qint64>(99999));
    window.observeCaptureLine(linfoLine(QStringLiteral("8_1"), LocoInfo::encodeBody(layout, parsed.values)));
    CHECK(window.liveCheckText().contains(QStringLiteral("Loco 7_1")),
          "another loco (another loco_unit_id) does not count against this one");

    QByteArray damaged = sent;
    damaged[20] = static_cast<char>(damaged[20] ^ 0x01);
    window.observeCaptureLine(linfoLine(QStringLiteral("7_1"), damaged));
    CHECK(window.liveCheckText().contains(QStringLiteral("does not match its contents")),
          "a damaged @linfo (bad loco_info_crc) is called out, not compared");
}
