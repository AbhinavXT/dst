"""rfid_oracle.py -- faithful port of capturedecoder.cpp decodeRfid() plus the
describe() CapType::Rfid row block. Ground-truth for validating the XML engine's
@rfid output, golden-vector style (byte-exact, against real .cap frames).

One intentional difference from the *current* C++ describe() block, applied here
and (separately) in the C++ describe() block as part of this migration:
  - type 12 rows now follow WIRE order: abs_loc_1, tin, abs_loc_2, ...
    (was: abs_loc_1, abs_loc_2, tin, ...). abs_loc_2 is wire-later than tin, and
    the schema engine walks bits in wire order with one monotonic cursor, so the
    canonical order is wire order. This is the only behavioral change.

Anx-D regime: LSB-first within two 64-bit LE words x (tag bytes 0-7) and
y (bytes 8-15); the reader-id is frame byte 0 and the 16-byte tag is frame[1:17].
"""

# ---- bit helpers (mirror rfidLeWord / rfidBf) ----------------------------
def _leword(b, off):
    v = 0
    for i in range(8):
        v |= b[off + i] << (8 * i)
    return v

def _bf(w, lo, hi):
    n = hi - lo + 1
    return (w >> lo) if n >= 64 else ((w >> lo) & ((1 << n) - 1))

# ---- CRC-30 (exact firmware port, == rfidCrc30) --------------------------
def rfid_crc30_bytes(d):
    MASK, POLY, TOP = 0x3FFFFFFF, 0x2030B9C7, 1 << 29
    crc = 0x3FFFFFFF
    for byte in d:
        crc = (crc ^ (byte << 22)) & MASK
        for _ in range(8):
            crc = (((crc << 1) ^ POLY) & MASK) if (crc & TOP) else ((crc << 1) & MASK)
    return (crc ^ 0x3FFFFFFF) & MASK

# ---- coded-value maps (mirror the rfid* helpers) -------------------------
def _named(v, m):
    return f"{v} ({m[v]})" if 0 <= v < len(m) else str(v)

def rfidTagType(t):
    return {9: "Normal", 10: "LC", 11: "Adjacent-Line",
            12: "Adjustment/Junction"}.get(t, "?")

def rfidSection(v):
    m = ["Station Section", "Absolute Block", "Automatic Section", "Virtual Block"]
    return _named(v & 3, m)

def rfidPlacement(v):
    m = ["In line section", "Signal foot (Nominal)", "Signal foot (Reverse)",
         "Turnout", "Exit (Nominal)", "Exit (Reverse)", "Signal foot (both)",
         "Exit (both)", "Dead Stop (Nominal)", "Dead Stop (Reverse)"]
    return f"{v} ({m[v]})" if v <= 9 else f"{v} (reserved)"

def rfidDup(v):
    return _named(v & 1, ["Main Tag", "Duplicate Tag"])

def rfidComm(v):
    return _named(v & 1, ["Required", "Not required"])

def rfidAbsLoc(v):
    return "N/A" if v == 0x7FFFFF else f"{v} m"

def rfidLcApproach(v):
    return _named(v & 3, ["KAVACH", "Non-KAVACH First", "Non-KAVACH Second", "Spare"])

def rfidApplDir(v):
    return _named(v & 1, ["Nominal", "Reverse"])

def rfidGateAlpha(v):
    m = ["None", "a", "b", "c", "d", "e", "Out of range (xx on DMI)", "Spare"]
    return _named(v & 7, m)

def rfidGateType(v):
    return _named(v & 1, ["Manned", "Unmanned"])

def rfidYesNo(v):
    return _named(v & 1, ["No", "Yes"])

def rfidWhistleType(v):
    return _named(v & 1, ["Distance based", "Time based"])

def rfidDirCorr(v):
    m = ["Reset Dir. unknown (derive from next tags)",
         "Loco Nominal, next tag Nominal",
         "Loco Nominal, next tag Reverse",
         "Loco Reverse, next tag Nominal",
         "Loco Reverse, next tag Reverse"]
    return f"{v} ({m[v]})" if v <= 4 else f"{v} (reserved)"

def rfidLocCorrType(v):
    return _named(v & 1, ["Adjustment (A-tag)", "Reset (J-tag)"])

# ---- decode (mirror decodeRfid) ------------------------------------------
def _decode(frame):
    if len(frame) < 17:
        return None
    t = {"readerId": frame[0]}
    tag = frame[1:17]
    x, y = _leword(tag, 0), _leword(tag, 8)

    d = bytearray(tag[:13]); d[12] &= 0x03
    t["crcStored"] = _bf(y, 34, 63)
    t["crcCalc"] = rfid_crc30_bytes(d)
    t["crcOk"] = (t["crcCalc"] == t["crcStored"])

    t["type"] = _bf(x, 0, 3)
    t["version"] = _bf(x, 4, 5)
    t["unique"] = _bf(x, 6, 15)
    t["absLoc"] = _bf(x, 16, 38)
    t["tinNom"] = _bf(x, 39, 46)
    t["tinRev"] = _bf(x, 47, 54)

    ty = t["type"]
    if ty == 9:
        t["stnNom"] = _bf(x, 55, 63) | (_bf(y, 0, 6) << 9)
        t["stnRev"] = _bf(y, 7, 22)
        t["sectNom"] = _bf(y, 23, 24)
        t["sectRev"] = _bf(y, 25, 26)
        t["place"] = _bf(y, 27, 30)
        t["dup"] = _bf(y, 31, 31)
        t["commNom"] = _bf(y, 32, 32)
        t["commRev"] = _bf(y, 33, 33)
    elif ty == 10:
        t["sectNom"] = _bf(x, 55, 56)
        t["sectRev"] = _bf(x, 57, 58)
        t["place"] = _bf(x, 59, 62)
        t["lcApproach"] = _bf(x, 63, 63) | (_bf(y, 0, 0) << 1)
        t["applDir"] = _bf(y, 1, 1)
        t["gateId"] = _bf(y, 2, 11)
        t["gateAlpha"] = _bf(y, 12, 14)
        t["gateType"] = _bf(y, 15, 15)
        t["distGate"] = _bf(y, 16, 25)
        t["autoWhistle"] = _bf(y, 26, 26)
        t["whistleType"] = _bf(y, 27, 27)
        t["dup"] = _bf(y, 31, 31)
        t["commNom"] = _bf(y, 32, 32)
        t["commRev"] = _bf(y, 33, 33)
    elif ty == 11:
        t["adj"] = [
            _bf(x, 55, 62),
            _bf(x, 63, 63) | (_bf(y, 0, 6) << 1),
            _bf(y, 7, 14),
            _bf(y, 15, 22),
            _bf(y, 23, 30),
        ]
        t["dup"] = _bf(y, 31, 31)
    elif ty == 12:
        t["absLoc2"] = _bf(x, 55, 63) | (_bf(y, 0, 13) << 9)
        t["dirCorr1"] = _bf(y, 14, 16)
        t["dirCorr2"] = _bf(y, 17, 19)
        t["locCorrType"] = _bf(y, 20, 20)
        t["sectNom"] = _bf(y, 23, 24)
        t["sectRev"] = _bf(y, 25, 26)
        t["dup"] = _bf(y, 31, 31)
        t["commNom"] = _bf(y, 32, 32)
        t["commRev"] = _bf(y, 33, 33)
    return t

# ---- describe() rows (mirror the CapType::Rfid block) --------------------
def decodeRfid(frame):
    t = _decode(frame)
    if t is None:
        return [("rfid", "(short frame)")]
    r = [
        ("reader_id", str(t["readerId"])),
        ("type", f"{t['type']} ({rfidTagType(t['type'])})"),
        ("version", str(t["version"])),
        ("unique", str(t["unique"])),
    ]
    ty = t["type"]
    if ty == 9:
        r += [
            ("abs_loc", rfidAbsLoc(t["absLoc"])),
            ("tin", f"{t['tinNom']} / {t['tinRev']}"),
            ("station", f"{t['stnNom']} / {t['stnRev']}"),
            ("section", f"{rfidSection(t['sectNom'])} / {rfidSection(t['sectRev'])}"),
            ("placement", rfidPlacement(t["place"])),
            ("duplication", rfidDup(t["dup"])),
            ("comm", f"{rfidComm(t['commNom'])} / {rfidComm(t['commRev'])}"),
        ]
    elif ty == 10:
        r += [
            ("abs_loc", rfidAbsLoc(t["absLoc"])),
            ("tin", f"{t['tinNom']} / {t['tinRev']}"),
            ("section", f"{rfidSection(t['sectNom'])} / {rfidSection(t['sectRev'])}"),
            ("placement", rfidPlacement(t["place"])),
            ("lc_approach", rfidLcApproach(t["lcApproach"])),
            ("applicable_dir", rfidApplDir(t["applDir"])),
            ("gate_id", str(t["gateId"])),
            ("gate_alpha", rfidGateAlpha(t["gateAlpha"])),
            ("gate_type", rfidGateType(t["gateType"])),
            ("dist_to_gate", f"{t['distGate']} m"),
            ("auto_whistle", rfidYesNo(t["autoWhistle"])),
            ("whistle_type", rfidWhistleType(t["whistleType"])),
            ("duplication", rfidDup(t["dup"])),
            ("comm", f"{rfidComm(t['commNom'])} / {rfidComm(t['commRev'])}"),
        ]
    elif ty == 11:
        a = t["adj"]
        r += [
            ("abs_loc", rfidAbsLoc(t["absLoc"])),
            ("tin", f"{t['tinNom']} / {t['tinRev']}"),
            ("adjacent_tin", f"{a[0]} / {a[1]} / {a[2]} / {a[3]} / {a[4]}"),
            ("duplication", rfidDup(t["dup"])),
        ]
    elif ty == 12:
        # WIRE order (migration change): abs_loc_1, tin, abs_loc_2, ...
        r += [
            ("abs_loc_1", rfidAbsLoc(t["absLoc"])),
            ("tin", f"{t['tinNom']} / {t['tinRev']}"),
            ("abs_loc_2", rfidAbsLoc(t["absLoc2"])),
            ("dir_corr_1", rfidDirCorr(t["dirCorr1"])),
            ("dir_corr_2", rfidDirCorr(t["dirCorr2"])),
            ("loc_corr_type", rfidLocCorrType(t["locCorrType"])),
            ("section", f"{rfidSection(t['sectNom'])} / {rfidSection(t['sectRev'])}"),
            ("duplication", rfidDup(t["dup"])),
            ("comm", f"{rfidComm(t['commNom'])} / {rfidComm(t['commRev'])}"),
        ]
    else:
        r += [
            ("abs_loc", rfidAbsLoc(t["absLoc"])),
            ("tin", f"{t['tinNom']} / {t['tinRev']}"),
            ("body", "(unknown tag type \u2014 header + CRC only)"),
        ]
    r.append(("CRC-30", f"0X{t['crcStored']:08X}  {'PASS' if t['crcOk'] else 'FAIL'}"))
    return r
