"""
aap_aep_oracle.py -- faithful port of capturedecoder.cpp decodeAAP / decodeAep.
Ground-truth oracle for validating the XML engine's aap/aep output.
Rows use BARE field names (as the hand decoders do); the validator strips
leading whitespace so the schema's uniform 2-space indent is ignored.
"""
from engine import MsbCursor  # same msb-first cursor

def decodeAAP(b):
    c = MsbCursor(b, 0, (len(b) - 4) * 8)
    r = []
    r.append(("PKT_TYPE",               str(c.take(4))))
    r.append(("PKT_LENGTH",             str(c.take(7))))
    r.append(("FRAME_NUM",              str(c.take(17))))
    r.append(("SOURCE_STN_ID",          str(c.take(16))))
    r.append(("STN_VERSION",            str(c.take(3))))
    r.append(("STN_ILC_IBS_LOC",        f"{c.take(23)} m"))
    r.append(("DEST_LOCO_ID",           str(c.take(20))))
    r.append(("Allotted_UpLink_Freq",   str(c.take(12))))
    r.append(("Allotted_DownLink_Freq", str(c.take(12))))
    r.append(("Allotted_TDMA_Timeslot", str(c.take(7))))
    r.append(("STN_RND_NUM_RS",         "0x%04x" % c.take(16)))
    r.append(("STN_TDMA",               str(c.take(7))))
    r.append(("MAC_CODE",               "0x%08x" % c.take(32)))
    return r

def decodeAep(b):
    c = MsbCursor(b, 0, (len(b) - 4) * 8)
    r = []
    r.append(("PKT_TYPE",              f"{c.take(4)} (Additional Emergency Packet)"))
    r.append(("PKT_LENGTH",            f"{c.take(7)} B"))
    r.append(("FRAME_NUM",             str(c.take(17))))
    r.append(("SOURCE_STN_ILC_IBS_ID", str(c.take(16))))
    ver = c.take(3)
    r.append(("SOURCE_STN_ILC_IBS_VERSION",
              "0 (not used)" if ver == 0 else
              "1 (Kavach Spec 3.2)" if ver == 1 else
              "2 (Kavach Spec 4.0)" if ver == 2 else
              f"{ver} (Kavach version)"))
    r.append(("STN_ILC_IBS_LOC", f"{c.take(23)} m"))
    sos = c.take(1)
    r.append(("GEN_SOS_CALL",
              "1 (General SoS \u2014 manual SOIP buttons)" if sos
              else "0 (No Station Manual SoS)"))
    r.append(("Padding", str(c.take(1))))
    return r
