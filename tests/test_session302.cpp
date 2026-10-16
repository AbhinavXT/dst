#include "testutil.h"

#include "decodeworkbench.h"
#include "fielddocs.h"

#include <QDir>
#include <QFile>
#include <QTableWidget>
#include <QTextStream>

// =============================================================================
//  Session 302 — the Decode Workbench shows the annexures' field descriptions.
//
//  Every decoded row of a radio packet (Annexure C) or an RFID tag
//  (Annexure D) carries its description as the tooltip of its Field and
//  Value cells. Rows of the capture line itself ("type", "rtc", "seq") and
//  packets the annexures do not cover (DMI) get none. Real frames from
//  replay/.
// =============================================================================

namespace {

QString firstLine(const QString &prefix)
{
    for (const QString &name : QDir(QStringLiteral(DL_SRC_DIR "/replay")).entryList({ QStringLiteral("*.cap") })) {
        QFile file(QStringLiteral(DL_SRC_DIR "/replay/") + name);
        if (!file.open(QIODevice::ReadOnly)) continue;
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.startsWith(prefix)) return line;
        }
    }
    return QString();
}

int rowNamed(QTableWidget *table, const QString &name)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0) && table->item(row, 0)->text() == name) return row;
    }
    return -1;
}

QString tipOf(QTableWidget *table, const QString &name, int column = 0)
{
    const int row = rowNamed(table, name);
    return row >= 0 && table->item(row, column) ? table->item(row, column)->toolTip() : QStringLiteral("<no row>");
}

}  // namespace

TEST_SUITE(session302)
{
    DecodeWorkbench bench;
    QTableWidget *table = bench.fieldTable();
    CHECK(table && table->horizontalHeaderItem(0)->toolTip().contains(QLatin1String("Annexure C")),
          "the Field header says the rows explain themselves");

    // ---- an SLRP ------------------------------------------------------------------------
    const QString slrp = firstLine(QStringLiteral("@slrp_"));
    CHECK(!slrp.isEmpty(), "a real SLRP line from replay/");
    bench.loadBuffer(slrp);
    CHECK(table->rowCount() > 20, QByteArray("decoded: ") + QByteArray::number(table->rowCount()) + " rows");
    const QString pktType = tipOf(table, QStringLiteral("  PKT_TYPE"));
    CHECK(pktType.contains(QLatin1String("<b>PKT_TYPE</b>")) && pktType.contains(QLatin1String("1001 (9)"))
              && pktType.contains(QLatin1String("C.4.2, page 12")),
          QByteArray("PKT_TYPE: the annexure's text and where: ") + pktType.toUtf8());
    CHECK(tipOf(table, QStringLiteral("  PKT_TYPE"), 1) == pktType, "the Value cell carries it too");
    CHECK(tipOf(table, QStringLiteral("  SOURCE_STN_ID")).contains(QLatin1String("SOURCE_STN_ILC_IBS_ID")),
          "DLConsole's name, described under the annexure's");
    int speedRow = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)->text().trimmed().startsWith(QLatin1String("speed["))) { speedRow = row; break; }
    }
    CHECK(speedRow >= 0 && table->item(speedRow, 0)->toolTip().contains(QLatin1String("LM_Static_Speed_Class")),
          "a static speed item: its three spec fields");
    int described = 0, fieldRows = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QString name = table->item(row, 0)->text();
        if (!name.startsWith(QLatin1Char(' '))) continue;
        ++fieldRows;
        described += table->item(row, 0)->toolTip().isEmpty() ? 0 : 1;
    }
    CHECK(fieldRows > 15 && described == fieldRows,
          QByteArray("every SLRP field row is described: ") + QByteArray::number(described) + "/" + QByteArray::number(fieldRows));
    CHECK(tipOf(table, QStringLiteral("rtc")).isEmpty() && tipOf(table, QStringLiteral("type")).isEmpty(),
          "the capture line's own rows are not");

    // ---- an RFID tag ----------------------------------------------------------------------
    const QString rfid = firstLine(QStringLiteral("@rfid_"));
    CHECK(!rfid.isEmpty(), "a real RFID line");
    bench.loadBuffer(rfid);
    const QString placement = tipOf(table, QStringLiteral("  placement"));
    CHECK(placement.contains(QLatin1String("Tag placement")) && placement.contains(QLatin1String("D 2. Normal Tag")),
          QByteArray("a Normal tag's placement, Annexure D 2: ") + placement.left(200).toUtf8());
    CHECK(tipOf(table, QStringLiteral("  type")).contains(QLatin1String("The decimal value is 9")),
          "the tag's type row (indented) is Annexure D's");
    CHECK(tipOf(table, QStringLiteral("type")).isEmpty(), "the capture's type row is not");
    CHECK(tipOf(table, QStringLiteral("  tin")).contains(QLatin1String("TIN in Reverse Direction")),
          "a group row: both halves");
    CHECK(tipOf(table, QStringLiteral("  reader_id")).isEmpty(), "the reader's id: not in Annexure D, no text");

    // ---- a packet the annexures do not cover ---------------------------------------------
    const QString dmi = firstLine(QStringLiteral("@dmi_"));
    if (!dmi.isEmpty()) {
        bench.loadBuffer(dmi);
        int tips = 0;
        for (int row = 0; row < table->rowCount(); ++row) tips += table->item(row, 0)->toolTip().isEmpty() ? 0 : 1;
        CHECK(table->rowCount() > 5 && tips == 0, "a DMI frame: no descriptions (not Annexure C or D)");
    } else {
        CHECK(true, "no DMI line in replay/ (nothing to check)");
    }
}
