"""ccdl_oracle.py -- faithful port of capturedecoder.cpp decodeCcSys / decodeDlSys."""
from nms_meaning import nmsHealthMeaning
from crc_algos import jamcrc

def leU(b, off, n):
    v = 0
    for i in range(n): v |= b[off+i] << (8*i)
    return v
def leS16(b, off):
    v = leU(b, off, 2)
    return v - 0x10000 if v & 0x8000 else v

def ccActiveRadio(v):
    m = ["No Active Radio","Radio-1","Radio-2","Both Radios"]
    return f"{v} ({m[v]})" if v < 4 else str(v)
def lcuCtrlHlth(x):
    return (f"v24_pgd1={x&1} v24_pgd2={(x>>1)&1} v33={(x>>2)&1} "
            f"v12={(x>>3)&1} safety_err={(x>>4)&1}")
def lcuElemStatus(x):
    return (f"can0={x&1} can1={(x>>1)&1} radio1={(x>>2)&1} "
            f"radio2={(x>>3)&1} gps1={(x>>4)&1} gps2={(x>>5)&1}")
def crcRow(b, frm, ln, at):
    stored = leU(b, at, 4); calc = jamcrc(b, frm, ln)
    return ("0x%08x" % stored).upper() + ("  PASS" if calc == stored else "  FAIL")

def decodeCcSys(b):
    if len(b) < 52: return [("ccsys", f"(short frame, need 52 B, got {len(b)})")]
    e = nmsHealthMeaning
    r = [
        ("active_radio",        ccActiveRadio(b[0])),
        ("health_radio1",       e(1,  b[1])),  ("health_radio2",       e(2,  b[2])),
        ("radio1_input_supply", e(3,  b[3])),  ("radio2_input_supply", e(4,  b[4])),
        ("radio1_temp",         e(5,  leS16(b,5))),  ("radio2_temp",   e(6,  leS16(b,7))),
        ("radio1_pa_temp",      e(7,  leS16(b,9))),  ("radio2_pa_temp",e(8,  leS16(b,11))),
        ("radio1_pa_supply",    e(9,  b[13])), ("radio2_pa_supply",    e(10, b[14])),
        ("radio1_tx_pa_current",e(11, b[15])), ("radio2_tx_pa_current",e(12, b[16])),
        ("radio1_reverse_power",e(13, leU(b,17,2))), ("radio2_reverse_power",e(14, leU(b,19,2))),
        ("radio1_forward_power",e(15, leU(b,21,2))), ("radio2_forward_power",e(16, leU(b,23,2))),
        ("radio1_rx_pkt_count", str(leU(b,25,2))), ("radio2_rx_pkt_count", str(leU(b,27,2))),
        ("active_gps",          e(18, b[29])),
        ("gps1_view",           e(19, b[30])), ("gps2_view",           e(20, b[31])),
        ("gps1_seconds",        e(21, b[32])), ("gps2_seconds",        e(22, b[33])),
        ("gps1_sat_in_view",    str(b[34])),   ("gps2_sat_in_view",    str(b[35])),
        ("gps1_cno_max",        str(b[36])),   ("gps2_cno_max",        str(b[37])),
        ("gps1_link_status",    e(27, b[38])), ("gps2_link_status",    e(28, b[39])),
        ("cc_crc",              crcRow(b, 0, 40, 40)),
        ("lcu_ctrl1_hlth",      lcuCtrlHlth(b[44])), ("lcu_ctrl2_hlth", lcuCtrlHlth(b[45])),
        ("lcu_elem_status1",    lcuElemStatus(b[46])), ("lcu_elem_status2", lcuElemStatus(b[47])),
        ("pkt_crc",             crcRow(b, 0, 48, 48)),
    ]
    return r

def decodeDlSys(b):
    if len(b) < 16: return [("dlsys", f"(short frame, need 16 B, got {len(b)})")]
    return [
        ("gsm1_rssi", str(b[0])), ("gsm2_rssi", str(b[1])), ("rem_key_num", str(b[2])),
        ("gsm1_health", str(b[3])), ("gsm2_health", str(b[4])), ("eval_version", str(b[5])),
        ("eval_checksum", nmsHealthMeaning(57, leU(b,6,4))),
        ("a_v24_pgd1_status", str(b[10])), ("a_v24_pgd2_status", str(b[11])),
        ("crc", crcRow(b, 0, 12, 12)),
    ]
