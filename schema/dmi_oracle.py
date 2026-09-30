"""dmi_oracle.py -- faithful Python port of decodeDmiReg() (capturedecoder.cpp).

Ground-truth reference for the schema-driven @dmi decode. Mirrors the hand
decoder field-for-field, including the LC gate block, packed datetime, the two
single-bit flag vectors (alarm_code / context_values), two mixed bf()/uN()
inline runs, the little-endian tail block, and the JAMCRC trailer.

The ONE deliberate change vs the historic hand decoder: the CRC row is rendered
in the canonical schema form  "0X........  PASS|FAIL"  (the old DMI-only
"(calc 0x........)" debug suffix is dropped, matching every other migrated
type). The C++ decodeDmiReg CRC row is updated to match.

DMI regime: 115-byte frame, AA AA .. BB BB. Body bytes 10..108 are LSB-first
bitfields with byte re-alignment between blocks; uN() reads are byte-aligned LE.
"""
from crc_algos import jamcrc

# ----------------------------- cursor (== DmiCursor) -----------------------
class Cur:
    def __init__(s, b): s.b = b; s.n = len(b); s.pos = 0
    def align(s):
        if s.pos & 7: s.pos += 8 - (s.pos & 7)
    def bf(s, w):                         # LSB-first bitfield
        v = 0
        for i in range(w):
            p = s.pos + i
            if (p >> 3) < s.n:
                v |= ((s.b[p >> 3] >> (p & 7)) & 1) << i
        s.pos += w
        return v
    def uN(s, nb):                        # byte-aligned little-endian uintN
        s.align(); bo = s.pos >> 3; v = 0
        for i in range(nb):
            if bo + i < s.n: v |= s.b[bo + i] << (8 * i)
        s.pos += 8 * nb
        return v

# ----------------------------- formatters ----------------------------------
def _idx(v, m):  return f"{v} ({m[v]})" if 0 <= v < len(m) else str(v)

def mapLocoMode(v):
    m = ["?","Stand_By","Staff_Responsible","Limited_Supervision","Full_Supervision",
         "Override","On_Sight","Trip","Post_Trip","Reverse","Shunting","Non_Leading",
         "System_Failure","Isolation"]
    return _idx(v, m)

def dmiBrakeType(v):
    m = ["NO_OVER_SPEED_NO_BRAKES","OVER_SPEED_NO_BRAKES","NORMAL_BRAKE",
         "FULL_SERVICE_BRAKE","EMERGENCY_BRAKE","LIGHT_ENGINE_BRAKE"]
    return f"{v} ({m[v]})" if v < 6 else str(v)

def dmiTrainType(v):
    m = ["LIGHT_ENGINE","GOODS_TRAIN","PASSENGER_ICF","PASSENGER_LHB","EMU","TRAIN_SET","PARCEL"]
    if 1 <= v <= 7: return f"{v} ({m[v-1]})"
    return "0 (none)" if v == 0 else str(v)

def dmiTargetDistType(v):
    m = ["EOA","TURNOUT","TSR","PSR","COLLISION","SOS_TARGET","UNDEFINED"]
    if 1 <= v <= 7: return f"{v} ({m[v-1]})"
    return "0 (none)" if v == 0 else str(v)

def dmiMoveDir(v):
    m = ["undefined","nominal","reverse"]
    return f"{v} ({m[v]})" if v < 3 else str(v)

def _sigDir(v):
    d = {0:"UP",1:"DN",2:"UP FAST",3:"DN FAST",8:"UP SLOW",9:"DN SLOW",10:"UP MAIN",
         11:"DN MAIN",12:"UP SUB",13:"DN SUB",14:"UP BI-DIR",15:"DN BI-DIR"}
    return d.get(v, "undefined")

def _sigKind(v):
    d = {0x10:"Dist",0x11:"Inr-Dist",0x12:"Gate-Dist",0x13:"Gate-Inr-Dist",0x14:"IB-Dist",
         0x15:"IB-Inr-Dist",0x16:"Auto",0x17:"Semi-Auto Lit",0x18:"Home",0x19:"Home L-X",
         0x1A:"R-Home",0x1B:"R-Home L-X",0x1C:"M/L-Str L-X",0x1D:"L/L-Str L-X",0x1E:"Int-Str",
         0x01:"Adv-Str",0x02:"IB-Stop",0x03:"Gate-Stop",0x04:"Calling-On",0x05:"Adv-Str-cum-Gate",
         0x06:"Gate-cum-Dist",0x07:"Adv-Str-cum-Dist",0x23:"Auto-Gate",0x24:"Semi-Auto",
         0x25:"Adv-Str-cum-G-ID",0x26:"Gate-cum-ID",0x27:"Gate-ID-cum-Dist",0x28:"IB-cum-Gate-Dist",
         0x29:"IB-cum-Gate-ID",0x2A:"IB-cum-Dist",0x2B:"Adv-Str-cum-IB-D",0x2C:"Str-cum-IB-Dist",
         0x2D:"Stop Board",0x2E:"Gate-cum-IB-Dist",0x2F:"Gate-cum-IB-ID",0x30:"Adv-Str-cum-G-D"}
    return d.get(v, "undefined")

def sigInfo(v):
    line = v & 0x1F; dr = (v >> 5) & 0xF; kind = (v >> 9) & 0x3F
    ovr = (v >> 15) & 1; stop = (v >> 16) & 1
    return (f"0x{v:x}  [type={_sigKind(kind)}  dir={_sigDir(dr)}  "
            f"line={'n/a' if line == 0 else line}  "
            f"override={'running' if ovr else 'standstill'}  stop_sig={'Y' if stop else 'N'}]")

def sigAspect(v):
    t = {0:"Unidentified",1:"Red",2:"Yellow, no route indication",
         3:"Yellow + Pos1 junction route, left",4:"Yellow + Pos2 junction route, left",
         5:"Yellow + Pos3 junction route, left",6:"Yellow + Pos4 junction route, right",
         7:"Yellow + Pos5 junction route, right",8:"Yellow + Pos6 junction route, right",
         10:"Double Yellow",11:"Green",12:"Double Yellow + Pos1 junction route, left",
         13:"Double Yellow + Pos4 junction route, right",14:"AG Marker OFF",
         15:"Red with Calling-on at OFF",24:"Stop Board / Buffer Stop"}
    if v in t: return f"{v} ({t[v]})"
    if 32 <= v <= 63: return f"{v} (Yellow + Stencil route {v-31})"
    return f"{v} (Spare)"

ALARM = [
 "System Fault, Isolate or Restart KAVACH","Ack Block Stop, SOS Generates in XX s","EB Bypassed (EB Cock Closed), No Traction",
 "Train Tripped, Select P_Trip","Brake Applied, Dead End Detected","Standstill Protection, Brake Applied",
 "Rollback Protection, Brake Applied","REV Movement Not Allowed, Use REV mode","Stand By mode - CAB input not Active",
 "Ack SR mode, KAVACH Territory","Ack SR mode, Station Radio Comm Fail","Ack SR mode, No Track Profile Info",
 "Ack SR mode, Tags Missing","Ack SR mode, Direction Unknown","Ack SR mode, GPS Fail","Ack LS mode, Station Radio Comm Fail",
 "Ack SR mode, TSR Link Fail","Head-On Collision with Loco","Rear-End Collision with Loco","Override Selected, Pass Signal",
 "Reverse Mode Expires","Manned LC Gate","Unmanned LC Gate","LS mode - Waiting for Station Radio Comm",
 "LS mode - Waiting for Track Profile Info","Leading/Non-Leading Input Active","Train Length Computation In Progress",
 "Train Length Computation Success","Train Length Computation Fail","Train Length Computation Aborted",
 "Turnout with Speed Limit","TSR with Speed Limit","PSR with Speed","End of Authority","KAVACH Territory Entry",
 "System Self Test In Progress","System Self Test Success","System Self Test Fail","Brake Test - Waiting for MR",
 "Brake Test - Waiting for BP","Brake Test - NSB Applied (BP)","Brake Test - FSB Applied (BP)","Brake Test - EB Applied (BP)",
 "Brake Test - LEB Applied (BC)","Brakes Testing Success","Brakes Testing Fail","Brakes Test Fail, Press Ack for Retesting",
 "Select Train Config, Press Config Button","Select Staff Responsible / Shunt mode","Approaching Radio Hole","Ack OS mode",
 "Train in Fouling Zone, Normalize the REV","SR/SH mode - ETCS Territory Entry","FS mode - ETCS Full Supervision mode",
 "Ack SR mode - ETCS Territory Exit","Ballise Default Telegram Received","Waiting for Traction Command",
 "Traction Cut-off Command Fail","Fouling Mark Entry","Fouling Mark Clear","Neutral Section Approaching",
 "Braking System Malfunction","No Forward Dir in REV mode","Ack SR mode - SR Authorized Received",
 "Ack SR mode - Slip/Skid Detected","Ack SR mode - Foreign Tag Detected","Ack SR mode - Odo Error Detected",
 "Brake Applied - Shunting Limit Exceed","Brake Applied - Station General SOS","Brake Applied - SPAD Detected","no_alarm"]

CTX = [
 "SOS - Self Loco Manual","SOS - Self Loco Stopped in Block Section","SOS - Self Loco Train Parted",
 "SOS - Other Loco Manual","SOS - Other Loco Stopped in Block Section","SOS - Other Loco Train Parted",
 "SOS - Station (All Locos)","SOS - Station (This Loco)","Over Speed - Reduce Speed",
 "Brake Applied - Speed Limit Exceeded","FSB will be Applied in XX s","EB will be Applied in XX s",
 "BIU Isolated","Train Type Selected"]

# ----------------------------- decode --------------------------------------
def decode_dmi(w):
    r = []
    if len(w) < 115 or w[0] != 0xAA or w[1] != 0xAA:
        return [("dmi", "(bad frame \u2014 expected 115 B AA AA .. BB BB)")]
    r.append(("msg.src_id",         str(w[3])))
    r.append(("msg.dest_id",        str(w[4])))
    r.append(("msg.message_id",     str(w[5])))
    r.append(("msg.message_length", str(w[6] | (w[7] << 8))))
    r.append(("msg.seq_num",        str(w[8] | (w[9] << 8))))

    c = Cur(bytes(w[10:109]))

    # LC_GATE_DISP_INFO
    lcWhist = c.uN(1); lcWtype = c.uN(1); lcDist = c.uN(4)
    lcAlpha = c.uN(1); lcNum = c.uN(2); lcMan = c.uN(1)
    r.append(("lc.auto_whistle",      str(lcWhist)))
    r.append(("lc.auto_whistle_type", str(lcWtype)))
    r.append(("lc.distance",          f"{lcDist} m"))
    r.append(("lc.id",                f"{lcNum} suf={lcAlpha}"))
    r.append(("lc.manning",           "Unmanned" if lcMan else "Manned"))

    # DATE_TIME
    dd = c.uN(1); mo = c.uN(1); yy = c.uN(2); hh = c.uN(1); mi = c.uN(1); ss = c.uN(1)
    r.append(("rtc", f"{dd:02d}-{mo:02d}-{yy} {hh:02d}:{mi:02d}:{ss:02d}"))

    # ALARM_CODE (71 flags + 9 pad)
    c.align()
    al = [ALARM[i] for i in range(71) if c.bf(1)]
    c.bf(9)
    r.append(("alarm_code", "; ".join(al) if al else "(none)"))

    # RFID_RECORD
    c.align()
    cur = c.bf(10); curS = c.bf(2); last = c.bf(10); lastS = c.bf(2)
    ll = c.bf(10); llS = c.bf(2); c.bf(4)
    r.append(("rfid.current",      f"{cur} (st {curS})"))
    r.append(("rfid.last",         f"{last} (st {lastS})"))
    r.append(("rfid.last_to_last", f"{ll} (st {llS})"))

    # inline run 1
    train_id = c.bf(20); warn = c.bf(8); mode = c.bf(4); absloc = c.bf(23); tspeed = c.bf(9)
    siginfo = c.bf(17); tdist = c.bf(15); stnloc = c.bf(23); splperm = c.bf(9); tsrdist = c.bf(15)
    mawrt = c.bf(16); btm = c.bf(1); apprsig = c.bf(15); distnext = c.bf(15); selftest = c.bf(2)
    stnid = c.bf(16); tsrlen = c.bf(15); tsrcls = c.bf(1); tlen = c.bf(11); tospeed = c.bf(5)
    splloop = c.bf(9)
    r.append(("train_id",                str(train_id)))
    r.append(("warning_type",            f"{warn} (unmapped)"))
    r.append(("loco_mode",               mapLocoMode(mode)))
    r.append(("abs_loco_loc",            f"{absloc} m ({absloc/1000.0:.3f} km)"))
    r.append(("train_speed",             f"{tspeed} km/h"))
    r.append(("current_sig_info",        sigInfo(siginfo)))
    r.append(("target_distance",         f"{tdist} m"))
    r.append(("stn_ilc_ibs_loc",         f"{stnloc} m"))
    r.append(("speed_limit_permissible", f"{splperm} km/h"))
    r.append(("tsr_distance",            f"{tsrdist} m"))
    r.append(("ma_w_r_t_sig",            f"{mawrt} m"))
    r.append(("btm_status",              str(btm)))
    r.append(("appr_sig_dist",           f"{apprsig} m"))
    r.append(("dist_next_rfid",          f"{distnext} m"))
    r.append(("LKAVACH_self_test",       str(selftest)))
    r.append(("station_id",              str(stnid)))
    r.append(("tsr_length",              f"{tsrlen} m"))
    r.append(("tsr_class",               str(tsrcls)))
    r.append(("train_length",            f"{tlen} m"))
    r.append(("to_speed",                f"{tospeed} km/h"))
    r.append(("speed_limit_loopline",    f"{splloop} km/h"))

    # DMI_CONTEXT_VALUES (14 flags + 2 pad)
    c.align()
    cx = [CTX[i] for i in range(14) if c.bf(1)]
    c.bf(2)
    r.append(("context_values", "; ".join(cx) if cx else "(none)"))

    # inline run 2
    acab = c.bf(2); secsp = c.bf(9); brk = c.bf(3); tdt = c.bf(4); sigstr = c.bf(6)
    radioh = c.bf(2); lkh = c.bf(1); diropr = c.bf(1); txtmsg = c.bf(1); mvdir = c.bf(2)
    k2nk = c.bf(1); spdcol = c.bf(1); pgh = c.bf(1); decel = c.bf(8); biuh = c.bf(1)
    gps = c.bf(1); aspect = c.bf(6); tsruniv = c.bf(6); sos = c.bf(3); collid = c.bf(20)
    r.append(("active_cab",            str(acab)))
    r.append(("section_speed_info",    f"{secsp} km/h"))
    r.append(("brake_type",            dmiBrakeType(brk)))
    r.append(("target_distance_type",  dmiTargetDistType(tdt)))
    r.append(("signal_strength",       str(sigstr)))
    r.append(("radio_health_status",   f"{radioh} (unmapped)"))
    r.append(("LKAVACH_health_status", "OK" if lkh else "fault"))
    r.append(("dir_of_opr_loco",       str(diropr)))
    r.append(("txt_msg_type",          str(txtmsg)))
    r.append(("movement_dir",          dmiMoveDir(mvdir)))
    r.append(("kavach_to_non_kavach",  str(k2nk)))
    r.append(("train_speed_color_sts", str(spdcol)))
    r.append(("pg_health_status",      "OK" if pgh else "fault"))
    r.append(("deceleration_constant", f"DC {decel//100}.{decel%100:02d}"))
    r.append(("biu_health_status",     "OK" if biuh else "fault"))
    r.append(("gps_status",            str(gps)))
    r.append(("current_sig_aspect",    sigAspect(aspect)))
    r.append(("tsr_universal_speed",   f"{tsruniv} (unmapped)"))
    r.append(("gen_sos_call",          f"{sos} (unmapped)"))
    r.append(("collision_loco_id",     str(collid)))

    # little-endian block
    pres = c.uN(2); tgts = c.uN(2); colld = c.uN(4); ovto = c.uN(1)
    exprm = c.uN(2); exprd = c.uN(2); tcsd = c.uN(4)
    fsbt = c.uN(1); ebt = c.uN(1); ackt = c.uN(1)
    r.append(("context_pressure",                str(pres)))
    r.append(("target_speed",                    f"{tgts} km/h"))
    r.append(("collision_loco_distance",         f"{colld} m"))
    r.append(("override_timeout",                f"{ovto} s"))
    r.append(("expiry_reverse_mode",             f"{exprm} s"))
    r.append(("expiry_reverse_distance",         f"{exprd} m"))
    r.append(("track_condition_start_distance",  f"{tcsd} m"))
    r.append(("fsb_applied_time",                f"{fsbt} s"))
    r.append(("eb_applied_time",                 f"{ebt} s"))
    r.append(("ack_blockstop_sos_generate_time", f"{ackt} s"))

    # tail
    c.align()
    sosstn = c.bf(16); ttype = c.bf(3); c.bf(5)
    r.append(("sos_station_id", str(sosstn)))
    r.append(("train_type",     dmiTrainType(ttype)))

    # num coaches: one byte after the tail, added to the emitter after the
    # original oracle was written. Its presence is proven by the CRC: with it,
    # JAMCRC over wire[3 : 3+107] matches the word stored at wire[110:114] on
    # every captured frame; without it the CRC is read one byte early and fails.
    c.align()
    r.append(("num coaches",    str(c.bf(8))))

    # CRC (canonical display): JAMCRC over wire[3 : 3+107], stored LE at wire[110:114]
    calc = jamcrc(w, 3, 107)
    stored = w[110] | (w[111] << 8) | (w[112] << 16) | (w[113] << 24)
    r.append(("CRC", f"0X{stored:08X}  {'PASS' if calc == stored else 'FAIL'}"))
    return r
