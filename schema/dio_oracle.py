"""dio_oracle.py -- faithful port of capturedecoder.cpp decodeDigitalIn1/In2/Out.
Ground-truth for validating the XML engine's dip/dop output."""

def dioBit(b, off, pos):
    byte = off + (pos >> 3)
    if byte < 0 or byte >= len(b): return -1
    return (b[byte] >> (pos & 7)) & 1

def _row(b, pos, name):
    v = dioBit(b, 0, pos)
    return (name, "--" if v < 0 else str(v))

DIP1 = ["1 dmi1_sos_nc","2 dmi1_sos_no","3 dmi1_cancel_nc","4 dmi1_cancel_no",
    "5 dmi1_common_nc","6 dmi1_common_no","7 dmi1_leading_non_leading_nc",
    "8 dmi1_leading_non_leading_no","9 dmi2_sos_nc","10 dmi2_sos_no","11 dmi2_cancel_nc",
    "12 dmi2_cancel_no","13 dmi2_common_nc","14 dmi2_common_no",
    "15 dmi2_leading_non_leading_nc","16 dmi2_leading_non_leading_no","17 pvef_disabled_fb",
    "18 neutral_section_fb","19 fouling_mark_entry_fb","20 horn1_fb_cock_no",
    "21 horn1_solenoid_no","22 horn2_fb_cock_no","23 horn2_solenoid_no","24 cab1_active",
    "25 cab1_forward","26 cab1_reverse","27 cab2_active","28 cab2_forward","29 cab2_reverse",
    "30 train_intigrity","31 traction_cutoff_fb"]

DIP2 = [(0,"1 kavach_isolation_fb_no"),(1,"2 kavach_isolation_fb_nc"),(2,"3 biu_isolation_fb_no"),
    (3,"4 biu_isolation_fb_nc"),(4,"5 mr_cock_fb_no"),(5,"6 bp_cock_fb_no"),
    (6,"7 sifa_valve_fb_no"),(7,"8 sifa_valve_fb_nc"),(8,"9 sifa_isolation_cock_fb_no"),
    (9,"10 sifa_isolation_cock_fb_nc"),(20,"21 fsb_relay_fb_no"),(21,"22 fsb_relay_fb_nc"),
    (22,"23 nb_relay_fb_no"),(23,"24 nb_relay_fb_nc"),(24,"25 eb_relay_fb_no"),
    (25,"26 eb_relay_fb_nc"),(26,"27 le_relay_fb_no"),(27,"28 le_relay_fb_nc"),
    (28,"29 sifa_or_veb_relay_fb_nc"),(29,"30 sifa_or_veb_relay_fb_no")]

def decodeDigitalIn1(b):
    if len(b) < 4: return [("dip1", "(short frame, need 4 B)")]
    return [_row(b, i, DIP1[i]) for i in range(31)]

def decodeDigitalIn2(b):
    if len(b) < 4: return [("dip2", "(short frame, need 4 B)")]
    return [_row(b, pos, name) for pos, name in DIP2]

def decodeDigitalOut(b):
    if len(b) < 16: return [("dop", "(short frame, need 16 B)")]
    r = []
    for pin in range(16):
        base = pin * 8
        a  = dioBit(b, 0, base + 0); bb = dioBit(b, 0, base + 1)
        fl = dioBit(b, 0, base + 2); tr = dioBit(b, 0, base + 3); ou = dioBit(b, 0, base + 4)
        r.append((f"pin[{pin}]", f"a={a} b={bb} fault={fl} trans={tr} output={ou}"))
    return r
