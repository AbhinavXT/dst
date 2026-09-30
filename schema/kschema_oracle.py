"""
kschema_oracle.py -- faithful Python port of kavachschema.h (namespace kschema).

This mirrors the *validated* C++ SLRP decoder (tables + walker + decodeSlrp)
line-for-line so it can serve as the ground-truth ORACLE the XML engine is
diffed against. If kavach.xml (via engine.py) reproduces this row-for-row on
real captures, the C++ schemadecoder (mirror of engine.py) will reproduce
kschema in the app.

Keep this aligned to kavachschema.h. Do not "improve" it -- it must match the
C++ output exactly, quirks included.
"""

# ---------- bit cursor (MSB-first, zero-fill past limit) -- BitCursor -------
class BitCursor:
    def __init__(self, b, start, limit):
        self.b = b; self.pos = start; self.limit = limit
    def take(self, n):
        v = 0
        for _ in range(n):
            if self.pos > self.limit - 1:
                self.pos += 1; continue
            bit = (self.b[self.pos >> 3] >> (7 - (self.pos & 7))) & 1
            v = (v << 1) | bit; self.pos += 1
        return v
    def takeS(self, n):
        v = self.take(n)
        if n < 32 and (v & (1 << (n - 1))):
            return v - (1 << n)
        return v

def hexRange(b, frm, ln):
    return ' '.join('%02X' % x for x in b[frm:frm+ln])

# ---------- value formatters (verbatim from capturedecoder.cpp) -------------
def mapLocoMode(v):
    m = ["?","Stand_By","Staff_Responsible","Limited_Supervision","Full_Supervision",
         "Override","On_Sight","Trip","Post_Trip","Reverse","Shunting","Non_Leading",
         "System_Failure","Isolation"]
    return f"{v} ({m[v]})" if v < len(m) else str(v)
def mapAuthType(v):
    m = ["Not used","OS Authority","FS Authority","SR Authority"]
    return f"{v} ({m[v & 3]})"
def mapPktDir(v):
    m = ["Unidentified","Nominal","Reverse","Spare"]
    return f"{v} ({m[v & 3]})"
def mapSpeed6(v):
    if v == 0:  return "0 (dead stop)"
    if v <= 50: return f"{v*5} km/h"
    if v <= 61: return f"{v} (reserved)"
    if v == 62: return "8 km/h (night override)"
    return "unknown"
def secType(v):
    m = ["Station Section","Absolute Block","Autoblock","Reserved"]; return f"{v} ({m[v & 3]})"
def sosType(v):
    m = ["No SoS","Foreign RFID","Reserved","Odo err>=120m","SPAD","Rear-end","Head-on",
         "Shunt violation","Station General SoS"]
    return f"{v} ({m[v]})" if v < 9 else str(v)
def tcType(v):
    m = ["Not used","Dead Stop","Radio hole","Non-stopping","Tunnel stopping",
         "Powerless/Neutral","Sound horn","Reversing","Fouling Mark","KAVACH Exit"]
    return f"{v} ({m[v]})" if v < 10 else str(v)
def tsrStatus(v):
    m = ["No applicable TSR","No latest TSR (->SR)","Latest TSR","Reserved"]; return f"{v} ({m[v & 3]})"
def lcClass(v):
    m = ["Spl","A","B1","B2","B","C","D","Spare"]; return m[v & 7]
def lcSuffix(v):
    m = ["none","a","b","c","d","e","OOR","spare"]; return m[v & 7]
def adjLocoDir(v):
    m = ["Not Known","Nominal","Reverse","Deduce"]; return m[v & 3]
def gradDir(v):  return "uphill" if v else "downhill"
def dupDir(v):   return "Reverse(-)" if v else "Nominal(+)"
def manned(v):   return "Unmanned" if v else "Manned"
def toSpeed(v):
    if v == 0:  return "NotUsed"
    if v == 31: return "Unrestricted"
    if v <= 18: return f"{v*5} km/h"
    return f"rsvd({v})"
def sigDir(v):
    d = {0:"UP",1:"DN",2:"UP FAST",3:"DN FAST",8:"UP SLOW",9:"DN SLOW",10:"UP MAIN",
         11:"DN MAIN",12:"UP SUB",13:"DN SUB",14:"UP BI-DIR",15:"DN BI-DIR"}
    return d.get(v, "undefined")
def sigKind(v):
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
    return (f"0x{v:x}  [type={sigKind(kind)}  dir={sigDir(dr)}  "
            f"line={'n/a' if line == 0 else line}  "
            f"override={'running' if ovr else 'standstill'}  stop_sig={'Y' if stop else 'N'}]")
def sigAspect(v):
    t = {0:"0 (Unidentified)",1:"1 (Red)",2:"2 (Yellow, no route indication)",
         3:"3 (Yellow + Pos1 junction route, left)",4:"4 (Yellow + Pos2 junction route, left)",
         5:"5 (Yellow + Pos3 junction route, left)",6:"6 (Yellow + Pos4 junction route, right)",
         7:"7 (Yellow + Pos5 junction route, right)",8:"8 (Yellow + Pos6 junction route, right)",
         10:"10 (Double Yellow)",11:"11 (Green)",
         12:"12 (Double Yellow + Pos1 junction route, left)",
         13:"13 (Double Yellow + Pos4 junction route, right)",14:"14 (AG Marker OFF)",
         15:"15 (Red with Calling-on at OFF)",24:"24 (Stop Board / Buffer Stop)"}
    if v in t: return t[v]
    if 32 <= v <= 63: return f"{v} (Yellow + Stencil route {v-31})"
    return f"{v} (Spare)"

# ---------- field model (mirrors kschema enums/structs) ---------------------
# K kinds
K_U, K_S, K_Met, K_SMet = 'U', 'S', 'Met', 'SMet'
# Role
R_None, R_SegLen, R_Start, R_Len, R_TagHop = 0, 1, 2, 3, 4
# Cond
C_Eq, C_InRange = 0, 1

# FieldDef tuple: (name,bits,kind,fmt,tmpl,condField,condVal,role,hide,condOp,condMax)
def F(name, bits, kind=K_U, fmt=None, tmpl=None, cf=None, cv=0,
      role=R_None, hide=False, cop=C_Eq, cmax=0):
    return (name, bits, kind, fmt, tmpl, cf, cv, role, hide, cop, cmax)

MISS = -9000000000000

HDR_F = [
    F("PKT_TYPE",4), F("PKT_LENGTH",10), F("FRAME_NUM",17), F("SOURCE_STN_ID",16),
    F("STN_VERSION",3), F("DEST_LOCO_ID",20), F("REF_PROF_ID",4), F("LAST_REF_RFID",10),
    F("DIST_PKT_START",15,K_SMet), F("PKT_DIR",2,K_U,mapPktDir),
    F("_pad",3,K_U,None,None,None,0,R_None,True),
]
MA_F = [
    F("FRAME_OFFSET",4), F("DEST_LOCO_SOS",4,K_U,sosType),
    F("TRAIN_SECTION_TYPE",2,K_U,secType), F("CUR_SIG_INFO",17,K_U,sigInfo),
    F("CUR_SIG_ASPECT",6,K_U,sigAspect), F("NEXT_SIG_ASPECT",6,K_U,sigAspect),
    F("APPR_SIG_DIST",15,K_Met), F("AUTHORITY_TYPE",2,K_U,mapAuthType),
    F("AUTHORIZED_SPEED",6,K_U,mapSpeed6,None,"AUTHORITY_TYPE",1),
    F("MA_W_R_T_SIG",16,K_Met), F("REQ_SHORTEN_MA",1),
    F("NEW_MA",16,K_Met,None,None,"REQ_SHORTEN_MA",1),
    F("TRN_LEN_INFO_STS",1),
    F("TRN_LEN_INFO_TYPE",1,K_U,None,None,"TRN_LEN_INFO_STS",1),
    F("REF_FRAME_NUM_TLM",17,K_U,None,None,"TRN_LEN_INFO_STS",1),
    F("REF_OFFSET_INT_TLM",8,K_U,None,None,"TRN_LEN_INFO_STS",1),
    F("NEXT_STN_COMM",1),
    F("APPR_STN_ILC_IBS_ID",16,K_U,None,None,"NEXT_STN_COMM",1),
]
SSP_CNT   = [F("LM_Speed_Info_CNT",5)]
SSP_ENTRY = [
    F("d",15,K_Met,None,"@%s m",None,0,R_SegLen),
    F("class",1,K_U,None,None,None,0,R_None,True),
    F("u",6,K_U,mapSpeed6,"universal %s","class",0),
    F("a",6,K_U,mapSpeed6,"A:%s","class",1),
    F("b",6,K_U,mapSpeed6,"B:%s","class",1),
    F("c",6,K_U,mapSpeed6,"C:%s","class",1),
]
GRAD_CNT   = [F("LM_Grad_Info_CNT",5)]
GRAD_ENTRY = [
    F("d",15,K_Met,None,"@%s m",None,0,R_SegLen),
    F("dir",1,K_U,gradDir,"%s"), F("value",5,K_U,None,"value=%s"),
]
LC_CNT   = [F("LM_LC_Info_CNT",5)]
LC_ENTRY = [
    F("d",15,K_Met,None,"@%s m",None,0,R_Start),
    F("id",10,K_U,None,"id=%s"), F("suffix",3,K_U,lcSuffix,"suf=%s"),
    F("manned",1,K_U,manned,"%s"), F("class",3,K_U,lcClass,"class=%s"),
    F("awEn",1,K_U,None,"aw=%s"), F("awType",2,K_U,None,"awType=%s"),
]
TO_CNT   = [F("TO_CNT",2)]
TO_ENTRY = [
    F("speed",5,K_U,toSpeed,"speed=%s"),
    F("start",15,K_Met,None,"start=%s m","speed",1,R_Start,False,C_InRange,18),
    F("release",12,K_Met,None,"release=%s m","speed",1,R_Len,False,C_InRange,18),
]
TLI_PRE   = [F("DIST_DUP_TAG",4), F("ROUTE_RFID_CNT",6)]
TLI_ENTRY = [
    F("dist",11,K_Met,None,"dist=%s m",None,0,R_TagHop),
    F("tag",10,K_U,None,"tag=%s"), F("dup_dir",1,K_U,dupDir,"dup_dir=%s"),
]
TLI_POST = [
    F("LOC_RESET",1,K_U,None,None,None,0,R_None,True),
    F("START_DIST_TO_LOC_RESET",15,K_Met,None,None,"LOC_RESET",1),
    F("ADJ_LOCO_DIR",2,K_U,adjLocoDir,None,"LOC_RESET",1),
    F("ABS_LOC_CORRECTION",23,K_Met,None,None,"LOC_RESET",1),
    F("ADJ_LINE_CNT",3),
]
TLI_TIN = [F("tin",9,K_U,None,"%s")]
# Always-present trailing TIN of the line the tag chain runs on. See the
# comment on <field name="TIN"> in kavach.xml: this is the field that made the
# sub-packet look like it ended in a pad byte.
TLI_TAIL = [F("TIN",9)]
TC_CNT   = [F("TRACKCOND_CNT",4)]
TC_ENTRY = [
    F("type",4,K_U,tcType,"%s"),
    F("sd",15,K_Met,None,"@%s m",None,0,R_Start),
    F("ln",15,K_Met,None,"len=%s m",None,0,R_Len),
]
TSR_PRE   = [F("TSR_STATUS",2,K_U,tsrStatus), F("TSR_Info_CNT",5)]
TSR_ENTRY = [
    F("id",8,K_U,None,"id=%s"),
    F("dist",15,K_Met,None,"@%s m",None,0,R_Start),
    F("len",15,K_Met,None,"len=%s m",None,0,R_Len),
    F("class",1,K_U,None,None,None,0,R_None,True),
    F("u",6,K_U,mapSpeed6,"universal %s","class",0),
    F("a",6,K_U,mapSpeed6,"A:%s","class",1),
    F("b",6,K_U,mapSpeed6,"B:%s","class",1),
    F("c",6,K_U,mapSpeed6,"C:%s","class",1),
    F("whistle",2,K_U,None,"whistle=%s"),
]

# SegmentDef: ('flat', fields) or ('rep', fields, countField, label, cmin, cmax)
def Flat(f):                          return ('flat', f)
def Rep(f, cnt, label, cmin=0, cmax=63): return ('rep', f, cnt, label, cmin, cmax)

# StructDef: (label, [segments])
SLRP_HEADER = ("SLRP", [Flat(HDR_F)])
MA_STRUCT   = ("MovementAuthority", [Flat(MA_F)])
SSP_STRUCT  = ("StaticSpeedProfile", [Flat(SSP_CNT), Rep(SSP_ENTRY,"LM_Speed_Info_CNT","speed",0,31)])
GRAD_STRUCT = ("GradientProfile", [Flat(GRAD_CNT), Rep(GRAD_ENTRY,"LM_Grad_Info_CNT","grad",0,31)])
LC_STRUCT   = ("LCgateProfile", [Flat(LC_CNT), Rep(LC_ENTRY,"LM_LC_Info_CNT","lc",0,31)])
TO_STRUCT   = ("TurnoutSpeed", [Flat(TO_CNT), Rep(TO_ENTRY,"TO_CNT","to",0,3)])
TLI_STRUCT  = ("TagLinking", [Flat(TLI_PRE), Rep(TLI_ENTRY,"ROUTE_RFID_CNT","tag",1,62),
                              Flat(TLI_POST), Rep(TLI_TIN,"ADJ_LINE_CNT","line_tin",0,5),
                              Flat(TLI_TAIL)])
TC_STRUCT   = ("TrackCondition", [Flat(TC_CNT), Rep(TC_ENTRY,"TRACKCOND_CNT","tc",0,15)])
TSR_STRUCT  = ("TSR", [Flat(TSR_PRE), Rep(TSR_ENTRY,"TSR_Info_CNT","tsr",0,31)])

SLRP_SUB = [MA_STRUCT, SSP_STRUCT, GRAD_STRUCT, LC_STRUCT,
            TO_STRUCT, TLI_STRUCT, TC_STRUCT, TSR_STRUCT]

# ---------- walker (mirrors readF/fullVal/bareVal/condOk/walkEntry/runStruct)
def readF(c, f):
    kind = f[2]
    return c.takeS(f[1]) if kind in (K_S, K_SMet) else c.take(f[1])
def fullVal(f, v):
    if f[3]: return f[3](v)
    if f[2] in (K_Met, K_SMet): return f"{v} m"
    return str(v)
def bareVal(f, v):
    if f[3]: return f[3](v)
    return str(v)
def condOk(f, scope):
    cf = f[5]
    if cf is None: return True
    cv = scope.get(cf, MISS)
    if f[9] == C_InRange: return f[6] <= cv <= f[10]
    return cv == f[6]

class Geo:
    def __init__(self): self.refKnown=False; self.refAbs=0; self.blockAbs=0; self.travel=1

def walkEntry(c, fields, scope, grp, geo):
    toks = []
    hasSeg=hasStart=hasLen=hasTag=False; segv=startv=lenv=tagv=0
    for f in fields:
        if not condOk(f, scope): continue
        v = readF(c, f); scope[f[0]] = v
        role = f[7]
        if   role == R_SegLen: hasSeg=True;   segv=v
        elif role == R_Start:  hasStart=True; startv=v
        elif role == R_Len:    hasLen=True;   lenv=v
        elif role == R_TagHop: hasTag=True;   tagv=v
        if f[8]: continue  # hide
        bare = bareVal(f, v)
        toks.append((f[4] % bare) if f[4] else f"{f[0]}={bare}")
    suf = ""
    if geo.refKnown:
        if hasSeg:
            a = grp['seg']; b = a + segv; grp['seg'] = b
            suf = f"   [{geo.blockAbs + geo.travel*a} \u2192 {geo.blockAbs + geo.travel*b} m]"
        elif hasStart and hasLen:
            suf = f"   [{geo.blockAbs + geo.travel*startv} \u2192 {geo.blockAbs + geo.travel*(startv+lenv)} m]"
        elif hasStart:
            suf = f"   abs={geo.blockAbs + geo.travel*startv} m"
        elif hasTag:
            grp['tag'] += tagv
            suf = f"   abs={geo.blockAbs + geo.travel*grp['tag']} m"
    return "  ".join(toks) + suf

def runStruct(c, sd, rows, geo, scope):
    for seg in sd[1]:
        if seg[0] == 'flat':
            for f in seg[1]:
                if not condOk(f, scope): continue
                v = readF(c, f); scope[f[0]] = v
                if not f[8]:
                    rows.append(("  " + f[0], fullVal(f, v)))
        else:
            _, fields, cntField, label, cmin, cmax = seg
            cnt = scope.get(cntField, 0)
            if cnt < cmin or cnt > cmax: cnt = 0
            grp = {'seg':0, 'tag':0}
            for i in range(cnt):
                if c.pos >= c.limit: break
                ev = dict(scope)
                line = walkEntry(c, fields, ev, grp, geo)
                rows.append((f"  {label}[{i+1}]", line))

def decodeSlrp(b, tagLoc=None):
    rows = []
    n = len(b)
    c = BitCursor(b, 0, (n - 4) * 8)
    scope = {}
    runStruct(c, SLRP_HEADER, rows, Geo(), scope)

    geo = Geo()
    ref  = scope.get("LAST_REF_RFID", 0)
    dps  = scope.get("DIST_PKT_START", 0)
    pdir = scope.get("PKT_DIR", 0)
    if tagLoc and ref in tagLoc:
        geo.refKnown = True
        geo.refAbs   = tagLoc[ref]
        geo.travel   = -1 if pdir == 2 else 1
        # Start signal = LAST_REF_RFID abs + DIST_PKT_START counted along the
        # direction of travel; every packet distance, the tag list included,
        # is counted from the start signal the same way (confirmed session 66).
        # Until then this read ref - dps (Nominal) and measured tags from ref.
        geo.blockAbs = geo.refAbs + geo.travel * dps

    idx = 1
    while c.pos // 8 < n - 8:
        sb = c.pos // 8
        t = c.take(4); L = c.take(7)
        if L == 0 or sb + L > n - 4: break
        sdp = SLRP_SUB[t] if t < 8 else None
        name = sdp[0] if sdp else f"reserved({t})"
        rows.append((f"subpkt[{idx}] {name} @byte {sb}", f"{L} B"))
        if sdp:
            sc = BitCursor(b, sb*8 + 11, sb*8 + L*8)
            ss = dict(scope)
            runStruct(sc, sdp, rows, geo, ss)
        c.pos = sb*8 + L*8
        idx += 1
    endByte = c.pos // 8
    rem = (n - 4) - endByte
    if rem == 4:   rows.append(("  MAC_CODE", hexRange(b, endByte, 4)))
    elif rem > 0:  rows.append(("  unparsed (raw)", hexRange(b, endByte, rem)))
    return rows
