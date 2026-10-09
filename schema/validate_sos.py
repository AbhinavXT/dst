#!/usr/bin/env python3
"""
validate_sos.py -- gate for the @sos / @sossrc / @sosev schema (session 184).

The oracle is the byte layout itself (sos_oracle.py, struct.unpack at the
offsets of SOS_handoff/01_SOS_LOGGING_PACKETS.md). Every frame is decoded by
the XML engine and each row compared with the oracle's value, then the
grouping the console relies on is checked:

  size      @sos 65 + 20 per station slot, @sossrc 56, @sosev 27;
            version 2 (README 03, minimal): 37 + 24 per loco + 13 per station
  group     each @sossrc follows its @sos (same snap_id), and their number is
            the popcount of that snapshot's src_in_use_mask
  events    an @sosev names a snapshot already sent (snap_id <= the last one)

Usage:  python3 validate_sos.py [capture.log ...]
Default: every replay/**/*.cap|*.log carrying @sos; the synthetic fixtures
(schema/fixtures/sos_synthetic_loco{1,2}.log, made by tests/sosgen) if none.
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine
import sos_oracle as oracle

HERE = os.path.dirname(os.path.abspath(__file__))
XML = os.path.join(HERE, "kavach.xml")
REPLAY = os.path.join(HERE, "..", "replay")
SYNTHETIC = [os.path.join(HERE, "fixtures", "sos_synthetic_loco1.log"),
             os.path.join(HERE, "fixtures", "sos_synthetic_loco2.log"),
             # session 192: README 03's minimal layout (version 2), same scenario
             os.path.join(HERE, "fixtures", "sos_minimal_loco1.log"),
             os.path.join(HERE, "fixtures", "sos_minimal_loco2.log")]

NUM = re.compile(r"^-?\d+(\.\d+)?")

# engine row name -> (oracle key, scale). scale None: compare the integer;
# a number: the engine shows value/scale (format="dm" 10, "x100" 100).
SNAP = {
    "version": ("version", None), "why": ("why", None), "snap_id": ("snap_id", None),
    "tick_ms": ("tick_ms", None), "own_abs_loc": ("own_abs_loc_dm", 10),
    "own_dir": ("own_dir", None), "own_mode": ("own_mode", None), "own_tin": ("own_tin", None),
    "own_train_length": ("own_train_length_m", None), "own_speed": ("own_speed_x100", 100),
    "own_ma": ("own_ma_m", None), "own_station_id": ("own_station_id", None),
    "own_rfid_tag_id": ("own_rfid_tag_id", None), "rear_tag_found": ("rear_tag_found", None),
    "rear_sect_type": ("rear_sect_type", None), "own_emergency_status": ("own_emergency_status", None),
    "access_req_recvd_stn_id": ("access_req_recvd_stn_id", None), "n_src_slots": ("n_src_slots", None),
    "agg_threat": ("agg_threat", None), "agg_station_won": ("agg_station_won", None),
    "collision_loco_id": ("collision_loco_id", None), "agg_station_id": ("agg_station_id", None),
    "collision_distance": ("collision_distance_dm", 10), "sos_distance": ("sos_distance_dm", 10),
    "sos_station_id": ("sos_station_id", None), "last_dest_loco_sos": ("last_dest_loco_sos", None),
    "n_stn_slots": ("n_stn_slots", None),
}
SNAP_HEX = {"src_in_use_mask": "src_in_use_mask", "src_active_mask": "src_active_mask"}
SNAP_FLAGS = {"lp_flags": ("lp_flags", "sosLp"), "dmi_bits": ("dmi_bits", "sosDmi"),
              "self_flags": ("self_flags", "sosSelf")}
STN = {"stn_in_use": ("in_use", None), "stn_id": ("station_id", None), "stn_age": ("age_ms", None),
       "stn_abs_loc": ("station_abs_loc_m", None), "stn_distance": ("distance_dm", 10),
       "stn_sos_distance": ("sos_distance_dm", 10)}
SRC = {
    "version": ("version", None), "snap_id": ("snap_id", None), "slot_index": ("slot_index", None),
    "source_loco_id": ("source_loco_id", None), "age": ("age_ms", None),
    "last_frame_no": ("last_frame_no", None), "last_emergency_sts": ("last_emergency_sts", None),
    "other_mode": ("other_mode", None), "other_speed": ("other_speed", None),
    "raw_abs_loc": ("raw_abs_loc_m", None), "raw_dir": ("raw_dir", None),
    "last_rfid_id": ("last_rfid_id", None), "approaching_station_id": ("approaching_station_id", None),
    "abs_loc": ("abs_loc_m", None), "movement_dir": ("movement_dir", None), "tin": ("tin", None),
    "train_length": ("train_length_m", None), "distance": ("distance_dm", 10),
    "sos_distance": ("sos_distance_dm", 10), "collision_distance": ("collision_distance_dm", 10),
}
SRC_FLAGS = {"threat_flags": ("threat_flags", "sosThreatBits"),
             "closest_flags": ("closest_flags", "sosThreatBits"),
             "eval_flags": ("eval_flags", "sosEval")}
# version 2 (session 192)
SNAP2 = {
    "version": ("version", None), "tick_ms": ("tick_ms", None), "own_abs_loc": ("own_abs_loc_dm", 10),
    "own_dir": ("own_dir", None), "own_mode": ("own_mode", None), "own_tin": ("own_tin", None),
    "own_speed": ("own_speed_x100", 100), "own_emergency_status": ("own_emergency_status", None),
    "collision_loco_id": ("collision_loco_id", None), "collision_distance": ("collision_distance_dm", 10),
    "sos_distance": ("sos_distance_dm", 10), "sos_station_id": ("sos_station_id", None),
    "n_src": ("n_src", None), "n_stn": ("n_stn", None),
}
SNAP2_FLAGS = {"lp_flags": ("lp_flags", "sosLp"), "dmi_bits": ("dmi_bits", "sosDmi"),
               "self_flags": ("self_flags", "sosSelf")}
LOCO2 = {"src_loco_id": ("loco_id", None), "src_age": ("age_ds", None), "src_abs_loc": ("abs_loc_m", None),
         "src_dir": ("dir", None), "src_tin": ("tin", None), "src_train_length": ("train_length_m", None),
         "src_sos_distance": ("sos_distance_dm", 10), "src_collision_distance": ("collision_distance_dm", 10)}
STN2 = {"stn_id": ("station_id", None), "stn_add_em": ("add_em", None), "stn_age": ("age_ds", None),
        "stn_abs_loc": ("abs_loc_m", None), "stn_sos_distance": ("sos_distance_dm", 10)}

EV = {"version": ("version", None), "tick_ms": ("tick_ms", None), "snap_id": ("snap_id", None),
      "event": ("event", None), "aux1": ("aux1", None), "aux2": ("aux2", None), "id": ("id", None),
      "distance": ("distance_dm", 10), "own_abs_loc": ("own_abs_loc_dm", 10),
      "own_speed": ("own_speed_x100", 100), "own_mode": ("own_mode", None)}


def default_logs():
    real = []
    for pat in ("**/*.cap", "**/*.log"):
        for p in sorted(glob.glob(os.path.join(REPLAY, pat), recursive=True)):
            with open(p, errors="replace") as fh:
                if any(ln.startswith("@sos_") for ln in fh):
                    real.append(p)
    return (real, False) if real else (SYNTHETIC, True)


def joined(sch, table, word):
    names = sch.flagtables.get(table, [])
    on = [names[b] for b in range(len(names)) if word & (1 << b)]
    return "; ".join(on) if on else "(none)"


def compare(rows, expect, numeric, flags, sch, errs, where):
    got = {name.strip(): val for name, val in rows}
    for name, (key, scale) in numeric.items():
        if name not in got:
            errs.append(f"{where}: no row {name}")
            continue
        m = NUM.match(got[name])
        if not m:
            errs.append(f"{where}: {name} = {got[name]!r} is not a number")
            continue
        want = expect[key] / scale if scale else expect[key]
        if abs(float(m.group(0)) - want) > (0.05 if scale else 0):
            errs.append(f"{where}: {name} engine {got[name]!r}, oracle {want}")
    for name, (key, table) in flags.items():
        want = joined(sch, table, expect[key])
        if got.get(name) != want:
            errs.append(f"{where}: {name} engine {got.get(name)!r}, oracle {want!r}")
    return got


def main(argv):
    logs, synthetic = (argv, False) if argv else default_logs()
    sch = engine.Schema(XML)
    total = ok = 0
    for path in logs:
        last_snap = None
        pending = 0
        for i, (tok, b) in enumerate(oracle.frames(path)):
            total += 1
            errs = []
            where = f"{os.path.basename(path)} #{i + 1} @{tok}"
            rows = engine.decode(b, sch, captype=tok)
            if tok == "sos" and b and b[0] == 2:
                f = oracle.snapshot_v2(b)
                if pending:
                    errs.append(f"{where}: {pending} @sossrc of snapshot {last_snap} missing")
                pending = 0
                if len(b) != f["size"]:
                    errs.append(f"{where}: {len(b)} bytes, expected {f['size']} (37 + 24 x {f['n_src']} + 13 x {f['n_stn']})")
                compare(rows, f, SNAP2, SNAP2_FLAGS, sch, errs, where)
                for k, lo in enumerate(f["locos"]):
                    sub = [(n.strip()[len(f"loco[{k + 1}] "):], v) for n, v in rows
                           if n.strip().startswith(f"loco[{k + 1}] ")]
                    got = compare(sub, lo, LOCO2, {}, sch, errs, f"{where} loco {k + 1}")
                    if int(got.get("src_threats", "-1"), 16) != lo["threats"]:
                        errs.append(f"{where} loco {k + 1}: threats engine {got.get('src_threats')!r}, oracle {lo['threats']:#x}")
                for k, st in enumerate(f["stations"]):
                    sub = [(n.strip()[len(f"station[{k + 1}] "):], v) for n, v in rows
                           if n.strip().startswith(f"station[{k + 1}] ")]
                    compare(sub, st, STN2, {}, sch, errs, f"{where} station {k + 1}")
            elif tok == "sos":
                f = oracle.snapshot(b)
                if pending:
                    errs.append(f"{where}: {pending} @sossrc of snapshot {last_snap} missing")
                if len(b) != 65 + 20 * f["n_stn_slots"]:
                    errs.append(f"{where}: {len(b)} bytes, expected {65 + 20 * f['n_stn_slots']}")
                got = compare(rows, f, SNAP, SNAP_FLAGS, sch, errs, where)
                for name, key in SNAP_HEX.items():
                    if int(got.get(name, "-1"), 16) != f[key]:
                        errs.append(f"{where}: {name} engine {got.get(name)!r}, oracle {f[key]:#x}")
                for k, st in enumerate(f["stations"]):
                    sub = [(n.strip()[len(f"station[{k + 1}] "):], v) for n, v in rows
                           if n.strip().startswith(f"station[{k + 1}] ")]
                    compare(sub, st, STN, {}, sch, errs, f"{where} station {k + 1}")
                last_snap = f["snap_id"]
                pending = bin(f["src_in_use_mask"]).count("1")
            elif tok == "sossrc":
                f = oracle.source(b)
                if len(b) != 56:
                    errs.append(f"{where}: {len(b)} bytes, expected 56")
                if f["snap_id"] != last_snap or pending <= 0:
                    errs.append(f"{where}: snapshot {f['snap_id']} is not the open one ({last_snap})")
                pending -= 1
                compare(rows, f, SRC, SRC_FLAGS, sch, errs, where)
            else:
                f = oracle.event(b)
                if len(b) != 27:
                    errs.append(f"{where}: {len(b)} bytes, expected 27")
                if last_snap is not None and f["snap_id"] > last_snap:
                    errs.append(f"{where}: names snapshot {f['snap_id']}, newest sent {last_snap}")
                compare(rows, f, EV, {}, sch, errs, where)
            if errs:
                for e in errs[:5]:
                    print("  FAIL", e)
            else:
                ok += 1
    src = "SYNTHETIC fixtures (tests/sosgen; layout only, not firmware)" if synthetic else \
          f"{len(logs)} capture(s)"
    print(f"@sos/@sossrc/@sosev from {src}")
    print(f"frames matching the oracle: {ok}/{total}")
    return 0 if total and ok == total else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
