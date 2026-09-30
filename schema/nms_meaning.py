"""nms_meaning.py -- faithful port of capturedecoder.cpp NMS value formatters
(nmsModuleName, nmsFormation, nmsHealthMeaning). Used both as the engine's
'nmsHealth' meaning hook and by the nmshlth oracle, so the parity test exercises
the schema-driven event-stream WALK (count/id/size/name/value), not the meaning."""

def nmsModuleName(i):
    m = ["VCC Mc-1","VCC Mc-2","VCC Mc-3","VCC Mc-4",
         "Input Card 1 Mc-1","Input Card 1 Mc-2","Input Card 1 Mc-3","Input Card 1 Mc-4",
         "Input Card 2 Mc-1","Input Card 2 Mc-2","Input Card 2 Mc-3","Input Card 2 Mc-4",
         "Output Card Mc-1","Output Card Mc-2","Analogue Card Mc-1","Analogue Card Mc-2",
         "Communication Card Mc-1","Communication Card Mc-2","Data logger Card Mc-1","Data logger Card Mc-2",
         "Rfid-Reader-1","Rfid-Reader-2","Radio-1","Radio-2","GPS-1","GPS-2","GSM-1","GSM-2",
         "Speed sensor-1","Speed sensor-2","DMI-1","DMI-2","VCC MC All","VCC Card-1","VCC Card-2",
         "Input Card-1","Input Card-1'","Input Card-2","Input Card-2'","Output Card-1",
         "Analogue Card-1","Data logger Card-1","Comm Card-1"]
    return m[i-1] if 1 <= i <= len(m) else "?"

def nmsFormation(v):
    f = ["?","Light Engine 120","Light Engine Multi 120","Pass 3-7 Coach 120",
         "Pass 8-13 Coach 120","Pass 14-20 Coach 120","Pass 21-27 Coach 120","Goods 59 BOXN Empty 75",
         "Goods 59 BOXN Half 75","Goods 59 BOXN Full 60","Goods 42 BCN Empty 75","Goods 42 BCN Half 75",
         "Goods 42 BCN Full 60","LE WAP5 170","WAP5-8LHB 170","LE WAP7 140"]
    return f[v] if 1 <= v <= 15 else f"{v}?"

def nmsHealthMeaning(eid, v):
    def rng(lo, hi): return lo <= v <= hi
    if eid in (1, 2):
        h = ["?","OK","Diagnostic Link Fail","Radio Fail"]; return h[v] if rng(0,3) else "?"
    if eid in (3,4,9,10):   return f"{v} V"
    if eid in (5,6,7,8):    return f"{v} \u00b0C"
    if eid in (11,12):      return f"{v} (Tx PA current)"
    if eid in (13,14,15,16): return f"{v*0.1:.1f} W"
    if eid == 17:           return f"{v} ms"
    if eid == 18:
        g = ["No Active GPS","GPS-1","GPS-2","Both GPS"]; return g[v] if rng(0,3) else "?"
    if eid in (19,20):
        g = ["No Data","V","A"]; return g[v] if rng(0,2) else "?"
    if eid in (21,22):      return f"{v} s"
    if eid in (27,28):
        g = ["link+PPS fail","link fail / PPS ok","link ok / PPS fail","link ok / PPS ok"]
        return g[v] if rng(0,3) else "?"
    if eid in (29,30):      return f"RSSI {v}"
    if eid == 31:           return "Default key set" if v == 0 else f"KMS key set {v}"
    if eid == 32:           return "No keys" if v == 0 else f"{v} key sets left"
    if eid == 33:           return "0x%04x" % (v & 0xffffffff)
    if eid == 57:           return "0x%08x" % (v & 0xffffffff)
    if eid in (34,35,36,37): return "OK" if v == 1 else ("NOT OK" if v == 0 else "?")
    if eid == 41:           return "Yes" if v else "No"
    if eid == 42:           return "Brake Test failed" if v == 0 else "MR not available"
    if eid == 43:           return nmsFormation(v)
    if eid == 44:
        c = ["No Cab","Cab1","Cab2","Both Cabs"]; return c[v] if rng(0,3) else "?"
    if eid == 45:
        br = ["Not used","Reverse movement","Unusual stoppage","Overspeed","Rollback",
              "MBT selected","No LP Acknowledge","MA Shortened","Head-on collision",
              "Rear-end collision","Loco Specific SoS","Station General SoS"]
        return br[v] if rng(0,11) else "?"
    if eid == 48:
        lo = (v >> 8) & 0xFFFFFF; code = v & 0xFF; return f"loco {lo}, code {code}"
    if eid == 49:
        s = ["?","Manual SoS","Manual SoS end","Unusual stop start","Unusual stop end"]
        return s[v] if rng(1,4) else "?"
    if eid in (50,51):      return "Isolated" if v == 1 else ("Connected" if v == 2 else "?")
    if eid == 52:           return "EB Connected" if v == 1 else ("EB Bypassed" if v == 2 else "?")
    if eid == 53:
        t = ["?","KAVACH Entry","KAVACH Exit","ETCS Entry","ETCS Exit"]
        return t[v] if rng(1,4) else "?"
    if eid == 55:
        mid = (v >> 4) & 0xFFF; mh = v & 0xF
        ok = "OK" if mh == 1 else ("NOT OK" if mh == 0 else "?")
        return f"module {mid} ({nmsModuleName(mid)}) = {ok}"
    return ""

def nmsFaultInputName(i):
    d = {
        1: 'Radio-1',
        2: 'Radio-2',
        3: 'Radio1 Power',
        4: 'Radio2 Power',
        5: 'GPS-1',
        6: 'GPS-2',
        7: 'GPS-1 view',
        8: 'GPS-2 view',
        9: 'GSM-1',
        10: 'GSM-2',
        11: 'CAN 1 status',
        12: 'CAN 2 status',
        13: 'CAN 3 Status',
        14: 'I2C_1',
        15: 'I2C_2',
        16: 'I2C_3',
        17: 'RS485_1',
        18: 'FSI_1',
        19: 'FSI_2',
        20: 'RTC',
        21: 'RTC_2',
        22: '24V_1 PS',
        23: '24V_2 PS',
        24: '3.3V PS',
        25: '1.2V PS',
        26: 'safety error',
        27: 'Link_1',
        28: 'Link_2',
        29: 'RS485_2',
        33: 'CPU Fail',
        100: 'CARD STATUS',
        1001: 'DMI 1',
        1002: 'DMI 2',
        1003: 'RFID READER 1',
        1004: 'RFID READER 2',
        1005: 'Active DMI',
        1006: 'Non-Active Cab DMI',
        1007: 'Speed Sensor1 comm',
        1008: 'Speed Sensor2 comm',
        1009: 'BIU',
        1010: 'MBT',
        1011: 'cpc1',
        1012: 'cpc2',
        1013: 'dmi1_sos_button',
        1014: 'dmi1_cancel_button',
        1015: 'dmi1_common_button',
        1016: 'dmi1_rotary_button',
        1017: 'dmi2_sos_button',
        1018: 'dmi2_cancel_button',
        1019: 'dmi2_common_button',
        1020: 'dmi2_rotary_button',
        1021: 'pvef',
        1022: 'horn1_solenoid',
        1023: 'horn2_solenoid',
        1024: 'horn1_cock',
        1025: 'horn2_cock',
        1026: 'traction',
        1027: 'kavach_isolation',
        1028: 'biu_isolation',
        1029: 'sifa_valve',
        1030: 'sifa_isolation',
        1031: 'fsb_relay',
        1032: 'nb_relay',
        1033: 'eb_relay',
        1034: 'le_relay',
        1035: 'sifa_relay',
    }
    return d.get(i, "?")
