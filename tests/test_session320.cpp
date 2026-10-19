#include "testutil.h"

#include "capturedecoder.h"
#include "lococonfigcore.h"
#include "lococonfigmodel.h"
#include "lococonfigwindow.h"
#include "messagedispatcher.h"
#include "udpcommunication.h"
#include "uicolors.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QLabel>
#include <QTemporaryDir>
#include <QThread>
#include <QUdpSocket>

// =============================================================================
//  Session 320 — Loco Configuration compares the editor with what each send
//  IP's VCC holds.
//
//  Abhinav: "compare configuration that is in loco configuration window and
//  that is arriving from the IPs entered and highlight where they are
//  different". The receiver now keeps each datagram's sender address; the
//  table has a column per ticked send IP with that VCC's latest @linfo, and
//  the cells that differ from the editor's value are highlighted.
//
//  Real @linfo: replay/loco_81_1_14102026_110915.cap (loco 1, 2026-10-14).
// =============================================================================

namespace {

QString firstLinfo()
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_81_1_14102026_110915.cap"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.startsWith("@linfo_")) return QString::fromLatin1(l);
    }
    return QString();
}

quint32 ipv4(const char *text) { return QHostAddress(QLatin1String(text)).toIPv4Address(); }

}  // namespace

TEST_SUITE(session320)
{
    const QString line = firstLinfo();
    CHECK(!line.isEmpty(), "fixture: a real @linfo line");
    if (line.isEmpty()) return;

    // ---- the dispatcher keeps each tab's sender -------------------------------------
    {
        MessageDispatcher d;
        d.ingestLocal(81, 1, line.toLatin1(), 1000, QString(), ipv4("192.168.25.16"));
        d.ingestLocal(82, 1, QByteArray("@dmi_2_1 2026-10-14T11:10:23 1 00"), 1000, QString());
        d.drainNow();
        CHECK(d.senderIp(QStringLiteral("81_1")) == QStringLiteral("192.168.25.16"), "a tab's sender is kept");
        CHECK(d.senderIp(QStringLiteral("82_1")).isEmpty(), "unknown when no address came with it");
    }

    // ---- a real datagram: the receiver reads the sender's address --------------------
    {
        MessageDispatcher d;
        UDPCommunication recv(50320, 50000, nullptr);
        d.attachReceiver(&recv);
        bool bound = false;
        QObject::connect(&recv, &UDPCommunication::bindSucceeded, [&](quint16) { bound = true; });
        recv.start();
        QElapsedTimer clock;
        clock.start();
        while (!bound && clock.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(bound, "the receiver bound");
        QUdpSocket vcc;
        const QByteArray body = line.toLatin1();
        QByteArray datagram;
        datagram.append(char(81)).append(char(101)).append(char(7));                    // src, dst 101, msg
        datagram.append(char(body.size() & 0xFF)).append(char((body.size() >> 8) & 0xFF));   // len, LE
        datagram.append(char(1)).append(char(0));                                         // kvch 1, LE
        datagram.append(body);
        vcc.writeDatagram(datagram, QHostAddress::LocalHost, 50320);
        while (d.senderIp(QStringLiteral("81_1")).isEmpty() && clock.elapsed() < 6000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            d.drainNow();
        }
        CHECK(d.senderIp(QStringLiteral("81_1")) == QStringLiteral("127.0.0.1"), "the datagram's sender is recorded");
        recv.quit();
        recv.wait(2000);
    }

    // ---- the window: a column per ticked send IP ------------------------------------
    QTemporaryDir temp;
    MessageDispatcher d;
    LocoConfigWindow window(nullptr, temp.path());
    window.setLiveSource(&d);
    LocoInfo::SendTarget a, b, c;
    a.ip = QStringLiteral("192.168.25.16");
    b.ip = QStringLiteral("192.168.25.102");
    c.ip = QStringLiteral("192.168.25.103");
    c.enabled = false;
    window.setTargets({ a, b, c });
    LocoFieldModel *model = window.fieldModel();
    CHECK(model->liveColumnCount() == 2, "two ticked IPs, two columns (the unticked one has none)");
    CHECK(model->headerData(LocoFieldModel::ColumnCount, Qt::Horizontal, Qt::DisplayRole).toString()
              == QStringLiteral("VCC 192.168.25.16"),
          "headed by the IP");
    CHECK(window.liveCompareText().isEmpty()
              && model->index(0, LocoFieldModel::ColumnCount).data(Qt::DisplayRole).toString() == QStringLiteral("—"),
          "nothing yet: the columns show a dash, and no summary line takes the table's room");

    // The VCC at .16 prints its LOCO_INFO.
    d.ingestLocal(81, 1, line.toLatin1(), 1000, QString(), ipv4("192.168.25.16"));
    d.drainNow();

    // What it holds, decoded on its own, against the editor field by field.
    LocoInfo::Layout layout;
    QString error;
    layout.load(QStringLiteral(":/schema/kavach.xml"), &error);
    const LocoInfo::Parsed held = LocoInfo::parseBody(layout, CaptureDecoder::parseLine(line).bytes);
    CHECK(held.ok, "fixture: the @linfo decodes with this layout");
    int expected = 0;
    QString aSame, aDifferent;
    for (const LocoInfo::Field &field : layout.fields()) {
        if (field.isCrc) continue;
        const bool same = LocoInfo::sameValue(field, model->values().value(field.key), held.values.value(field.key));
        expected += same ? 0 : 1;
        if (same && aSame.isEmpty()) aSame = field.key;
        if (!same && aDifferent.isEmpty()) aDifferent = field.key;
    }
    CHECK(model->liveDifferences(0) == expected, QByteArray("the differing fields counted (")
                                                     + QByteArray::number(model->liveDifferences(0)) + " of "
                                                     + QByteArray::number(expected) + ")");
    CHECK(model->liveDifferences(1) == 0, "the IP with no @linfo yet counts none");
    const QString summary = window.liveCompareText();
    CHECK(summary.contains(QStringLiteral("192.168.25.102: no @linfo yet"))
              && (expected == 0 ? summary.contains(QStringLiteral("192.168.25.16: holds this configuration"))
                                : summary.contains(QStringLiteral("192.168.25.16: %1 field").arg(expected))),
          QByteArray("the summary line (") + summary.toUtf8() + ")");

    // Make one field differ on purpose (or use one that already does).
    if (aDifferent.isEmpty() && !aSame.isEmpty()) {
        const int row = model->rowForKey(aSame);
        const QVariant now = model->values().value(aSame);
        model->setData(model->index(row, LocoFieldModel::ColumnValue), QString::number(now.toLongLong() + 1), Qt::EditRole);
        aDifferent = aSame;
        ++expected;
    }
    const int row = model->rowForKey(aDifferent);
    const QModelIndex cell = model->index(row, LocoFieldModel::ColumnCount);
    CHECK(cell.data(Qt::BackgroundRole).value<QColor>() == UiColor::bannerBg(), "a differing cell is highlighted");
    CHECK(cell.data(Qt::ToolTipRole).toString().startsWith(QStringLiteral("192.168.25.16 holds")), "and says what the VCC holds");
    CHECK(model->index(row, 0).data(LocoFieldModel::DiffersFromLiveRole).toBool(), "the row is marked for the group");
    const QModelIndex none = model->index(row, LocoFieldModel::ColumnCount + 1);
    CHECK(none.data(Qt::DisplayRole).toString() == QStringLiteral("—") && !none.data(Qt::BackgroundRole).isValid(),
          "the IP with no @linfo shows a dash, not highlighted");

    // Unticked: its column goes.
    b.enabled = false;
    window.setTargets({ a, b, c });
    CHECK(model->liveColumnCount() == 1, "unticking an IP removes its column");
    window.setTargets({});
    CHECK(model->liveColumnCount() == 0 && window.liveCompareText().isEmpty(), "no targets: no columns, no line");
}
