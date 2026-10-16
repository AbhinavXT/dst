#!/usr/bin/env python3
"""Builds schema/fielddocs.json from the texts below, which are copied from
RDSO Annexure C (Amdt-10) and Annexure D (Amdt-7). Only PDF line-break
hyphens are rejoined ("re- transmitted" -> "retransmitted"); wording, values
and typos are the annexures' own."""
import json
import sys

docs = []


def add(packets, names, src, clause, page, parts, tag_types=None, note=None):
    entry = {"packets": packets, "names": names, "src": src, "clause": clause, "page": page,
             "parts": [{"field": f, "bits": b, "text": t} for (f, b, t) in parts]}
    if tag_types:
        entry["tag_types"] = tag_types
    if note:
        entry["note"] = note
    docs.append(entry)


PKT_TYPE_V = ("0000- 0111: Radio packets for KAVACH V3.2\n"
              "Radio packets for KAVACH V4.0:\n"
              "1000: Reserved for future use\n"
              "1001: Station to Onboard Regular Packet\n"
              "1010: Onboard to Station Regular Packet\n"
              "1011: Access Authority Packet\n"
              "1100: Additional Emergency Packet\n"
              "1101: Onboard Access Request\n"
              "1110 to 1111: Reserved for future use")
FRAME_NUM_T = ("1 to 86400 ((hr * 3600 + mm * 60 + ss)+ 1)\n"
               "Example: 00:00:00 - Frame No 1\n00:00:02 - Frame No. 3\n……\n23:59:58 - Frame No 86399")
LEN7 = ("Packet Length is in terms of bytes\n000 0000 - 1 byte\n000 0001 – 2 bytes\n…………\n"
        "111 1111 – 128 bytes")
STN_ID_T = "Unique Code, Valid values from 1 to 65535 (Purchaser Railway to Decide) (It will be unique for one KMS)"
STN_VER_T = ("0: Not used\n1 to 7: Kavach Version\n1 : Kavach Specification 3.2\n"
             "2 : Kavach Specification 4.0")
LOCO_VER_T = ("0: Not used\n1 to 7: Kavach Version\n1: Kavach Specification 3.2\n"
              "2: Kavach Specification 4.0")
SUBPKT_T = ("0000: Movement Authority\n0001: Static Speed Profile\n0010: Gradient Profile\n"
            "0011: LC gate profile\n0100: Turnout Speed Profile\n0101: Tag Linking Information\n"
            "0110: Track Condition data\n0111: Temporary speed Restrictions Profile\n"
            "1000 to 1111: Reserved for future use")
SPEED6_T = ("0: Reserved\n1- 50: 5-250 Kmph, Speed in steps of 5kmph. Max Speed = 250 kmph\n"
            "51--61: Reserved for future use\n62 – 8 Kmph\n63 : Unknown")
SIG_ASPECT_T = ("0 Unidentified\n000001 Red\n000010 Yellow without Display of Route Indication\n"
                "000011 Yellow with Pos1 Junction Type Route Indication (left)\n"
                "000100 Yellow with Pos2 Junction Type Route Indication (left)\n"
                "000101 Yellow with Pos3 Junction Type Route Indication (left)\n"
                "000110 Yellow with Pos4 Junction Type Route Indication (right)\n"
                "000111 Yellow with Pos5 Junction Type Route Indication (right)\n"
                "001000 Yellow with Pos6 Junction Type Route Indication (right)\n"
                "001001 Spare\n001010 Double Yellow\n001011 Green\n"
                "001100 Double Yellow with Pos1 Junction Type Route Indication (left)\n"
                "001101 Double Yellow with Pos4 Junction Type Route Indication (right)\n"
                "001110 AG Marker OFF\n001111 Red with Calling-on at OFF\n"
                "010000 to 010111 Spare\n011000 Stop Board / Buffer Stop\n011001 to 011111 Spare\n"
                "100000 to 111111 Yellow with Stencil route 1 to 32")
MOVE_DIR_T = ("00 Traffic Direction not established / unidentified\n01 Nominal\n10 Reverse\n"
              "11 Reserved for future use")
MODE_T = ("0001 Stand_By\n0010 Staff_Responsible_Mode\n0011 Limited_Supervision\n0100 Full_Supervision\n"
          "0101 Override\n0110 On_Sight\n0111 Trip\n1000 Post_Trip\n1001 Reverse\n1010 Shunting\n"
          "1011 Non_Leading\n1100 System_Failure\n1101 Isolation")
TIN9_T = ("0 Ignore / Don‟t Care\n1 to 250 Track Identity Number as per Track Section Occupied\n"
          "251 Onboard shed TIN\n252-511 Reserved for future use")
SPEED_KMPH_T = ("0 to 400 Train Speed in kmph\n401 to 510 Reserved for future use\n"
                "511 Train Speed unidentified")
DIST_KM = "Value in meters i.e. ranging from 0 – 32.76 km"

# ============================================================================
#  Annexure C, C.4.2: Station to Onboard regular packet (SLRP)
# ============================================================================
C42 = ("slrp",)
add(["slrp"], ["PKT_TYPE"], "C", "C.4.2", 12, [("PKT_TYPE", 4, "1001 (9)\n" + PKT_TYPE_V)])
add(["slrp"], ["PKT_LENGTH"], "C", "C.4.2", 12, [("PKT_LENGTH", 10,
    "Packet Length is in terms of bytes\n00 0000 0000 - 1 byte\n00 000 0001 – 2 bytes\n…………\n"
    "11 1111 1111 – 1024 bytes")])
add(["slrp"], ["FRAME_NUM"], "C", "C.4.2", 12, [("FRAME_NUM", 17, FRAME_NUM_T)])
add(["slrp"], ["SOURCE_STN_ID"], "C", "C.4.2", 13, [("SOURCE_STN_ILC_IBS_ID", 16, STN_ID_T)])
add(["slrp"], ["STN_VERSION"], "C", "C.4.2", 13, [("SOURCE_STN_ILC_IBS_VERSION", 3, STN_VER_T)])
add(["slrp"], ["DEST_LOCO_ID"], "C", "C.4.2", 13, [("DEST_LOCO_ID", 20, "1 to 999999")])
add(["slrp"], ["REF_PROF_ID"], "C", "C.4.2", 13, [("REF_PROF_ID", 4,
    "This is the Profile number of the below packets transmitted to Onboard KAVACH and is specific to it. "
    "On every update of MA, these packets are to be retransmitted.\n"
    "0000: No profile information. On receipt of Access Authority Packet, the onboard KAVACH shall send "
    "‘0000’ retaining the profile already available for speed supervision. Stationary KAVACH shall send "
    "REF_PROF_ID as “0000” when route is not known.\n"
    "0001 to 1111: Valid profile information\n"
    "This is associated with TurnOut_INFO, TSR_INFO, TAG_LINKING, NEUTRAL _INFO, etc., Onboard KAVACH is "
    "expected to acknowledge the receipt of this profile ID. Stationary KAVACH will stop transmission of "
    "this profile after the receipt of acknowledgement.")])
add(["slrp"], ["LAST_REF_RFID"], "C", "C.4.2", 14, [("LAST_REF_RFID", 10,
    "Below track profile data is given from last RFID as a reference position. This RFID shall be one of "
    "the last ten tags read by Onboard KAVACH. Onboard KAVACH shall retain last 11 RFID Tags along with "
    "their location. From the last 10 RFID Tags reported by Onboard KAVACH, Stationary KAVACH shall send "
    "the profile with respect to the most recently received tag. Stationary KAVACH shall send the actual "
    "distances of start and end locations of each element in the profile with respect to LAST_REF_RFID. "
    "Stationary and Onboard KAVACH shall not consider Foreign tags and wrong line tags as LAST_REF_RFID.")])
add(["slrp"], ["DIST_PKT_START"], "C", "C.4.2", 14, [("DIST_PKT_START", 15,
    "Signed Value. -16384m to +16383m. Distance in meters from LAST_REF_RFID from where below sub packets "
    "data starts. For this distance there is no profile or already profile might be given. When the value "
    "is negative, the onboard KAVACH shall supervise the profile from the REAR end of the train. When the "
    "value is positive, the onboard KAVACH shall merge with the existing profile, if available and "
    "supervise MRSP. Positive correction shall be sent by Stationary KAVACH in exceptional cases. When "
    "current Onboard route is unknown, this value to be from shifted position reference (eg. from "
    "approaching signal foot).")])
add(["slrp"], ["PKT_DIR"], "C", "C.4.2", 14, [("PKT_DIR", 2, "00-unidentified\n01- Nominal\n10- Reverse")])
add(["slrp"], ["subpkt"], "C", "C.4.2", 15, [
    ("SUB_PKT_TYPE", 4, SUBPKT_T),
    ("SUB_PKT_LENGTH", 7, "Length in bytes. Max 128 bytes (1024 bits).")])

# ---- Movement Authority sub packet ----
add(["slrp"], ["FRAME_OFFSET"], "C", "C.4.2", 15, [("FRAME_OFFSET", 4,
    "0001 to 1110\nFrame offset = (Stationary Kavach frame number - last received Onboard Kavach frame "
    "number)/2\nCyclic substraction to be ensured at 00:00 hours.")])
add(["slrp"], ["DEST_LOCO_SOS"], "C", "C.4.2", 15, [("DEST_LOCO_SOS", 4,
    "SoS/ Emergency Condition to Specific Onboard under following conditions\n"
    "0000: No SoS /Emergency\n0001: Foreign RFID\n0010: Reserved.\n0011: Onboard Odo error is >= 120m\n"
    "0100: Detection of SPAD\n0101: Rear-end collision\n0110: Head-On collision\n"
    "0111: Violation of Shunting limits in shunt mode\n1000: Station General SoS\n1001 to 1111: Reserved")])
add(["slrp"], ["TRAIN_SECTION_TYPE"], "C", "C.4.2", 15, [("TRAIN_SECTION_TYPE", 2,
    "00: Station Section\n01: Absolute Block\n10: Autoblock\n11: Reserved")])
add(["slrp"], ["CUR_SIG_INFO"], "C", "C.4.2", 16, [("CUR_SIG_INFO", 17,
    "a16a15a14a13a12a11a10a9a8a7a6a5a4a3a2a1a0\n"
    "a4 to a0 : (to be defined and displayed only for applicable Home / Routing Home / Starter / "
    "Intermediate Starter)\n"
    "00000: To be sent when line number information is not applicable.\n"
    "11111: Line Number in excess of 30 Decimal, in this case, no line number to be displayed on DMI.\n"
    "11110: Goods Lines (in case of any Goods Line > 30 Decimal, no need to display Line Number on DMI, "
    "however, the information to be displayed on DMI that the Train is going to Goods Line). It is "
    "clarified that even for multiple Goods Lines, Line Number shall not be communicated to Onboard "
    "KAVACH Unit and distinction among Goods Line would not be available through DMI to Onboard Pilot.\n"
    "a8 to a5: Line Name\n0000-Up Signal\n0001-Down Signal\n0010-Up Fast Signal\n0011-Down Fast Signal\n"
    "1000-Up Slow Signal\n1001-Down Slow Signal\n1010-Up Main Signal\n1011-Down Main Signal\n"
    "1100-Up Sub Signal\n1101-Down Sub Signal\n1110-UP BI-Direction\n1111-DN BI-Direction\n"
    "0100 to 0111-Future Use.\n"
    "This field is not to be used for any purpose other than display associated with signal.\n"
    "a14 to a9: Type of Signal\n"
    "0 Undefined - nothing to be displayed on DMI\n"
    "010xxx Various Distant Signals & Auto Signals: 010000 – Distant Signal; 010001 - Inner Distant Signal; "
    "010010 - Gate Distant Signal; 010011 - Gate Inner Distant Signal; 010100 - IB Distant Signal; "
    "010101 - IB Inner Distant Signal; 010110 - Auto Signal (Excludes Gate Stop Signal in Auto Territory); "
    "010111 - Semi-Automatic Signal with A-marker lit; 100100- Semi Automatic Signal without A marker lit\n"
    "0110xx Various Home Signals: 011000 - Main Home without Junction Route Indicator; 011001 - Main Home "
    "with Junction Route Indicator; 011010 - Routing Home without Junction Type Route Indicator; "
    "011011 - Routing Home with Junction Type Route Indicator\n"
    "0111xx Various types of Starter Signals: 011100 - Mainline Starter; 011101 - Loopline Starter; "
    "011110 - Intermediate Starter\n"
    "x0xxxx Other Misc Signals: 000001 - Advanced Starter Signal; 000010 - IB Stop Signal; "
    "000011 - Gate Stop Signal; 000100 - Calling-on Signal; 000101 - Advanced Starter-cum-Gate Signal; "
    "000110 - Gate-cum-Distant Signal; 000111 - Advanced Starter-cum-Distant Signal; 100011 - Gate Stop "
    "Signal in Auto Territory; 100100 - Semi Automatic Signal without A marker lit; 100101- Advance "
    "Starter-cum-Gate Inner Distant Signal; 100110- Gate-cum-Inner Distant Signal; 100111- Gate Inner "
    "Distant-cum-Distant Signal; 101000- IB Signal-cum-Gate Distant Signal; 101001- IB Signal-cum-Gate "
    "Inner Distant Signal; 101010- IB Signal-cum-Distant Signal; 101011- Advanced Starter-cum- IB Distant; "
    "101100- Starter-cum- IB Distant Signal; 101101- Stop Board/Buffer Stop; 101110- Gate cum IB Distant "
    "Signal; 101111- Gate cum IB Inner Distant Signal; 110000- Advance Starter-cum-Gate Distant Signal\n"
    "001000 - Only in RFID Tag, not in Radio Packet. OnboardKavach shall apply Brake when it crosses signal "
    "with this code (dead stop locations - such as end of berthing tracks with Shunt Signals) in normal "
    "mode.\n"
    "The full list of signals along with corresponding binary codes will be issued through a Technical "
    "Advisory Note (TAN). Modification of nomenclature shall not result in change of Executive Software.\n"
    "a15: Signal override permission when danger (0: at standstill, 1: while running- permissive signals "
    "controlled by such stop signals shall also be considered)\n"
    "a16 Stop Signal (0: No, 1: Yes)")], note="The annexure prints CUR_SIG_ASPECT's codes and a15/a16 "
    "between pages 18 and 20; a15 and a16 are part of CUR_SIG_INFO's 17 bits.")
add(["slrp"], ["CUR_SIG_ASPECT"], "C", "C.4.2", 19, [("CUR_SIG_ASPECT", 6, SIG_ASPECT_T)])
add(["slrp"], ["NEXT_SIG_ASPECT"], "C", "C.4.2", 20, [("NEXT_SIG_ASPECT", 6,
    "0 In case current Signal Aspect is RED (Undefined)\nCodes as given in CUR_SIG_ASPECT above")])
add(["slrp"], ["APPR_SIG_DIST"], "C", "C.4.2", 20, [("APPR_SIG_DIST", 15,
    "Approaching signal distance in meter from the last reference RFID Tag (valid up to 32767m)")])
add(["slrp"], ["AUTHORITY_TYPE"], "C", "C.4.2", 20, [("AUTHORITY_TYPE", 2,
    "00: Not to be used\n01: OS Authority (Distance allowed in OS mode with speed restriction)\n"
    "10: FS Authority (Distance allowed in FS mode)\n"
    "11: SR Authority. When MA is required to be extended beyond border signal and adjacent S-KAVACH "
    "communication failed, Authorised speed shall be unknown (63). Onboard KAVACH shall ignore "
    "APPR_SIG_DIST and MA_W_R_T_SIG.")])
add(["slrp"], ["AUTHORIZED_SPEED"], "C", "C.4.2", 20, [("AUTHORIZED_SPEED", 6,
    "Only If AUTHORITY_TYPE = ’01, AUTHORIZED_SPEED variables follows.\n"
    "0-50: 0 to 250 kmph, (in revolution of 5Kmph) i.e 3: 15 kmph to be sent for unknown route. No Track "
    "profile packets to be sent.\n51-61: Reserved for future use,\n"
    "62: 8 Kmph (Configuarble) for auto signal override during night\n63: Unknown.")])
add(["slrp"], ["MA_W_R_T_SIG"], "C", "C.4.2", 20, [("MA_W_R_T_SIG", 16,
    "0 to 65534 in meters.\n65535: Unknown (Onboard Kavach continues in SR Mode).\n"
    "The Movement Authority transmitted shall be the distance of End of Authority from reference RFID "
    "position. In order to cater the delays and failure in acquiring information due to missing radio "
    "frames, the Movement Authority in Onboard shall be deduced as the Movement authority corresponding "
    "to particular frame received from Stationary Kavach Unit minus the actual distance traveled by the "
    "Onboard from that reference RFID position.")])
add(["slrp"], ["REQ_SHORTEN_MA"], "C", "C.4.2", 21, [("REQ_SHORTEN_MA", 1,
    "0: No request from trackside for shortening MA\n1: New request from trackside for shortening MA\n"
    "(New, Future Use)")])
add(["slrp"], ["NEW_MA"], "C", "C.4.2", 21, [("NEW_MA", 16,
    "Only If REQ_SHORTEN_MA = 1, NEW_MA variables follow.\n"
    "New MA due to signal cancellation request from EI\n(New, Future Use)")])
add(["slrp"], ["TRN_LEN_INFO_STS"], "C", "C.4.2", 21, [("TRN_LEN_INFO_STS", 1,
    "0 – No Train Length Info,\n1 – Train Length Info follows\n"
    "Only If TRN_LEN_INFO_STS = 1, TRN_LEN_INFO_TYPE and remaining variables follow.")])
add(["slrp"], ["TRN_LEN_INFO_TYPE"], "C", "C.4.2", 21, [("TRN_LEN_INFO_TYPE", 1,
    "0 – means. REF_FRAME_NUM_TL and REF_OFFSET_INT_TL pertain to “Start” frame and offset.\n"
    "1 - means REF_FRAME_NUM_TL and REF_OFFSET_INT_TL pertain to “END” frame and offset.")])
add(["slrp"], ["REF_FRAME_NUM_TLM"], "C", "C.4.2", 21, [("REF_FRAME_NUM_TLM", 17, FRAME_NUM_T)])
add(["slrp"], ["REF_OFFSET_INT_TLM"], "C", "C.4.2", 22, [("REF_OFFSET_INT_TLM", 8,
    "0 to 200 (10ms resolution)")])
add(["slrp"], ["NEXT_STN_COMM"], "C", "C.4.2", 22, [("NEXT_STN_COMM", 1,
    "0 – No next station handover\n1 – Requires next station handover\n"
    "Only If NEXT_STN_COMM = 1, APPR_STN_ILC_IBS_ID variables follow.")])
add(["slrp"], ["APPR_STN_ILC_IBS_ID"], "C", "C.4.2", 22, [("APPR_STN_ILC_IBS_ID", 16,
    "1 to 65535\nApproaching next stationary Kavach ID")])

# ---- Static Speed Profile ----
add(["slrp"], ["LM_Speed_Info_CNT"], "C", "C.4.2", 22, [("LM_Speed_Info_CNT", 5, "1 to 31")])
add(["slrp"], ["speed"], "C", "C.4.2", 22, [
    ("LM_Static_Speed_Distance", 15, DIST_KM),
    ("LM_Static_Speed_Class", 1, "0 – Universal Speed will follow\n1 – Classified Speeds A,B,C will follow"),
    ("LM_Static_Speed_Value", 6, SPEED6_T + "\nClass 0: Universal Static Speed\n"
     "Class 1: Static Speed for Category A Trains (LE / Passenger Trains); Static Speed for Category B "
     "Trains (Loaded Goods Trains); Static Speed for Category C Trains (Empty Goods Trains)")])

# ---- Gradient Profile ----
add(["slrp"], ["LM_Grad_Info_CNT"], "C", "C.4.2", 23, [("LM_Grad_Info_CNT", 5, "1 to 31")])
add(["slrp"], ["grad"], "C", "C.4.2", 23, [
    ("LM_Gradient_Distance", 15, DIST_KM),
    ("LM_GDIR", 1, "0 = downhill\n1 = uphill"),
    ("LM_GRADIENT_VALUE", 5,
     "This is the absolute value of the average gradient between two defined position as described in "
     "Annexure-I. Values lie between 0 to 30. Value 31: reserved.\n"
     "0: Gradient not steeper than “1 in 1000”. Includes Level Gradient\n"
     "1: Gradient from “1 in 1000” to not steeper than “1 in 500”\n"
     "2: Gradient from “1 in 500” to not steeper than “1 in 333”\n"
     "3: Gradient from “1 in 333” to not steeper than “1 in 250”\n"
     "4: Gradient from “1 in 250” to not steeper than “1 in 200”\n….\n"
     "n: Gradient from “1 in (1000/n)” to not steeper than “1 in {1000/(n+1)}”\n….\n"
     "30: Gradient steeper than “1 in 33”\n31: Reserved")])

# ---- LC gate profile ----
add(["slrp"], ["LM_LC_Info_CNT"], "C", "C.4.2", 24, [("LM_LC_Info_CNT", 5, "0 to 31")])
add(["slrp"], ["lc"], "C", "C.4.2", 25, [
    ("LM_LC_Distance", 15, DIST_KM + "\nThis is the start distance of the LC gate from reference position."),
    ("LM_LC_ID_Numeric", 10, "0: Invalid\n1 – 1021: LC Gate Number\n1022: LC Gate Number other than 1 to "
     "1022 - out of range (Display xx on DMI)\n1023: Spare"),
    ("LM_LC_ID_Alpha_Suffix", 3, "000 No suffix\n001 a\n010 b\n011 c\n100 d\n101 e\n"
     "110 Out of Range (Display xx on DMI)\n111 Spare"),
    ("LM_LC_Manning_Type", 1, "0 : Manned, 1 : Unmanned"),
    ("LM_LC_Class", 3, "000 Spl\n001 A\n000 B1\n011 B2\n100 B (where not specified in terms of B1/B2)\n"
     "101 C\n110 D\n111 Spare"),
    ("LM_LC_Auto_Whistling_Enabled", 1, "0 : No, 1 : Yes"),
    ("LM_LC_Auto_Whistling_Type", 2, "00 Distance Based\n01 Time Based (Not Used)\n"
     "10 Configured Pattern Based (Not Used)\n11 Spare")],
    note="LM_LC_Class: the annexure's table prints 000 for both Spl and B1.")

# ---- Turnout Speed Profile ----
add(["slrp"], ["TO_CNT"], "C", "C.4.2", 26, [("TO_CNT", 2,
    "Number of turnouts from reference position\n0: No turnouts\n1-3: No.of turnouts follow")])
add(["slrp"], ["to"], "C", "C.4.2", 26, [
    ("TO_SPEED", 5, "00000 Not Used\n00001 Upto 5 kmph\n00010 Upto 10 kmph\n00011 Upto 15 kmph\n--\n--\n"
     "10010 Upto 90 Kmph\n10011-11110 Reserved for future use\n11111 Unrestricted"),
    ("DIFF_DIST_TO", 15, "Only If TO_SPEED = restricted, DIFF_DIST_TO variable follow.\n"
     "Starting Distance of the turnout from last reference RFID. Value in meters i.e. ranging from "
     "0 – 32.76 km"),
    ("TO_SPEED_REL_DIST", 12, "Only If TO_SPEED = restricted, DIFF_DIST_TO variable follow.\n"
     "Turnout release distance. Value in meters i.e. ranging from 0 - 4095 m. Value to be given upto end "
     "of turnout or upto other location will be defined by railways.")])

# ---- Tag Linking Information ----
add(["slrp"], ["DIST_DUP_TAG"], "C", "C.4.2", 27, [("DIST_DUP_TAG", 4,
    "Distance between Main and duplicate tag. 0000 shall be sent when the tags are placed closer than "
    "1 meter. 1111 is invalid\nIn a yard, the distance between main and duplicate tag is mentioned in the "
    "RFID Tag data format. This distance shall be kept common for a yard and shall be sent by a stationary "
    "KAVACH in TLI subpacket.")])
add(["slrp"], ["ROUTE_RFID_CNT"], "C", "C.4.2", 27, [("ROUTE_RFID_CNT", 6,
    "List of expected approaching RFID tags from reference position up to the End of Authority. Station "
    "updates the new list only when required.\n0: No tag shall be crossed by Onboard KAVACH. eg: In "
    "approach of danger signal.")])
add(["slrp"], ["tag"], "C", "C.4.2", 28, [
    ("DIST_NXT_RFID", 11, "Distance of next RFID from previous RFID (first tag will be from last reference "
     "RFID) in meters i.e. 2047 meter."),
    ("NXT_RFID_TAG_ID", 10, "Next RFID Tag ID"),
    ("DUP_TAG_DIR", 1, "Linking Direction of Duplicate Tag w.r.t Main Tag\n0: Duplicate Tag in Nominal "
     "Direction (+)/No Linking distance correction is required for T-Tag and A-Tag\n1: Duplicate Tag in "
     "Reverse Direction (-)")])
add(["slrp"], ["LOC_RESET"], "C", "C.4.2", 28, [("ABS_LOC_RESET", 1,
    "0–No Locational Error (The following bits will not be padded)\n1–Location Correction (New Section)\n"
    "Location shall get corrected in block section after 100m from Advance Starter. Onboard shall not "
    "apply brakes due to any of these reasons. When this information is not available, linking distance "
    "given in N-tag shall be used to avoid abnormal train trip due to location correction.\n"
    "Station shall able to transmit MA, SSP, TSR and maintain radio communication even after location "
    "reset.\nOnly If ABS_LOC_RESET>0, below variables follow.\nIn Given MA, single location reset is "
    "considered.")])
add(["slrp"], ["START_DIST_TO_LOC_RESET"], "C", "C.4.2", 29, [("START_DIST_TO_LOC_RESET", 15,
    "This is the start distance of the Normal tag (from the Onboard current location) in which location "
    "correction is done. Value in meters.")])
add(["slrp"], ["ADJ_LOCO_DIR"], "C", "C.4.2", 29, [("ADJ_LOCO_DIR", 2,
    "This is expected Onboard direction after passing location correction N-tag\n00 – Not Known\n"
    "01 – Nominal\n10 – Reverse\n11 – Deduce from Tags")])
add(["slrp"], ["ABS_LOC_CORRECTION"], "C", "C.4.2", 29, [("ABS_LOC_CORRECTION", 23,
    "This is the new absolute location from Adjustment/Junction Tag location correction")])
add(["slrp"], ["ADJ_LINE_CNT"], "C", "C.4.2", 29, [("ADJ_LINE_CNT", 3,
    "Adjacent line TINs along the MA for unusual stoppage detection.\n0: No adjacent lines, Self block "
    "section TIN will follow.\n1-5: Number of Adjacent lines including occupied self block section TIN.\n"
    "6: Reserved\n7: unknown")])
add(["slrp"], ["line_tin", "TIN"], "C", "C.4.2", 29, [("LINE_TIN", 9,
    "Self and Adjacent Line TIN\nOnly If ADJ_LINE_CNT = 0 to 5, LINE_TIN variable will follow.")])

# ---- Track Condition data ----
add(["slrp"], ["TRACKCOND_CNT"], "C", "C.4.2", 30, [("TRACKCOND_CNT", 4,
    "Track condition in MA region from reference RFID")])
add(["slrp"], ["tc"], "C", "C.4.2", 30, [
    ("TRACKCOND_TYPE", 4, "0000: Not used\n0001: Dead Stop\n0010: Radio hole (MA is valid upto Comm. fail "
     "time out)\n0011: Non stopping area\n0100: Tunnel stopping area\n0101: Powerless section (Neutral "
     "section)\n0110: Sound horn\n0111: Reversing area\n1000: Fouling Mark location\n1001: KAVACH "
     "Territory Exit. (Not to validate RFID linking beyond this location).\n1010 to 1111: Reserved for "
     "future use"),
    ("START_DIST_TRACKCOND", 15, "Start distance to Track condition from reference RFID. " + DIST_KM),
    ("LENGTH_TRACKCOND", 15, "Length of the Track condition. " + DIST_KM)])

# ---- Temporary Speed Restrictions ----
add(["slrp"], ["TSR_STATUS"], "C", "C.4.2", 31, [("TSR_STATUS", 2,
    "00 – No applicable TSR for the current MA\n01 – No Latest TSR Information (Onboard KAVACH shall "
    "transit to SR Mode, No MA to be extended by Stationary KAVACH).\n10 – Latest TSR Information\n"
    "11 – Reserved")])
add(["slrp"], ["TSR_Info_CNT"], "C", "C.4.2", 31, [("TSR_Info_CNT", 5, "0 to 31")])
add(["slrp"], ["tsr"], "C", "C.4.2", 31, [
    ("TSR_ID", 8, "This is the ID of TSR received from TSR management system."),
    ("TSR_Distance", 15, "This is the distance to TSR starting point from reference RFID. " + DIST_KM),
    ("TSR_Length", 15, "Length of TSR. Value in meters i.e. ranging from 0 – 32.76km"),
    ("TSR_Class", 1, "0 – Universal Speed\n1 – Classified Speed"),
    ("TSR_Universal_Speed", 6, "only if Q_TSR_CLASS = 0, LM_TSR_Universal_Speed variable follow\n"
     "0 Dead stop\nN = 1 to 40: Speed in kmph = 5*N (5,10,15, ....,200 kmph)\n41 to 61 Reserved for "
     "future use\n62 8 kmph\n63 Unknown"),
    ("TSR_ClassA_Speed / TSR_ClassB_Speed / TSR_ClassC_Speed", 6,
     "only if LM_TSR_Class = 1, LM_TSR_ClassA_Speed (B, C) variable follow. Values are Same as "
     "LM_TSR_Universal_Speed."),
    ("TSR_Whistle", 2, "00: No Whistle\n01: Whistle blow\n10-11: Spare")])
add(["slrp"], ["MAC_CODE"], "C", "C.4.2", 32, [("LOCO_Specific_MAC_CODE", 32,
    "Calculated from starting field PACKET_TYPE to last Sub-Packet padding bits")])
add(["slrp"], ["CRC"], "C", "C.4.2", 32, [("PKT_CRC", 32, "Packet CRC")])

# ============================================================================
#  C.4.3: Onboard to Station regular packet (LSRP)
# ============================================================================
add(["lsrp"], ["PKT_TYPE"], "C", "C.4.3", 32, [("PKT_TYPE", 4, "1010 (10)\n0000: Undefined\n" + PKT_TYPE_V)])
add(["lsrp"], ["PKT_LENGTH"], "C", "C.4.3", 32, [("PKT_LENGTH", 7, LEN7)])
add(["lsrp"], ["FRAME_NUM"], "C", "C.4.3", 33, [("FRAME_NUM", 17, FRAME_NUM_T)])
add(["lsrp"], ["SOURCE_LOCO_ID"], "C", "C.4.3", 33, [("SOURCE_LOCO_ID", 20,
    "1 to 999999 (Separate Look up table for EMU/DEMU)\n0 is an invalid Loco ID.")])
add(["lsrp"], ["LOCO_VERSION"], "C", "C.4.3", 33, [("SOURCE_LOCO_VERSION", 3, LOCO_VER_T)])
add(["lsrp"], ["ABS_LOCO_LOC"], "C", "C.4.3", 33, [("ABS_LOCO_LOC", 23,
    "Absolute Location in meters shall comensate for Reader Offset in rear end and reader offset in front\n"
    "8388607 is invalid Location.")])
add(["lsrp"], ["L_DOUBTOVER"], "C", "C.4.3", 33, [("L_DOUBTOVER", 9,
    "This is the over-reading amount plus the 5 m location accuracy of RFID Tag + 5% odometery error. "
    "This information shall be used for distance supervision of targets on safe-side (eg. PSR, TSR, Rear "
    "End Collision etc.)")])
add(["lsrp"], ["L_DOUBTUNDER"], "C", "C.4.3", 33, [("L_DOUBTUNDER", 9,
    "This is the under-reading amount plus the 5 m location accuracy of RFID Tag + 5% odometery error. "
    "This information shall be used for supervision of location for discarding the Tag identified beyond "
    "this length (eg. Linking, Head on Collision etc.)")])
add(["lsrp"], ["TRAIN_INT"], "C", "C.4.3", 34, [("TRAIN_INT", 2,
    "Train Integrity status of the train\n00: No Train Integrity information available\n01: Train integrity "
    "confirmed by integrity monitoring device\n10: Train integrity confirmed by Loco Pilot\n11: Reserved\n"
    "(New, Future Use)")])
add(["lsrp"], ["TRAIN_LENGTH"], "C", "C.4.3", 34, [("TRAIN_LENGTH", 11,
    "0: Unidentified/ Invalid\n1 to 2047: Train length in mtrs (It shall include both L_doubt Over and "
    "L_doubt under).")])
add(["lsrp"], ["TRAIN_SPEED"], "C", "C.4.3", 34, [("TRAIN_SPEED", 9, SPEED_KMPH_T)])
add(["lsrp"], ["MOVEMENT_DIR"], "C", "C.4.3", 34, [("MOVEMENT_DIR", 2, MOVE_DIR_T)])
add(["lsrp"], ["EMERGENCY_STATUS"], "C", "C.4.3", 34, [("EMERGENCY_STATUS", 3,
    "000 No Emergency - Regular Packet\n001 Unusual Stoppage in block section\n010 SoS\n"
    "011 Roll Back Detected\n100 Head On Collision\n101 Rear End Collision\n110 Parting SoS\n111 Spare")])
add(["lsrp"], ["LOCO_MODE"], "C", "C.4.3", 34, [("LOCO_MODE", 4, MODE_T)])
add(["lsrp"], ["LAST_RFID_TAG"], "C", "C.4.3", 35, [("LAST_RFID_TAG", 10, "Tag ID of Last RFID Tag Read")])
add(["lsrp"], ["TAG_DUP"], "C", "C.4.3", 35, [("TAG_DUP", 1, "0: Main Tag\n1: Duplicate Tag")])
add(["lsrp"], ["TAG_LINK_INFO"], "C", "C.4.3", 35, [("TAG_LINK_INFO", 3,
    "000 No Tag missing\n001 Duplicate Tag missing\n010 Main Tag missing\n011 Both Tag missing\n"
    "100 Tag position interchanged\n101 Both Tags have same location info\n"
    "110 Intertag distance less than DIST_DUP_TAG\n111 Intertag distance greater than DIST_DUP_TAG")])
add(["lsrp"], ["TIN"], "C", "C.4.3", 35, [("TIN", 9,
    "Each TIN is 9 bits. Track identification number occupied by front end of onboard KAVACH.\n" + TIN9_T)])
add(["lsrp"], ["Brake_Applied"], "C", "C.4.3", 35, [("Brake_Applied", 3,
    "0 No over speed, No brakes by Kavach\n1 Over speed but no brakes by Kavach\n"
    "10 Normal Service Brake by Kavach (not to be sent when hardwire interface is not done. For ex: in "
    "EMUs and Trainsets )\n11 Full Service Brake by Kavach\n100 Emergency Brake by Kavach\n"
    "101 to 111 Spare")])
add(["lsrp"], ["NEW_MA_REPLY"], "C", "C.4.3", 36, [("NEW_MA_REPLY", 2,
    "0: No request for Shorten MA from Station KAVACH\n1: Request to Shorten MA granted\n"
    "2: Request to Shorten MA rejected\n3: reserved\n(New, Future Use)")])
add(["lsrp"], ["LAST_REF_PROFILE_NUM"], "C", "C.4.3", 36, [("LAST_REF_PROFILE_NUM", 4,
    "Indicates the last track profile number received. To ensure RFID linking and TSR data received and "
    "taken into cognizance by Onboard.\n0: Indicates no track profile data with Onboard KAVACH in given "
    "MA. On receipt of Access Authority Packet, the onboard KAVACH shall send ‘0000’ retaining the profile "
    "already available for speed supervision.")])
add(["lsrp"], ["SIG_OV"], "C", "C.4.3", 36, [("SIG_OV", 1,
    "Request for OS MA to pass the approaching signal at danger when Authority to Proceed is available "
    "with LP.\n0: Signal Override Inactive\n1: Signal Override Active")])
add(["lsrp"], ["Info_Ack"], "C", "C.4.3", 36, [("Info_Ack", 4,
    "0: No Ack\n1: Loco Specific SoS Ack by LP\n2: FS to LS Ack by LP\n3: LS to SR Ack by LP\n"
    "4: FS to SR Ack by LP\n5: OS to SR Ack by LP\n6: OV to SR Ack by LP\n7: Trip Ack by LP\n"
    "8: PTRIP to SR Ack by LP\n9: Auto horn Ack by LP\n10: Train Length Measurement (TLM) Start packet "
    "received Ack from Onboard KAVACH\n11: TLM End packet received Ack from Onboard KAVACH\n"
    "12: Unusual Stoppage Ack by LP\n13: Manual SoS Ack by LP\n14: Spare\n15: Spare\n"
    "When event is started, flag is to set and when event ends flag is to be reset. The Ack shall be sent "
    "for minimum 5 (five) cycle or as long as flag is set high. The expected functionality in the "
    "Stationary KAVACH (SVK), is to log the event in case of mode transitions. In cases of SoS "
    "acknowledgement, it would be released by SVK. In case of TLM Start/End acknowledgement, STCAS would "
    "act on transmission of TLM. Acknowledgement by LP for onboard specific sos and TLM Start/End can "
    "occur simultaneously.")])
add(["lsrp"], ["Loco_Health", "Health_Raw_Stale"], "C", "C.4.3", 37, [("Loco_Health_Status", 6,
    "(Only for NMS Logging and report generation)\nOnboard Kavach health shall be prepared for length of "
    "24bits and same to be included in each radio packet as per below procedure. Each bit indicates "
    "status of each sub system in the OnboardKavach unit\n"
    "Frame Number in Binary xxxx xxxx xxxx x001: First 6 bits of Onboard Health\n"
    "xxxx xxxx xxxx x011: Second 6 bits of Onboard Health\n"
    "xxxx xxxx xxxx x101: Third 6 bits of Onboard Health\n"
    "xxxx xxxx xxxx x111: Fourth 6 bits of Onboard Health\n"
    "Onboard Kavach Health (as prescribed in Clause 6.1.24 of Annexure-G)")])
add(["lsrp"], ["MAC_CODE"], "C", "C.4.3", 38, [("MAC_CODE", 32,
    "Calculated from PKT_TYPE to Onboard_Health_Status fields")])
add(["lsrp"], ["CRC"], "C", "C.4.3", 38, [("PKT_CRC", 32, "Packet CRC")])

# ============================================================================
#  C.4.4: Access Authority Packet (AAP)
# ============================================================================
add(["aap"], ["PKT_TYPE"], "C", "C.4.4", 38, [("PKT_TYPE", 4, "1011 (11)\n" + PKT_TYPE_V)])
add(["aap"], ["PKT_LENGTH"], "C", "C.4.4", 38, [("PKT_LENGTH", 7, LEN7)])
add(["aap"], ["FRAME_NUM"], "C", "C.4.4", 38, [("FRAME_NUM", 17, FRAME_NUM_T)])
add(["aap"], ["SOURCE_STN_ID"], "C", "C.4.4", 39, [("SOURCE_STN_ILC_IBS_ID", 16, STN_ID_T)])
add(["aap"], ["STN_VERSION"], "C", "C.4.4", 39, [("SOURCE_STN_ILC_IBS_VERSION", 3, STN_VER_T)])
add(["aap"], ["STN_ILC_IBS_LOC"], "C", "C.4.4", 39, [("STN_ILC_IBS_LOC", 23, "Absolute Location in meters")])
add(["aap"], ["DEST_LOCO_ID"], "C", "C.4.4", 39, [("DEST_LOCO_ID", 20,
    "1 to 999999 (Separate Look up table for EMU/DEMU")])
FREQ_T = ("0 FDMA Not Used\n1 to 2560 Base Frequency: 406 MHz{conf} Allotted Channel Frequencies "
          "at 25kHz space is Channel-1: 406 + 1*0.025 = 406.025 MHz Channel-2: 406 + 2*0.025 =406.050 MHz "
          "-- Channel-2560: 406 + 2560*0.025 = 470.000 MHz\n2561 to 4093 Reserved for future use\n"
          "4094 Other Radio Communication systems used like WiFi/LTE/4G/5G Networks\n4095 Not to be used")
add(["aap"], ["Allotted_UpLink_Freq"], "C", "C.4.4", 39, [("Allotted_UpLink_Freq", 12,
    "Allotted Frequency Channel for UpLink (L-Kavach to S-Kavach frequency)\n" + FREQ_T.format(conf=" (Configurable)"))])
add(["aap"], ["Allotted_DownLink_Freq"], "C", "C.4.4", 39, [("Allotted_DownLink_Freq", 12,
    "Allotted Frequency Channel for DownLink (S-Kavach to L-Kavach frequency)\n" + FREQ_T.format(conf=""))])
add(["aap"], ["Allotted_TDMA_Timeslot"], "C", "C.4.4", 40, [("Allotted_TDMA_Timeslot", 7,
    "0 Not nominated\n1 to 68 Exact Time slot shall be sent by stationary KAVACH excluding reserved slot "
    "in Frame")])
add(["aap"], ["STN_RND_NUM_RS"], "C", "C.4.4", 40, [("STN_RND_NUM_RS", 16,
    "On reception of Access Request Packet from Onboard KAVACH Unit, Stationary KAVACH unit generates its "
    "own Random Number (RS).")])
add(["aap"], ["STN_TDMA"], "C", "C.4.4", 40, [("STN_TDMA", 7,
    "0 to 68 Station TDMA slot time in p-markers to capture RSSI.\n100 to 125 Reserved for future use\n"
    "126 Other Radio Communication systems used like WiFi/LTE/4G/5G Networks\n127 Not to be used")])
add(["aap"], ["MAC_CODE"], "C", "C.4.4", 40, [("MAC_CODE", 32, "Calculated for PKT_TYPE to station TDMA")])
add(["aap"], ["CRC"], "C", "C.4.4", 40, [("PKT_CRC", 32, "Packet CRC")])

# ============================================================================
#  C.4.5: Additional Emergency Packet (AEP)
# ============================================================================
add(["aep"], ["PKT_TYPE"], "C", "C.4.5", 41, [("PKT_TYPE", 4, "1100 (12)\n" + PKT_TYPE_V)])
add(["aep"], ["PKT_LENGTH"], "C", "C.4.5", 41, [("PKT_LENGTH", 7, LEN7)])
add(["aep"], ["FRAME_NUM"], "C", "C.4.5", 41, [("FRAME_NUM", 17, FRAME_NUM_T)])
add(["aep"], ["SOURCE_STN_ILC_IBS_ID"], "C", "C.4.5", 41, [("SOURCE_STN_ILC_IBS_ID", 16, STN_ID_T)])
add(["aep"], ["SOURCE_STN_ILC_IBS_VERSION"], "C", "C.4.5", 41, [("SOURCE_STN_ILC_IBS_VERSION", 3, STN_VER_T)])
add(["aep"], ["STN_ILC_IBS_LOC"], "C", "C.4.5", 41, [("STN_ILC_IBS_LOC", 23, "Absolute Location in meters")])
add(["aep"], ["GEN_SOS_CALL"], "C", "C.4.5", 41, [("GEN_SOS_CALL", 1,
    "0 No Station Manual SoS\n1 General SoS Call generated by Stationary Unit Conditions: Manual operation "
    "of SOS buttons provided on SOIP.")])
add(["aep"], ["CRC"], "C", "C.4.5", 42, [("PKT_CRC", 32, "Packet CRC")])

# ============================================================================
#  C.4.6: Access Request Packet (ARP)
# ============================================================================
add(["arp", "arprecv"], ["PKT_TYPE"], "C", "C.4.6", 42, [("PKT_TYPE", 4, "1101 (13)\n0000: Undefined\n" + PKT_TYPE_V)])
add(["arp", "arprecv"], ["PKT_LENGTH"], "C", "C.4.6", 42, [("PKT_LENGTH", 7, LEN7)])
add(["arp", "arprecv"], ["FRAME_NUM"], "C", "C.4.6", 42, [("FRAME_NUM", 17, FRAME_NUM_T)])
add(["arp", "arprecv"], ["SOURCE_LOCO_ID"], "C", "C.4.6", 42, [("SOURCE_LOCO_ID", 20, "1 to 999999")])
add(["arp", "arprecv"], ["LOCO_VERSION"], "C", "C.4.6", 43, [("SOURCE_LOCO_VERSION", 3, LOCO_VER_T)])
add(["arp", "arprecv"], ["ABS_LOCO_LOC"], "C", "C.4.6", 43, [("ABS_LOCO_LOC", 23, "Absolute Location in meters")])
add(["arp", "arprecv"], ["TRAIN_LENGTH"], "C", "C.4.6", 43, [("TRAIN_LENGTH", 11,
    "0: Unidentified/ Invalid\n1 to 2047: Train length in mtrs")])
add(["arp", "arprecv"], ["TRAIN_SPEED"], "C", "C.4.6", 43, [("TRAIN_SPEED", 9, SPEED_KMPH_T)])
add(["arp", "arprecv"], ["MOVEMENT_DIR"], "C", "C.4.6", 43, [("MOVEMENT_DIR", 2,
    "00 Traffic Direction not established / unidentified\n01 Nominal (Normally Traffic Direction as UP)\n"
    "10 Reverse (Normally Traffic Direction as DOWN)\n11 Reserved for future use")])
add(["arp", "arprecv"], ["EMERGENCY_STATUS"], "C", "C.4.6", 43, [("EMERGENCY_STATUS", 3,
    "000 No Emergency - Regular Packet\n001 Side Collision (Unusual Stoppage)\n010 SoS\n"
    "011 Roll Back Detected\n100 Head On Collision\n101 Rear End Collision\n110 Parting SoS\n111 Spare")])
add(["arp", "arprecv"], ["LOCO_MODE"], "C", "C.4.6", 43, [("LOCO_MODE", 4, MODE_T)])
add(["arp", "arprecv"], ["APPROACHING_STN_ID"], "C", "C.4.6", 44, [("Approaching Station ID", 16,
    "Approaching Station ID as received from Tag")])
add(["arp", "arprecv"], ["LAST_RFID_TAG"], "C", "C.4.6", 44, [("LAST_RFID_TAG", 10,
    "Tag ID of Last RFID Tag Read – The combination of Approaching Station ID and LAST_RFID_TAG shall be "
    "used for detecting Head on Collision or Rear End Collision. For this purpose, LAST_Station_ID shall "
    "be also be used by the Onboard KAVACH as read from the Tag.")])
add(["arp", "arprecv"], ["TIN"], "C", "C.4.6", 44, [("TIN", 9, "TIN programmed in last RFID tag\n" + TIN9_T)])
add(["arp", "arprecv"], ["Longitude", "lon_d", "lon_m", "lon_s"], "C", "C.4.6", 44, [("Longitude", 21,
    "-180 to +180\nSigned. Degrees: First nine bits. Minutes: six bits. Seconds: six bits.")])
add(["arp", "arprecv"], ["Latitude", "lat_d", "lat_m", "lat_s"], "C", "C.4.6", 44, [("Latitude", 20,
    "-90 to +90\nSigned. Degrees: First eight bits. Minutes: six bits. Seconds: six bits.")])
add(["arp", "arprecv"], ["LOCO_RND_NUM_RL"], "C", "C.4.6", 44, [("LOCO_RND_NUM_RL", 16,
    "Onboard Random number for session request. Change of this value by Onboard KAVACH indicates that "
    "requesting a fresh session from onboard KAVACH.")])
add(["arp", "arprecv"], ["CRC"], "C", "C.4.6", 44, [("PKT_CRC", 32, "Packet CRC")])

# ============================================================================
#  Annexure D: RFID tags (packet "rfid": the decoder, the Tag Builder)
# ============================================================================
SECT_T = ("Indicates whether the territory encountered by the Loco falls in Station vicinity or Block "
          "section in its direction of travel\n00-Station Section\n01-Absolute block\n10-Automatic section\n"
          "11-Virtual Block\n(01, 10 and 11 – to be used for collision detection)")
PLACE_T = ("Denotes the placement of the tag\n‘0000’ – In line section\n‘0001’ – At Signal foot in Nominal "
           "direction\n‘0010’ – At Signal foot in Reverse direction\n‘0011’ – At Turnout\n‘0100’ – At KAVACH "
           "territory exit in Nominal direction\n‘0101’ – At KAVACH territory exit in Reverse direction\n"
           "‘0110’ – At Signal foot in both directions\n“0111” – At KAVACH territory exit in both directions\n"
           "“1000” – At Dead Stop in Nominal Direction\n“1001” – At Dead Stop in Reverse Direction\n"
           "“1010-1111” Reserved")
ABS_T = ("(1111111 11111111 11111111 : Not Applicable)\nGeographical location of the RFID Tag in terms of "
         "meters as per Railway metrics or Location referred in the Signal Interlocking Plan of the "
         "respective stations.\nValue ranges from 0 to 83,88,607")
UNIQUE_T = "Unique ID of the RFID Tag. Value ranges from 1 to 1023"
DUP_T = "0- Main Tag\n1- Duplicate Tag"
COMM_T = "0- Required\n1-Not required"
CRC30_T = "Compute CRC (Cyclic Redundancy Checksum) to check the data integrity. 30 Bit CCITT CRC"

# ---- D 2. Normal Tag (type 9) ----
N = [9]
add(["rfid"], ["type"], "D", "D 2. Normal Tag", 4, [("Type of Tag (1001: Normal Tag)", 4,
    "X3 - X0\nThis field denotes the type of Tag. The decimal value is 9")], tag_types=N)
add(["rfid"], ["version"], "D", "D 2. Normal Tag", 4, [("Version", 2,
    "X5-X4\n0: KAVACH Spec 3.2\n1: KAVACH Spec 4.0\n2,3: Reserved for future use")], tag_types=N)
add(["rfid"], ["unique"], "D", "D 2. Normal Tag", 4, [("Unique ID of RFID Tag Set", 10,
    "X15– X6\n" + UNIQUE_T)], tag_types=N)
add(["rfid"], ["abs_loc"], "D", "D 2. Normal Tag", 4, [("Absolute Loc in meters", 23, "X38– X16\n" + ABS_T)],
    tag_types=N)
TIN_NOM_N = ("X46 – X39\nTrack Identification number encountered while traversing in Nominal direction"
             "(Incremental direction of absolute location)\nValue ranges from 0 to 255\n0: TIN value to be "
             "used at KAVACH territory exit in valid direction")
TIN_REV_N = ("X54– X47\nTrack Identification number encountered while traversing in Reverse direction "
             "(Decremental direction of absolute location).\nValue ranges from 0 to 255\n0: TIN value to "
             "be used at KAVACH territory exit in valid direction")
add(["rfid"], ["tin_nom"], "D", "D 2. Normal Tag", 4, [("TIN in Nominal Direction", 8, TIN_NOM_N)], tag_types=N)
add(["rfid"], ["tin_rev"], "D", "D 2. Normal Tag", 4, [("TIN in Reverse Direction", 8, TIN_REV_N)], tag_types=N)
add(["rfid"], ["tin"], "D", "D 2. Normal Tag", 4, [("TIN in Nominal Direction", 8, TIN_NOM_N),
                                                    ("TIN in Reverse Direction", 8, TIN_REV_N)], tag_types=N)
STN_NOM = ("Y6-Y0 & X63- X55\nStation ID corresponding to Tag placement,\nNote: 0 to be programmed if Tag "
           "is not mapped to any station")
STN_REV = ("Y22– Y7\nStation ID corresponding to Tag placement,\nNote: 0 to be programmed if Tag is not "
           "mapped to any station")
add(["rfid"], ["stn_nom"], "D", "D 2. Normal Tag", 4, [("Station ID in Nominal direction", 16, STN_NOM)], tag_types=N)
add(["rfid"], ["stn_rev"], "D", "D 2. Normal Tag", 4, [("Station ID in Reverse direction", 16, STN_REV)], tag_types=N)
add(["rfid"], ["station"], "D", "D 2. Normal Tag", 4, [("Station ID in Nominal direction", 16, STN_NOM),
                                                        ("Station ID in Reverse direction", 16, STN_REV)], tag_types=N)
add(["rfid"], ["sect_nom"], "D", "D 2. Normal Tag", 4, [("Section type in Nominal direction", 2,
    "Y24 – Y23\n" + SECT_T)], tag_types=N)
add(["rfid"], ["sect_rev"], "D", "D 2. Normal Tag", 5, [("Section type in Reverse direction", 2,
    "Y26 – Y25\n" + SECT_T)], tag_types=N)
add(["rfid"], ["section"], "D", "D 2. Normal Tag", 4, [
    ("Section type in Nominal direction", 2, "Y24 – Y23\n" + SECT_T),
    ("Section type in Reverse direction", 2, "Y26 – Y25 (as above)")], tag_types=N)
add(["rfid"], ["placement"], "D", "D 2. Normal Tag", 5, [("Tag placement", 4, "Y30 – Y27\n" + PLACE_T)],
    tag_types=N, note="Annexure D: “The Normal tag shall cater the requirement of the Signal foot Tag, Turn "
    "Out Tag, Exit Tag, Adjustment/Junction Tag as per the placement of the tag.”")
add(["rfid"], ["duplication"], "D", "D 2. Normal Tag", 5, [("Tag Duplication", 1, "Y31\n" + DUP_T)], tag_types=N)
add(["rfid"], ["comm_nom"], "D", "D 2. Normal Tag", 5, [("Communication in Nominal Direction", 1, "Y32\n" + COMM_T)],
    tag_types=N)
add(["rfid"], ["comm_rev"], "D", "D 2. Normal Tag", 5, [("Communication in Reverse Direction", 1, "Y33\n" + COMM_T)],
    tag_types=N)
add(["rfid"], ["comm"], "D", "D 2. Normal Tag", 5, [("Communication in Nominal Direction", 1, "Y32\n" + COMM_T),
                                                     ("Communication in Reverse Direction", 1, "Y33\n" + COMM_T)],
    tag_types=N)

# ---- D 3. LC Tag (type 10) ----
L = [10]
add(["rfid"], ["type"], "D", "D 3. LC Tag", 6, [("Type of Tag (1010 : LC Tag)", 4,
    "X3 - X0\nThis field denotes the type of Tag. The decimal value is 10")], tag_types=L)
add(["rfid"], ["version"], "D", "D 3. LC Tag", 6, [("Version", 2,
    "X5-X4\n0-TCAS Spec 3.2\n1-TCAS (KAVACH) Spec 4.0\n2,3 – Reserved for future use")], tag_types=L)
add(["rfid"], ["unique"], "D", "D 3. LC Tag", 6, [("Unique ID of RFID Tag Set", 10, "X15– X6\n" + UNIQUE_T)],
    tag_types=L)
add(["rfid"], ["abs_loc"], "D", "D 3. LC Tag", 6, [("Absolute Loc in meters", 23, "X38– X16\n" + ABS_T)], tag_types=L)
TIN_NOM_L = ("X46 – X39\nTrack Identification number encountered while traversing in Nominal direction "
             "(Incremental direction of absolute location)\nValue ranges from 0 to 255\n0: TIN value to be "
             "used in Non-KAVACH territory or at KAVACH territory exit in valid direction")
TIN_REV_L = ("X54 – X47\nTrack Identification number encountered while traversing in Reverse direction "
             "(Decremental direction of absolute location)\nValue ranges from 0 to 255\n0: TIN value to be "
             "used in Non-KAVACH territory or at KAVACH territory exit in valid direction")
add(["rfid"], ["tin_nom"], "D", "D 3. LC Tag", 6, [("TIN in Nominal Direction", 8, TIN_NOM_L)], tag_types=L)
add(["rfid"], ["tin_rev"], "D", "D 3. LC Tag", 6, [("TIN in Reverse Direction", 8, TIN_REV_L)], tag_types=L)
add(["rfid"], ["tin"], "D", "D 3. LC Tag", 6, [("TIN in Nominal Direction", 8, TIN_NOM_L),
                                               ("TIN in Reverse Direction", 8, TIN_REV_L)], tag_types=L)
add(["rfid"], ["sect_nom"], "D", "D 3. LC Tag", 6, [("Section type in Nominal direction", 2, "X56 – X55\n" + SECT_T)],
    tag_types=L)
add(["rfid"], ["sect_rev"], "D", "D 3. LC Tag", 6, [("Section type in Reverse direction", 2, "X58 – X57\n" + SECT_T)],
    tag_types=L)
add(["rfid"], ["section"], "D", "D 3. LC Tag", 6, [("Section type in Nominal direction", 2, "X56 – X55\n" + SECT_T),
                                                   ("Section type in Reverse direction", 2, "X58 – X57 (as above)")],
    tag_types=L)
add(["rfid"], ["placement"], "D", "D 3. LC Tag", 7, [("Tag placement", 4,
    "X62 – X59\nDenotes the placement of the tag (G-tag can to be used when approaching an LC gate instead "
    "of N-Tag)\n" + PLACE_T.split("\n", 1)[1])], tag_types=L)
add(["rfid"], ["lc_approach"], "D", "D 3. LC Tag", 7, [("LC Gate Approach tag in direction of applicability", 2,
    "Y0-X63\nDenotes whether the Tag is placed in KAVACH or Non KAVACH territory. If placed in Non KAVACH "
    "territory, parameters for nominal direction only to be used\n00: KAVACH\n01: NON KAVACH First\n"
    "10: NON KAVACH Second\n11: Spare")], tag_types=L)
add(["rfid"], ["applicable_dir"], "D", "D 3. LC Tag", 7, [("Applicable Direction", 1,
    "Y1\nDenotes the applicable direction of the LC gate (0=Nominal, 1=Reverse)")], tag_types=L)
add(["rfid"], ["gate_id"], "D", "D 3. LC Tag", 7, [("Gate ID", 10,
    "Y11-Y2\nUnique ID of the LC Gate as per Railway data. Value ranges from 1 to 1023")], tag_types=L)
add(["rfid"], ["gate_alpha"], "D", "D 3. LC Tag", 7, [("LC Gate ID (alpha)", 3,
    "Y14-Y12\nUnique ID of the LC Gate as per Railway data. Value ranges from 1 to 1023\n000 : None,\n"
    "001: a,\n010: b,\n011: c,\n100: d,\n101: e,\n110: Out of range (display xx on DMI)\n111: Spare")],
    tag_types=L)
add(["rfid"], ["gate_type"], "D", "D 3. LC Tag", 8, [("Gate Type", 1,
    "Y15\nDenotes the type of LC gate (0=manned,1=unmanned)")], tag_types=L)
add(["rfid"], ["dist_to_gate"], "D", "D 3. LC Tag", 8, [("Distance to Gate", 10,
    "Y25-Y16\nDenotes the distance from this tag to the approaching LC gate in terms of meters")], tag_types=L)
add(["rfid"], ["auto_whistle"], "D", "D 3. LC Tag", 8, [("Auto whistling", 1,
    "Y26\nDenotes whether Auto whistling is required (0=No, 1= Yes)")], tag_types=L)
add(["rfid"], ["whistle_type"], "D", "D 3. LC Tag", 8, [("Type of Auto whistling", 1,
    "Y27\nDenotes type of Auto whistling required\n0= Distance based Auto-whistling\n"
    "1= Time based Auto Whistling")], tag_types=L)
add(["rfid"], ["duplication"], "D", "D 3. LC Tag", 8, [("Tag Duplication", 1, "Y31\n" + DUP_T)], tag_types=L)
add(["rfid"], ["comm_nom"], "D", "D 3. LC Tag", 8, [("Communication in Nominal Direction", 1, "Y32\n" + COMM_T)],
    tag_types=L)
add(["rfid"], ["comm_rev"], "D", "D 3. LC Tag", 8, [("Communication in Reverse Direction", 1, "Y33\n0-Required\n1-Not required")],
    tag_types=L)
add(["rfid"], ["comm"], "D", "D 3. LC Tag", 8, [("Communication in Nominal Direction", 1, "Y32\n" + COMM_T),
                                                ("Communication in Reverse Direction", 1, "Y33\n0-Required\n1-Not required")],
    tag_types=L)

# ---- D 4. Adjacent Line Tag (type 11) ----
A = [11]
add(["rfid"], ["type"], "D", "D 4. Adjacent Line tag", 9, [("Type of Tag (1011: Adjacent Line Tag )", 4,
    "X3 – X0\nThis field denotes the type of Tag. The decimal value is 11")], tag_types=A)
add(["rfid"], ["version"], "D", "D 4. Adjacent Line tag", 9, [("Version", 2,
    "X5 - X4\n0: TCAS (KAVACH) Spec 3.2\n1: TCAS (KAVACH) Spec 4.0\n2,3: Reserved for future use")], tag_types=A)
add(["rfid"], ["unique"], "D", "D 4. Adjacent Line tag", 9, [("Unique ID of RFID Tag Set", 10,
    "X15 – X6\n" + UNIQUE_T)], tag_types=A)
add(["rfid"], ["abs_loc"], "D", "D 4. Adjacent Line tag", 9, [("Absolute Loc in meters", 23,
    "X38 – X16\n(1111111 11111111 11111111 : Not Applicable)\nGeographical location of the RFID Tag in terms "
    "of meters as per Railway metrics or Location referred in the Signal Interlocking Plan of the respective "
    "stations.\nValue ranges from 0 to 83,88,608")], tag_types=A,
    note="The adjacent line tag's table gives 83,88,608 where the other tags give 83,88,607.")
TIN_NOM_A = ("X46 – X39\nTrack Identification number encountered while traversing in Nominal direction"
             "(Incremental direction of absolute location)\nValue ranges from 0 to 255")
TIN_REV_A = ("X54 – X47\nTrack Identification number encountered while traversing in Reverse direction "
             "(Decremental direction of absolute location)\nValue ranges from 0 to 255")
add(["rfid"], ["tin_nom"], "D", "D 4. Adjacent Line tag", 9, [("TIN in Nominal Direction", 8, TIN_NOM_A)], tag_types=A)
add(["rfid"], ["tin_rev"], "D", "D 4. Adjacent Line tag", 9, [("TIN in Reverse Direction", 8, TIN_REV_A)], tag_types=A)
add(["rfid"], ["tin"], "D", "D 4. Adjacent Line tag", 9, [("TIN in Nominal Direction", 8, TIN_NOM_A),
                                                          ("TIN in Reverse Direction", 8, TIN_REV_A)], tag_types=A)
ADJ_T = "TIN number of adjacent lines Value range (1-255), 0=No adjacent Line TIN"
adj_bits = ["X62 – X55", "Y6-Y0 & X63", "Y14-Y7", "Y22-Y15", "Y30-Y23"]
for i in range(5):
    add(["rfid"], ["adj%d" % i], "D", "D 4. Adjacent Line tag", 9,
        [("Adjacent Line-%d TIN" % (i + 1), 8, adj_bits[i] + "\n" + ADJ_T)], tag_types=A)
add(["rfid"], ["adjacent_tin"], "D", "D 4. Adjacent Line tag", 9,
    [("Adjacent Line-1 to Adjacent Line-5 TIN", 40, "X62 – X55, Y6-Y0 & X63, Y14-Y7, Y22-Y15, Y30-Y23 "
      "(8 bits each)\n" + ADJ_T)], tag_types=A)
add(["rfid"], ["duplication"], "D", "D 4. Adjacent Line tag", 9, [("Tag Duplication", 1, "Y31\n" + DUP_T)], tag_types=A)

# ---- D 5. Adjustment/Junction Tag (type 12) ----
J = [12]
add(["rfid"], ["type"], "D", "D 5. Adjustment/Junction Tag", 10, [("Type of Tag (1100: Junction Tag V2.0)", 4,
    "X3 - X0\nThis field denotes the type of Tag. The decimal value is 12")], tag_types=J)
add(["rfid"], ["version"], "D", "D 5. Adjustment/Junction Tag", 10, [("Version", 2,
    "X5-X4\n0: TCAS Spec 3.2\n1: TCAS Spec 4.0\n2,3: Reserved for future use")], tag_types=J)
add(["rfid"], ["unique"], "D", "D 5. Adjustment/Junction Tag", 10, [("Unique ID of RFID Tag Set", 10,
    "X15– X6\n" + UNIQUE_T)], tag_types=J)
add(["rfid"], ["abs_loc_1"], "D", "D 5. Adjustment/Junction Tag", 10, [("Absolute Location-1 (in meters)", 23,
    "X38– X16\n" + ABS_T)], tag_types=J)
TIN1 = ("X46 – X39\nTrack Identification number associated towards Absolute location -1 Value ranges from 0 "
        "to 255\n0: TIN value to be used at TCAS territory exit in valid direction")
TIN2 = ("X54 – X47\nTrack Identification number associated towards Absolute location -2 Value ranges from 0 "
        "to 255\n0: TIN value to be used at TCAS territory exit in valid direction")
add(["rfid"], ["tin_nom"], "D", "D 5. Adjustment/Junction Tag", 10, [("TIN -1", 8, TIN1)], tag_types=J)
add(["rfid"], ["tin_rev"], "D", "D 5. Adjustment/Junction Tag", 10, [("TIN -2", 8, TIN2)], tag_types=J)
add(["rfid"], ["tin"], "D", "D 5. Adjustment/Junction Tag", 10, [("TIN -1", 8, TIN1), ("TIN -2", 8, TIN2)], tag_types=J)
add(["rfid"], ["abs_loc_2"], "D", "D 5. Adjustment/Junction Tag", 10, [("Absolute Location-2 in meters", 23,
    "Y13-Y0 & X63- X55\n(1111111 11111111 11111111 : Not Applicable)\nGeographical location of the RFID Tag "
    "in terms of meters as per Railway metrics or Location referred in the Signal Interlocking Plan of the "
    "respective stations.\nValue ranges from 0 to 83,88,607")], tag_types=J)
DIRC = ("000 – Reset Direction unknown. Derive direction from next tags\n001 – Location Correction, Loco "
        "travel direction Nominal and Next tag is in Nominal direction\n010 – Location Correction, Loco travel "
        "direction Nominal and Next tag is in Reverse direction\n011 – Location Correction, Loco travel "
        "direction Reverse and Next tag is in Nominal direction\n100 – Location Correction, Loco travel "
        "direction Reverse and Next tag is in Reverse direction\n110 – 111: Reserved")
add(["rfid"], ["dir_corr_1"], "D", "D 5. Adjustment/Junction Tag", 10,
    [("Direction Correction-1 (Absolute Location-1)", 3, "Y16- Y14\nDirection Reset toward absolute location-1\n" + DIRC)],
    tag_types=J)
add(["rfid"], ["dir_corr_2"], "D", "D 5. Adjustment/Junction Tag", 11,
    [("Direction Correction-2 (Absolute Location-2)", 3, "Y19- Y17\nDirection Reset toward absolute location-2\n" + DIRC)],
    tag_types=J)
add(["rfid"], ["loc_corr_type"], "D", "D 5. Adjustment/Junction Tag", 11, [("Location Correction Type", 1,
    "Y20\n0 – Adjustment in Absolute Location 1 – Reset in Absolute Location")], tag_types=J)
SECT12 = ("Indicates whether the territory encountered by the Loco falls in Station vicinity or Block section "
          "in its direction of travel\n00-Station Section\n01-Absolute block\n10-Autoblock\n11-Virtual Block\n"
          "(01, 10 and 11 – to be used for collision detection)")
add(["rfid"], ["sect_nom"], "D", "D 5. Adjustment/Junction Tag", 11, [("Section type-1", 2,
    "Y24 – Y23\nSection type toward absolute location-1\n" + SECT12)], tag_types=J)
add(["rfid"], ["sect_rev"], "D", "D 5. Adjustment/Junction Tag", 11, [("Section type -2", 2,
    "Y26 – Y25\nSection type toward absolute location-2\n" + SECT12)], tag_types=J)
add(["rfid"], ["section"], "D", "D 5. Adjustment/Junction Tag", 11, [
    ("Section type-1", 2, "Y24 – Y23\nSection type toward absolute location-1\n" + SECT12),
    ("Section type -2", 2, "Y26 – Y25\nSection type toward absolute location-2 (as above)")], tag_types=J)
add(["rfid"], ["duplication"], "D", "D 5. Adjustment/Junction Tag", 11, [("Tag Type", 1,
    "Y31\n0 – Main Tag 1– Duplicate tag")], tag_types=J)
COMM12 = "0 – Required 1– Not Required"
add(["rfid"], ["comm_nom"], "D", "D 5. Adjustment/Junction Tag", 11, [("Communication in Nominal direction", 1,
    "Y32\n" + COMM12)], tag_types=J)
add(["rfid"], ["comm_rev"], "D", "D 5. Adjustment/Junction Tag", 11, [("Communication in Reverse direction", 1,
    "Y33\n" + COMM12)], tag_types=J)
add(["rfid"], ["comm"], "D", "D 5. Adjustment/Junction Tag", 11, [
    ("Communication in Nominal direction", 1, "Y32\n" + COMM12),
    ("Communication in Reverse direction", 1, "Y33\n" + COMM12)], tag_types=J)

# ---- CRC, every tag type ----
add(["rfid"], ["CRC-30"], "D", "D 2.–5., D 6. CRC-30/CDMA", 5, [("CRC", 30,
    "Y63-Y34\n" + CRC30_T + "\nD 6. CRC-30/CDMA: Polynomial: x30+ x29+ x21 + x20 + x15 + x13 + x12 + x11 + "
    "x8 + x7 + x6 + x2 + x + 1; Width: 30 bits; Truncated Polynomial: 0x2030B9C7; Initial Remainder: "
    "0x3FFFFFFF; Final Xor Value: 0x3FFFFFFF.\nSample Data: 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, "
    "0x39 CRC-30 Value: 0x04C34ABF")],
    note="The Adjustment/Junction tag's table calls it “30 Bit CRC /CDMA”; the others “30 Bit CCITT CRC”. "
         "D 6 gives the algorithm.")

doc = {
    "_comment": "Field descriptions shown when hovering a field (session 301). Texts are copied from the "
                "annexures named in 'sources': only PDF line-break hyphens are rejoined; wording, values "
                "and the annexures' own typos are kept. 'names' are the names DLConsole shows (schema "
                "field, group or repeat names; a row's [n] index and ' (…)' suffix are ignored); 'field' is "
                "the annexure's name. 'note' is DLConsole's, never the annexure's. Built by "
                "schema/build_fielddocs.py.",
    "sources": {
        "C": {"title": "Annexure-C — Multiple Access Scheme & Radio Communication Protocol",
              "doc": "RDSO/SPN/196/2020 Version 4.0, Amdt-10, effective 27.06.2024"},
        "D": {"title": "Annexure-D — KAVACH RFID Tag Data Format",
              "doc": "RDSO/SPN/196/2020 Version 4.0, Amdt-7, effective 07.06.2024"},
    },
    "docs": docs,
}
out = sys.argv[1] if len(sys.argv) > 1 else "fielddocs.json"
with open(out, "w", encoding="utf-8") as f:
    json.dump(doc, f, ensure_ascii=False, indent=1)
    f.write("\n")
print(len(docs), "entries ->", out)
