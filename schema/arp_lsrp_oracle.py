"""arp_lsrp_oracle.py -- faithful port of capturedecoder.cpp decodeARP / decodeLSRP
(body only, cursor @ bit 80). The msg.* header envelope is emitted by describe()
in C++, not by these functions, so it is not part of this oracle."""
from engine import MsbCursor, to_signed

def _trainSpeed9(v):
    if v <= 400: return f"{v} km/h"
    if v == 511: return "unidentified"
    return f"{v} (reserved)"
def _moveDir(v):
    m = ["Unidentified","Nominal","Reverse","Reserved"]; return f"{v} ({m[v & 3]})"
def _emergency(v):
    m = ["No Emergency","Unusual Stoppage","SoS","Roll Back","Head-On Collision",
         "Rear-End Collision","Parting SoS","Spare"]
    return f"{v} ({m[v]})" if v < 8 else str(v)
def _locoMode(v):
    m = ["?","Stand_By","Staff_Responsible","Limited_Supervision","Full_Supervision",
         "Override","On_Sight","Trip","Post_Trip","Reverse","Shunting","Non_Leading",
         "System_Failure","Isolation"]
    return f"{v} ({m[v]})" if v < len(m) else str(v)

def decodeARP(b):
    c = MsbCursor(b, 80, (len(b) - 4) * 8)
    r = []
    r.append(("PKT_TYPE",          str(c.take(4))))
    r.append(("PKT_LENGTH",        str(c.take(7))))
    r.append(("FRAME_NUM",         str(c.take(17))))
    r.append(("SOURCE_LOCO_ID",    str(c.take(20))))
    r.append(("LOCO_VERSION",      str(c.take(3))))
    r.append(("ABS_LOCO_LOC",      f"{c.take(23)} m"))
    r.append(("TRAIN_LENGTH",      f"{c.take(11)} m"))
    r.append(("TRAIN_SPEED",       _trainSpeed9(c.take(9))))
    r.append(("MOVEMENT_DIR",      _moveDir(c.take(2))))
    r.append(("EMERGENCY_STATUS",  _emergency(c.take(3))))
    r.append(("LOCO_MODE",         _locoMode(c.take(4))))
    r.append(("APPROACHING_STN_ID", str(c.take(16))))
    r.append(("LAST_RFID_TAG",     str(c.take(10))))
    r.append(("TIN",               str(c.take(9))))
    d = to_signed(c.take(9), 9); m = c.take(6); s = c.take(6)
    r.append(("Longitude", f"{d}\u00b0 {m}' {s}\""))
    d = to_signed(c.take(8), 8); m = c.take(6); s = c.take(6)
    r.append(("Latitude",  f"{d}\u00b0 {m}' {s}\""))
    r.append(("LOCO_RND_NUM_RL", "0x%04x" % c.take(16)))
    return r

def decodeLSRP(b):
    c = MsbCursor(b, 80, (len(b) - 4) * 8)
    r = []
    r.append(("PKT_TYPE",          str(c.take(4))))
    r.append(("PKT_LENGTH",        str(c.take(7))))
    r.append(("FRAME_NUM",         str(c.take(17))))
    r.append(("SOURCE_LOCO_ID",    str(c.take(20))))
    r.append(("LOCO_VERSION",      str(c.take(3))))
    r.append(("ABS_LOCO_LOC",      f"{c.take(23)} m"))
    r.append(("L_DOUBTOVER",       f"{c.take(9)} m"))
    r.append(("L_DOUBTUNDER",      f"{c.take(9)} m"))
    r.append(("TRAIN_INT",         str(c.take(2))))
    r.append(("TRAIN_LENGTH",      f"{c.take(11)} m"))
    r.append(("TRAIN_SPEED",       _trainSpeed9(c.take(9))))
    r.append(("MOVEMENT_DIR",      _moveDir(c.take(2))))
    r.append(("EMERGENCY_STATUS",  _emergency(c.take(3))))
    r.append(("LOCO_MODE",         _locoMode(c.take(4))))
    r.append(("LAST_RFID_TAG",     str(c.take(10))))
    r.append(("TAG_DUP",           str(c.take(1))))
    r.append(("TAG_LINK_INFO",     str(c.take(3))))
    r.append(("TIN",               str(c.take(9))))
    r.append(("Brake_Applied",     str(c.take(3))))
    r.append(("NEW_MA_REPLY",      str(c.take(2))))
    r.append(("LAST_REF_PROFILE_NUM", str(c.take(4))))
    r.append(("SIG_OV",            str(c.take(1))))
    r.append(("Info_Ack",          str(c.take(4))))
    c.take(2)                                          # Spare
    # Loco_Health_Status: six bits whose meaning rotates with FRAME_NUM & 7
    # (firmware switch cases 1/3/5/7). Bit b of the 6-bit value is fault
    # 6*g + b, numbered from the LSB as the firmware's SET_BIT does. Even
    # frames have no case, so the field still holds the previous group's
    # bits and is reported raw rather than against a table.
    frame_num = int(r[2][1])
    health = c.take(6)
    grp = frame_num & 7
    if grp in _HEALTH_GROUPS:
        first, names = _HEALTH_GROUPS[grp]
        on = [names[b] for b in range(6) if (health >> b) & 1]
        r.append((f"Loco_Health (faults {first}-{first + 5})",
                  "; ".join(on) if on else "(none)"))
        for b in range(6):
            r.append((names[b], "1 (FAULT)" if (health >> b) & 1 else "0 (ok)"))
    else:
        r.append(("Health_Raw_Stale", "0x%02x" % health))
        r.append(("Health_Note", "not updated on even frames (stale)"))
    r.append(("MAC_CODE",          "0x%08x" % c.take(32)))
    return r

# FRAME_NUM & 7 -> (first fault number, names by bit position LSB-first).
_HEALTH_GROUPS = {
    1: (0,  ["B0  SYSTEM_INTERNAL_FAULT", "B1  SPEED_SENSOR1_FAULT",
             "B2  EB_DRIVE_FAULT", "B3  EB_APPLICATION_FEEDBACK_FAULT",
             "B4  RFID_READER1_LINK_FAIL", "B5  RFID_READER2_LINK_FAIL"]),
    3: (6,  ["B6  RADIO1_LINK_FAIL", "B7  RADIO2_LINK_FAIL",
             "B8  LP_OCIP_DMI1_LINK_FAIL", "B9  LP_OCIP_DMI2_LINK_FAIL",
             "B10 GPS1_PPS1_FAIL", "B11 GPS2_PPS2_FAIL"]),
    5: (12, ["B12 GPS1_VIEW_NOT_AVAILABLE", "B13 GPS2_VIEW_NOT_AVAILABLE",
             "B14 TAG_LINKING_INCORRECT", "B15 GSM1_FAULT",
             "B16 GSM2_FAULT", "B17 RADIO1_RSSI_WEAK"]),
    7: (18, ["B18 RADIO2_RSSI_WEAK", "B19 SESSION_KEY_MISMATCH",
             "B20 REMAINING_KEYS_LESS_THAN_5", "B21 BIU_CONNECTIVITY_FAULT",
             "B22 SPEED_SENSOR2_FAULT", "B23 CAB_INPUT_FAULT"]),
}
