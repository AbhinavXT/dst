# Reject-rule extraction from SIF-0533 v4.22 test cases

Extracted by cell-level table parsing, so each transmission is paired with the
reception verdict from the SAME table row rather than by proximity in flowed
text. 279 operation rows found across 53 sections; 69 assert a rejection.

Nothing here is in the product yet. These are for review before they become
rules the console will assert, because a wrong reject reason in a safety tool
is worse than no reject reason.

## A. Ready to encode

| Clause | Field | Bits | Doc value | Read as | Rejects when |
|---|---|---|---|---|---|
| 31.6.1 | `PKT_TYPE` | 4 | `1000` | binary | `== 8` |
| 31.9 | `FRAME_NUM` | 17 | `0` | decimal | `== 0` |
| 31.9 | `FRAME_NUM` | 17 | `86401` | decimal | `== 86401` |
| 31.10 | `SOURCE_STN_ILC_IBS_ID` | 16 | `65536` | decimal | `== 65536` |
| 31.11 | `SOURCE_STN_ILC_IBS_VERSION` | 3 | `0` | decimal | `== 0` |
| 31.11 | `SOURCE_STN_ILC_IBS_VERSION` | 3 | `3` | decimal | `== 3` |
| 31.13.2 | `REF_PROF_ID` | 4 | `0000` | binary | `== 0` |
| 31.15 | `DIST_PKT_START` | 15 | `16390` | decimal | `== 16390` |
| 31.16.1 | `PKT_DIR` | 2 | `00` | binary | `== 0` |
| 31.16.4 | `PKT_DIR` | 2 | `11` | binary | `== 3` |
| 31.19.4 | `FRAME_OFFSET` | 4 | `1111` | binary | `== 15` |
| 31.20.12 | `DEST_LOCO_SOS` | 4 | `1111` | binary | `== 15` |
| 31.21.4 | `TRAIN_SECTION_TYPE` | 2 | `11` | binary | `== 3` |
| 31.23.17 | `CUR_SIG_ASPECT` | 6 | `010000` | binary | `== 16` |
| 31.23.18 | `CUR_SIG_ASPECT` | 6 | `010111` | binary | `== 23` |
| 31.23.20 | `CUR_SIG_ASPECT` | 6 | `011001` | binary | `== 25` |
| 31.23.21 | `CUR_SIG_ASPECT` | 6 | `011111` | binary | `== 31` |
| 31.24.17 | `NEXT_SIG_ASPECT` | 6 | `010000` | binary | `== 16` |
| 31.24.18 | `NEXT_SIG_ASPECT` | 6 | `010111` | binary | `== 23` |
| 31.24.20 | `NEXT_SIG_ASPECT` | 6 | `011001` | binary | `== 25` |
| 31.24.21 | `NEXT_SIG_ASPECT` | 6 | `011111` | binary | `== 31` |
| 31.26.1 | `AUTHORITY_TYPE` | 2 | `00` | binary | `== 0` |
| 31.31.1 | `TRN_LEN_INFO_STS` | 1 | `0` | binary | `== 0` |
| 31.32.4 | `APPR_STN_ILC_IBS_ID` | 16 | `65539` | decimal | `== 65539` |
| 31.36.2 | `ROUTE_RFID_CNT` | 6 | `0` | decimal | `== 0` |
| 31.39.52 | `ADJ_LINE_CNT` | 3 | `6` | decimal | `== 6` |
| 31.39.53 | `ADJ_LINE_CNT` | 3 | `7` | decimal | `== 7` |
| 31.59.1 | `FRAME_NUM` | 17 | `0` | decimal | `== 0` |
| 31.59.5 | `FRAME_NUM` | 17 | `86401` | decimal | `== 86401` |
| 31.60.1 | `FRAME_NUM` | 17 | `0` | decimal | `== 0` |
| 31.60.5 | `FRAME_NUM` | 17 | `86401` | decimal | `== 86401` |
| 31.62.1 | `SOURCE_STN_ILC_IBS_VERSION` | 3 | `0` | decimal | `== 0` |
| 31.62.4 | `SOURCE_STN_ILC_IBS_VERSION` | 3 | `3` | decimal | `== 3` |
| 32.11.1 | `FRAME_NUM` | 17 | `0` | decimal | `== 0` |
| 32.12.1 | `SOURCE_STN_ILC_IBS_ID` | 16 | `0` | decimal | `== 0` |
| 32.13.1 | `SOURCE_STN_ILC_IBS_VERSION` | 3 | `0` | decimal | `== 0` |
| 32.14.1 | `DEST_LOCO_ID` | 20 | `0` | decimal | `== 0` |
| 32.20.1 | `FRAME_NUM` | 17 | `0` | decimal | `== 0` |

## B. Width mismatch — NOT encoding without your call

The doc writes these as 4-character binary strings, but `kavach.xml` gives the
field 6 bits. Read as binary they are 9 and 14; read as decimal they are 1001
and 1110, which will not fit in 6 bits at all. Same section also has genuine
6-bit values (`010000`, `011111`), so the doc is mixing two widths.

| Clause | Field | Doc value | Schema bits |
|---|---|---|---|
| 31.23.10 | `CUR_SIG_ASPECT` | `1001` | 6 |
| 31.23.15 | `CUR_SIG_ASPECT` | `1110` | 6 |
| 31.24.10 | `NEXT_SIG_ASPECT` | `1001` | 6 |
| 31.24.15 | `NEXT_SIG_ASPECT` | `1110` | 6 |

## C. False positives — the PKT_TYPE names the section, not the fault

These rows do assert a rejection, but the `PKT_TYPE` in the sentence is the
packet being tested, not the reason. The real condition is elsewhere in the
row and needs reading by hand.

| Clause | Row |
|---|---|
| 31.46.2 | 2 Stationary KAVACH should send a packet with PKT_TYPE as 0011 followed by complete packet and valid packet length (SUB_PKT_ LENGTH) LC_ SUB_PKT_L CNT |
| 31.47.1 | Stationary KAVACH should send a packet with PKT_TYPE as 0011 followed by complete packet and valid packet length (SUB_PKT_ LENGTH). Stationary KAVACH  |
| 31.48.1 | Stationary KAVACH should send a packet with PKT_TYPE as 0011 followed by complete packet and valid packet length (SUB_PKT_ LENGTH) and Valid LC_ID_Num |
| 31.54.2 | Stationary KAVACH should send a packet with PKT_TYPE as 0111 followed by complete packet and valid packet length (SUB_PKT_ LENGTH), TSR_Status as 3 (R |
| 31.54.4 | Stationary KAVACH should send a packet with PKT_TYPE as 0111 followed by complete packet and valid packet length (SUB_PKT_ LENGTH) and TSR_Status as 3 |
| 31.54.6 | Stationary KAVACH should send a packet with PKT_TYPE as 0111 followed by complete packet and valid packet length (SUB_PKT_ LENGTH) and and TSR_Status  |
| 31.55.5 | Stationary KAVACH should send a packet with PKT_TYPE as 0111 followed by complete packet and valid packet length (SUB_PKT_ LENGTH) and TSR_Status as 3 |
| 31.6.2 | Stationary KAVACH should send a packet with PKT_TYPE as 1010 followed by complete packet. |
| 31.7 | Onboard KAVACH should send a packet with PKT_TYPE as 1001 (Station to Onboard Regular Packet) followed by complete packet. |

## D. Narrative conditions — need judgement

Not of the form `FIELD == value`. Each needs a condition expression, and several
need state the console does not currently keep.

| Clause | Row |
|---|---|
| 31.12 | Stationary KAVACH should send a packet with DEST_ONBOARD _ID as 0, 12345, 222345, 606060 and 1234567 followed by complet |
| 31.13.1 | Stationary KAVACH shall send the Access Authority Packet. |
| 31.44.2 | The Stationary KAVACH shall send LM_GRAD _SPEED_INFO_CNT as “ 0”. |
| 31.45.2 | The Stationary KAVACH shall send GRAD _SPEED_INFO_CNT as “31”. |
| 31.61.1 | Onboard KAVACH should send a packet with Onboard KAVACH ID as “0” followed by complete packet. |
| 31.63.1 | Onboard KAVACH should send a packet with invalid ABS_LOCATION till Valid MOVEMENT_DIR is available with it |
| 31.64.1 | If the train integrity status is disabled, then Onboard KAVACH shall send 00 - No Train Integrity information available  |
| 32.3.1 | Transmission of packets from Onboard KA- |
| 32.8.1 | Onboard KAVACH shall send “Info_Ack” as Zero |
| 32.9.1 | Transmission of packets from Onboard |
| 32.15.6 | Stationary KAVACH shall send the packets both in LTE and UHF with uplink and downlink frequency channel marked as 4094,  |
| 32.16.1 | Stationary KAVACH should send a packet |
| 32.16.2 | Stationary KAVACH should send a packet |
| 32.21.1 | Onboard KAVACH should send a packet with SOURCE_Loco_ID as 0 followed by complete packet. |

## E. Your two answers, as rules

**FRAME_OFFSET 14** — accepted when the RFID has absolute location or the train
is in a station section, rejected otherwise. So this is not a value rule but a
conditional one, and the table format has to carry a guard expression. Clause
31.19.3 and 31.19.5 are then both right, in different sections.

**Own onboard ID** — inferred from observed ARP/LSRP rather than configured.
Which means `DEST_ONBOARD_ID` rules cannot fire until an ARP or LSRP has been
seen, and the console must say *that* rather than silently not checking.

## Fields the schema does not name

`SUB_PKT_TYPE`, `LM_SPEED_INFO_CNT`, `SOURCE_LOCO_VERSION` appear in the doc but
not as `<field name=>` in kavach.xml under those spellings. Needs mapping before
their rules can reference decoded values.