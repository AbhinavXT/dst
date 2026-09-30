#include "testutil.h"

#include "messageheader.h"
#include "packetpreset.h"
#include "packetmakerdialog.h"

#include <QByteArray>
#include <QComboBox>
#include <QGroupBox>
#include <QTableWidget>
#include <QCheckBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QLineEdit>
#include <QNetworkDatagram>
#include <QPushButton>
#include <QSpinBox>
#include <QUdpSocket>

// Session 64: operator-defined fields between the 8-byte message header and
// the packet (station_id, loco_id, ...).
TEST_SUITE(headerextras)
{
    using MessageHeader::Extra;

    // ---- defaults ---------------------------------------------------------
    {
        const QVector<Extra> d = MessageHeader::defaultExtras();
        CHECK(d.size() == 2, "two starting rows");
        CHECK(d[0].name == "station_id" && d[0].bytes == 2, "station_id is uint16");
        CHECK(d[1].name == "loco_id"    && d[1].bytes == 4, "loco_id is uint32");
        CHECK(!d[0].enabled && !d[1].enabled, "both unticked: nothing extra by default");
        CHECK(MessageHeader::encodeExtras(d).isEmpty(), "unticked rows emit no bytes");
        CHECK(MessageHeader::buildWithExtras("slrp", 33, 0x0102, 7, 2, d)
                  == MessageHeader::build("slrp", 33, 0x0102, 7, 2),
              "with nothing ticked the envelope is the plain 8-byte header, unchanged");
    }

    // ---- value parsing: refuse, never truncate -----------------------------
    {
        qint64 v = 0;
        CHECK(MessageHeader::parseExtraValue("1234", 2, &v) && v == 1234, "decimal");
        CHECK(MessageHeader::parseExtraValue("0x04D2", 2, &v) && v == 1234, "0x hex");
        CHECK(MessageHeader::parseExtraValue(" 0XfF ", 1, &v) && v == 255, "case/space tolerant");
        CHECK(MessageHeader::parseExtraValue("65535", 2, &v) && v == 65535, "uint16 max fits");
        CHECK(!MessageHeader::parseExtraValue("65536", 2, &v), "uint16 max+1 refused");
        CHECK(MessageHeader::parseExtraValue("4294967295", 4, &v) && v == 4294967295LL,
              "uint32 max fits");
        CHECK(!MessageHeader::parseExtraValue("0x100000000", 4, &v), "uint32 overflow refused");
        CHECK(!MessageHeader::parseExtraValue("256", 1, &v), "uint8 overflow refused");
        CHECK(!MessageHeader::parseExtraValue("-1", 2, &v), "negative refused");
        CHECK(!MessageHeader::parseExtraValue("12a", 2, &v), "junk refused");
        CHECK(!MessageHeader::parseExtraValue("", 2, &v), "empty refused");
        CHECK(!MessageHeader::parseExtraValue("1", 3, &v), "3-byte width refused");
    }

    // ---- exact bytes and message_length ------------------------------------
    {
        Extra st; st.name = "station_id"; st.bytes = 2; st.value = 0x0102; st.enabled = true;
        Extra lo; lo.name = "loco_id";    lo.bytes = 4; lo.value = 0x0A0B0C0D; lo.enabled = true;

        // slrp, 33-byte packet, station_id only: msg_len = 33 + 8 + 2 = 43 = 0x2B.
        const QByteArray a = MessageHeader::buildWithExtras("slrp", 33, 0x0102, 7, 2, { st });
        CHECK(a.size() == 10, "8-byte header + 2-byte station_id");
        CHECK(a.toHex() == "070209002b0002010201",
              "header then station_id LE; msg_len counts the extra (0x2B)");

        // Both, in table order.
        const QByteArray b = MessageHeader::buildWithExtras("slrp", 33, 0, 7, 2, { st, lo });
        CHECK(b.size() == 14, "8 + 2 + 4");
        CHECK(quint8(b[4]) == 33 + 14 && quint8(b[5]) == 0, "msg_len = packet + 14");
        CHECK(b.mid(8).toHex() == "02010d0c0b0a", "station_id then loco_id, both LE");

        // Order follows the table, not the name.
        const QByteArray c = MessageHeader::buildWithExtras("slrp", 33, 0, 7, 2, { lo, st });
        CHECK(c.mid(8).toHex() == "0d0c0b0a0201", "reordered rows reorder the bytes");

        // Big-endian per row.
        Extra be = st; be.bigEndian = true;
        CHECK(MessageHeader::encodeExtras({ be }).toHex() == "0102", "BE row");
        Extra be4 = lo; be4.bigEndian = true;
        CHECK(MessageHeader::encodeExtras({ be4 }).toHex() == "0a0b0c0d", "BE uint32");

        // A disabled row in the middle is skipped, not zero-filled.
        Extra off = lo; off.enabled = false;
        CHECK(MessageHeader::encodeExtras({ st, off, st }).toHex() == "02010201",
              "unticked rows are skipped entirely");
        CHECK(MessageHeader::extrasSize({ st, off, lo }) == 6, "size counts ticked rows");

        // uint8.
        Extra u8; u8.name = "x"; u8.bytes = 1; u8.value = 0xAB; u8.enabled = true;
        CHECK(MessageHeader::encodeExtras({ u8 }).toHex() == "ab", "uint8");

        // arp: the 10-byte form a loco's own radio emits.
        const QByteArray arp = MessageHeader::buildWithExtras("arp", 29, 0, 7, 2, { st });
        CHECK(arp.size() == 10 && quint8(arp[4]) == 39, "arp with station_id: 10 B, msg_len 39");

        // No header type -> nothing, extras or not.
        CHECK(MessageHeader::buildWithExtras("rfid", 10, 0, 7, 2, { st }).isEmpty(),
              "no header, no extras");
    }

    // ---- invalid enabled rows block; invalid disabled rows do not -----------
    {
        Extra bad; bad.name = "station_id"; bad.bytes = 2; bad.value = 70000; bad.enabled = true;
        QString err;
        CHECK(!MessageHeader::validateExtras({ bad }, &err), "70000 in uint16 refused");
        CHECK(err.contains("station_id"), "and the error names the field");
        CHECK(MessageHeader::buildWithExtras("slrp", 33, 0, 7, 2, { bad }).isEmpty(),
              "an invalid envelope is empty — callers refuse, never send bare");
        bad.enabled = false;
        CHECK(MessageHeader::validateExtras({ bad }), "an unticked bad row blocks nothing");
        Extra w; w.bytes = 3; w.enabled = true;
        CHECK(!MessageHeader::validateExtras({ w }), "3-byte width refused");
    }

    // ---- preset round-trip, and old presets --------------------------------
    {
        PacketPreset p;
        p.captype = "slrp";
        Extra st; st.name = "station_id"; st.bytes = 2; st.value = 4321;
        st.enabled = true; st.bigEndian = true;
        Extra lo; lo.name = "loco_id"; lo.bytes = 4; lo.value = 4294967295LL;
        p.extras = { st, lo };
        PacketPreset q;
        CHECK(q.fromJson(p.toJson()), "preset parses back");
        CHECK(q.extras == p.extras, "extras round-trip exactly, uint32 max included");

        QJsonObject old = p.toJson();
        old.remove("msg_extras");
        PacketPreset r;
        CHECK(r.fromJson(old) && r.extras.isEmpty(),
              "a pre-session-64 preset loads with no extras");
    }

    // ---- the dialog ---------------------------------------------------------
    {
        PacketMakerDialog dlg;
        QGroupBox *box = nullptr;
        for (QGroupBox *g : dlg.findChildren<QGroupBox *>()) {
            if (g->title().startsWith(QStringLiteral("Extra header fields"))) { box = g; }
        }
        CHECK(box != nullptr, "the extras panel exists");
        CHECK(box && box->isCheckable() && !box->isChecked(), "folded away by default");
        CHECK(box && box->title().contains(QStringLiteral("none")),
              "and its title says nothing is being added");

        QString err;
        const QVector<Extra> d = dlg.extrasForTest(&err);
        CHECK(err.isEmpty() && d == MessageHeader::defaultExtras(),
              "the table starts with the default rows, unticked");

        Extra st = d[0]; st.enabled = true; st.value = 0x0102;
        dlg.setExtrasForTest({ st, d[1] });
        CHECK(box && box->title().contains(QStringLiteral("+2 B")),
              "a ticked row is announced in the title even while folded");

        auto *tbl = box ? box->findChild<QTableWidget *>() : nullptr;
        CHECK(tbl && tbl->rowCount() == 2, "two rows");
        if (tbl) {
            // Type a value that does not fit: the dialog must refuse.
            tbl->item(0, 3)->setText(QStringLiteral("70000"));
            dlg.extrasForTest(&err);
            CHECK(!err.isEmpty() && err.contains("station_id"),
                  "an out-of-range typed value is reported, naming the field");
            CHECK(box->title().contains(QStringLiteral("INVALID")),
                  "and the title says so");
            tbl->item(0, 3)->setText(QStringLiteral("0x0102"));
            dlg.extrasForTest(&err);
            CHECK(err.isEmpty(), "a corrected value clears it");
        }
    }

    // ---- end to end: what actually goes on the wire ------------------------
    // Build and send a real slrp to a loopback socket and read the datagram
    // back. The unit checks above prove the envelope; this proves the dialog
    // puts it in front of the packet, in the right place, with the length
    // the header claims.
    {
        QUdpSocket rx;
        CHECK(rx.bind(QHostAddress(QHostAddress::LocalHost), 0), "loopback receiver binds");
        const quint16 port = rx.localPort();

        PacketMakerDialog dlg;
        QCoreApplication::processEvents();

        QLineEdit *host = nullptr;
        for (QLineEdit *le : dlg.findChildren<QLineEdit *>()) {
            if (le->text() == QLatin1String("127.0.0.1")) { host = le; break; }
        }
        QSpinBox *portSpin = nullptr;
        for (QSpinBox *sb : dlg.findChildren<QSpinBox *>()) {
            if (sb->parent() == &dlg && sb->maximum() == 65535 && sb->value() == 20000) {
                portSpin = sb; break;
            }
        }
        QPushButton *build = nullptr, *sendOnce = nullptr;
        for (QPushButton *b : dlg.findChildren<QPushButton *>()) {
            if (b->text().contains(QStringLiteral("Verify")))    { build = b; }
            if (b->text().contains(QStringLiteral("Send Once"))) { sendOnce = b; }
        }
        QCheckBox *arm = nullptr;
        for (QCheckBox *cb : dlg.findChildren<QCheckBox *>()) {
            if (cb->text().contains(QStringLiteral("Arm"), Qt::CaseInsensitive)) { arm = cb; break; }
        }
        CHECK(host && portSpin && build && sendOnce && arm, "the send controls are all found");
        if (!(host && portSpin && build && sendOnce && arm)) { return; }
        portSpin->setValue(port);

        Extra st = MessageHeader::defaultExtras()[0];
        st.enabled = true; st.value = 0x0102;
        Extra lo = MessageHeader::defaultExtras()[1];
        lo.enabled = true; lo.value = 0x0A0B0C0D;
        dlg.setExtrasForTest({ st, lo });

        build->click();
        QCoreApplication::processEvents();
        arm->setChecked(true);
        QCoreApplication::processEvents();
        CHECK(sendOnce->isEnabled(), "built and armed: Send Once is live");

        auto receive = [&rx]() -> QByteArray {
            QElapsedTimer t; t.start();
            while (!rx.hasPendingDatagrams() && t.elapsed() < 2000) {
                QCoreApplication::processEvents();
                rx.waitForReadyRead(20);
            }
            return rx.hasPendingDatagrams() ? rx.receiveDatagram().data() : QByteArray();
        };

        sendOnce->click();
        const QByteArray d = receive();
        CHECK(d.size() > 14, "a datagram arrived");
        if (d.size() > 14) {
            const int msgLen = quint8(d[4]) | (quint8(d[5]) << 8);
            CHECK(quint8(d[2]) == 9, "it is an slrp (message_id 9)");
            CHECK(msgLen == d.size(), "message_length is the whole datagram, extras included");
            CHECK(d.mid(8, 6).toHex() == "02010d0c0b0a",
                  "station_id then loco_id sit right after the 8-byte header");
        }

        // Unticking loco_id after the build must not quietly change what the
        // armed Send puts out: Send goes dead until the next Build.
        QTableWidget *extrasTbl = nullptr;
        for (QTableWidget *t : dlg.findChildren<QTableWidget *>()) {
            if (t->columnCount() == 5 && t->horizontalHeaderItem(4)
                && t->horizontalHeaderItem(4)->text() == QLatin1String("Byte order")) {
                extrasTbl = t;
            }
        }
        CHECK(extrasTbl != nullptr, "the extras table is found");
        if (extrasTbl) {
            QCheckBox *loSend = extrasTbl->cellWidget(1, 0)
                                    ? extrasTbl->cellWidget(1, 0)->findChild<QCheckBox *>()
                                    : nullptr;
            CHECK(loSend && loSend->isChecked(), "loco_id's Send box is a real, ticked checkbox");
            if (loSend) { loSend->setChecked(false); }
            QCoreApplication::processEvents();
            CHECK(!sendOnce->isEnabled(), "an edit to the extras after Build disables Send");

            build->click();
            QCoreApplication::processEvents();
            CHECK(sendOnce->isEnabled(), "and a rebuild brings it back");
            sendOnce->click();
            const QByteArray d2 = receive();
            CHECK(d2.size() == d.size() - 4, "loco_id unticked: four bytes fewer on the wire");
            if (d2.size() > 10) {
                CHECK(d2.mid(8, 2).toHex() == "0201", "station_id still follows the header");
                CHECK((quint8(d2[4]) | (quint8(d2[5]) << 8)) == d2.size(),
                      "and message_length followed the change");
                CHECK(d2.mid(10) == d.mid(14), "the packet itself is untouched");
            }

            // An invalid ticked value stops Build, so Send stays dead.
            extrasTbl->item(0, 3)->setText(QStringLiteral("70000"));
            build->click();
            QCoreApplication::processEvents();
            CHECK(!sendOnce->isEnabled(), "an out-of-range extra fails Build: nothing can go out");
        }
        arm->setChecked(false);
    }
}
