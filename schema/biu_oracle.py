"""biu_oracle.py -- the @biu_* frames (session 420): the BIU's CAN data bytes as
the VCC copies them to DLConsole (docs/biu-firmware, biu_dlconsole_mirror).

RDSO MP.0.01.00.31 Rev.05 Annexure-1 2.2, as biuprotocol.h and the firmware's
biu_encode_command / take_biu_frame read them. Pressure bytes 0.05 kg/cm2 per bit.
"""
import struct

KG = 0.05
SIZES = {"biu_cmd": 8, "biu_beat": 1, "biu_beatrx": 1, "biu_nmt": 2,
         "biu_press": 8, "biu_codes": 8, "biu_disc": 8}
CMD_D1 = ("brake_test_request", "health", "bp_charging_cutout", "biu_isolate",
          "speed_valid", "light_engine", "remote_loco", "dynamic_braking")
CMD_D2 = ("fault_code_ack", "display_code_ack", "a9_valid", "sa9_valid")
PRESSURES = ("bp", "bc", "mr", "a9_ref", "sa9_ref", "air_flow", "feed_pipe")
DISC = (("biu_healthy", "cmd_overridden_by_pilot", "pilot_overridden_by_biu", "manual_isolate",
         "bp_charging_cutout", "emergency_valve", "power_cutoff_relay"),
        ("panel_test_success", "panel_test_fail", "panel_test_in_progress"),
        ("bp_cutout_valve_on", "bp_control_valve_on", "bc_control_valve_on", "emergency_valve_on",
         "bp_cutout_valve_healthy", "bp_control_valve_healthy", "bc_control_valve_healthy",
         "emergency_valve_healthy"))
NMT_STATES = {0: "boot-up", 4: "stopped", 5: "operational", 127: "pre-operational"}
NMT_CS = {1: "start remote node", 2: "stop remote node", 128: "enter pre-operational",
          129: "reset node", 130: "reset communication"}


def kg(raw):
    """The schema's kg05 text for a pressure byte."""
    return f"{raw * KG:.2f} kg/cm² ({raw})"


def bits(byte, names):
    return {n: (byte >> i) & 1 for i, n in enumerate(names)}


def decode(token, frame):
    """Field -> expected engine text (the first word for enum rows)."""
    if len(frame) != SIZES[token]:
        raise ValueError(f"{token}: {len(frame)} bytes, the frame is {SIZES[token]}")
    if token == "biu_cmd":
        speed, d1, d2, a9, sa9, spare = struct.unpack("<HBBBBH", frame)
        o = {"speed_mps": f"{speed / 100.0:.2f}"}
        o.update({k: str(v) for k, v in bits(d1, CMD_D1).items()})
        o.update({k: str(v) for k, v in bits(d2, CMD_D2).items()})
        o.update({"a9_bp_target": kg(a9), "sa9_bc_target": kg(sa9), "spare": str(spare)})
        return o
    if token in ("biu_beat", "biu_beatrx"):
        name = "nmt_state" if token == "biu_beat" else "biu_nmt_state"
        return {name: f"{frame[0]} ({NMT_STATES.get(frame[0], '?')})"}
    if token == "biu_nmt":
        return {"command": f"{frame[0]} ({NMT_CS.get(frame[0], '?')})", "node": "0x%02x" % frame[1]}
    if token == "biu_press":
        o = {n: kg(frame[i]) for i, n in enumerate(PRESSURES)}
        o.update({n + "_valid": str((frame[7] >> i) & 1) for i, n in enumerate(PRESSURES)})
        return o
    if token == "biu_codes":
        iso, fault, display, spare = struct.unpack("<HHHH", frame)
        return {"isolation_counter": str(iso), "fault_code": "0x%04x" % fault,
                "display_code": "0x%04x" % display, "spare": str(spare)}
    o = {}
    for byte, names in zip(frame[:3], DISC):
        o.update({k: str(v) for k, v in bits(byte, names).items()})
    o["spare"] = str(int.from_bytes(frame[3:8], "little"))
    return o
