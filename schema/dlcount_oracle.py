"""dlcount_oracle.py -- the @dlcount frame (session 403) as the firmware packs it.

Send_Counter_Messege_To_Data_Logger, PACK, little-endian:
    STRUCT_MESSAGE_HEADER_NEW  src u8, dest u8, message_id u16, message_length u16, seq_num u16
    counter_value              u8   0 isolation, 1 SOS, 2 OV trip (255: not sent)
    CRC                        crcFast(buffer, 9): JAMCRC init 0, u32 LE (ASSUMED u32)
"""
import struct

from crc_algos import jamcrc

LKAVACH, LDLU, MSG_ID = 2, 6, 105
SIZE = 13
COUNTERS = {0: "Isolation counter", 1: "SOS counter", 2: "OV trip counter"}


def encode(counter, seq=0):
    head = struct.pack("<BBHHHB", LKAVACH, LDLU, MSG_ID, SIZE, seq, counter)
    return head + struct.pack("<I", jamcrc(head, 0, len(head)))


def decode(frame):
    if len(frame) != SIZE:
        raise ValueError(f"{len(frame)} bytes, the struct is {SIZE}")
    src, dest, mid, length, seq, counter, crc = struct.unpack("<BBHHHBI", frame)
    return {
        "msg.src_id": src, "msg.dest_id": dest, "msg.message_id": mid,
        "msg.message_length": length, "msg.seq_num": seq, "counter_value": counter,
        "crc": crc, "crc_ok": crc == jamcrc(frame, 0, 9),
    }
