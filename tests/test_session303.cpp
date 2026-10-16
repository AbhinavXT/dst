#include "testutil.h"

#include "capturedecoder.h"
#include "fielddocs.h"
#include "lococonsolewindow.h"
#include "logmodel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTableWidget>

// =============================================================================
//  Session 303 — the Live Loco Console shows the annexures' field descriptions.
//
//  Each packet tab's rows carry Annexure C's (radio packets) or Annexure D's
//  (RFID tags) description of the field as the tooltip of both cells, kept
//  across the console's refresh ticks; the capture's own rows and packets
//  the annexures do not cover have none. Real frames from replay/, fed as
//  the dispatcher feeds them (session 168's way).
// =============================================================================

namespace {

QString nthOf303(const QByteArray &type, int n)
{
    QFile f(QStringLiteral(DL_SRC_DIR "/replay/loco_1_1_27062026_140226.cap"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    int seen = 0;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.startsWith("@" + type + "_") && seen++ == n) return QString::fromLatin1(l);
    }
    return QString();
}

QString tip303(QTableWidget *table, const QString &name, int column = 0)
{
    if (!table) return QStringLiteral("<no table>");
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0) && table->item(row, 0)->text() == name)
            return table->item(row, column) ? table->item(row, column)->toolTip() : QString();
    }
    return QStringLiteral("<no row>");
}

}  // namespace

TEST_SUITE(session303)
{
    LocoConsoleWindow w(nullptr);
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    auto feed = [&w](const QString &line) {
        LogEntryPtr e(new LogEntry);
        e->text = line;
        e->epochMs = QDateTime::currentMSecsSinceEpoch();
        QMetaObject::invokeMethod(&w, "onEntryAppended", Q_ARG(QString, QStringLiteral("1_1")), Q_ARG(LogEntryPtr, e));
        QMetaObject::invokeMethod(&w, "onRefreshTick");
    };
    const QString slrp = nthOf303("slrp", 5), lsrp = nthOf303("lsrp", 5), rfid = nthOf303("rfid", 0);
    CHECK(!slrp.isEmpty() && !lsrp.isEmpty() && !rfid.isEmpty(), "real SLRP, LSRP and RFID lines");
    feed(slrp);
    feed(lsrp);
    feed(rfid);

    QTableWidget *slrpTable = w.tableForLabel(QString::fromLatin1(CaptureDecoder::typeLabel(CapType::SLRP)));
    QTableWidget *lsrpTable = w.tableForLabel(QString::fromLatin1(CaptureDecoder::typeLabel(CapType::LSRP)));
    QTableWidget *rfidTable = w.tableForLabel(QString::fromLatin1(CaptureDecoder::typeLabel(CapType::Rfid)));
    CHECK(slrpTable && lsrpTable && rfidTable, "the SLRP, LSRP and RFID tabs");
    CHECK(slrpTable && slrpTable->horizontalHeaderItem(0)->toolTip().contains(QLatin1String("Annexure D")),
          "the Field header says the rows explain themselves");

    const QString ma = tip303(slrpTable, QStringLiteral("  MA_W_R_T_SIG"));
    CHECK(ma.contains(QLatin1String("<b>MA_W_R_T_SIG</b>")) && ma.contains(QLatin1String("65535: Unknown"))
              && ma.contains(QLatin1String("C.4.2, page 20")),
          QByteArray("SLRP MA_W_R_T_SIG: Annexure C's text and where: ") + ma.left(160).toUtf8());
    CHECK(tip303(slrpTable, QStringLiteral("  MA_W_R_T_SIG"), 1) == ma, "on the Value cell too");
    CHECK(tip303(lsrpTable, QStringLiteral("  L_DOUBTOVER")).contains(QLatin1String("over-reading amount")),
          "LSRP L_DOUBTOVER, C.4.3");
    CHECK(tip303(lsrpTable, QStringLiteral("  LOCO_VERSION")).contains(QLatin1String("SOURCE_LOCO_VERSION")),
          "DLConsole's name, the annexure's description");
    CHECK(tip303(rfidTable, QStringLiteral("  unique")).contains(QLatin1String("Unique ID of RFID Tag Set")),
          "an RFID tag's unique ID, Annexure D");
    CHECK(tip303(rfidTable, QStringLiteral("type")).isEmpty() && tip303(slrpTable, QStringLiteral("rtc")).isEmpty(),
          "the capture line's rows have none");

    // Kept across ticks: the tables are rebuilt on every refresh.
    feed(nthOf303("slrp", 6));
    QMetaObject::invokeMethod(&w, "onRefreshTick");
    CHECK(tip303(slrpTable, QStringLiteral("  MA_W_R_T_SIG")) == ma, "still there after the next frame and tick");

    // Every indented row of the SLRP tab is described.
    int described = 0, fieldRows = 0;
    for (int row = 0; slrpTable && row < slrpTable->rowCount(); ++row) {
        const QString name = slrpTable->item(row, 0)->text();
        if (!name.startsWith(QLatin1Char(' '))) continue;
        ++fieldRows;
        described += slrpTable->item(row, 0)->toolTip().isEmpty() ? 0 : 1;
    }
    CHECK(fieldRows > 15 && described == fieldRows,
          QByteArray("every SLRP field row: ") + QByteArray::number(described) + "/" + QByteArray::number(fieldRows));
}
