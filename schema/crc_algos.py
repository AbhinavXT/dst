"""crc_algos.py -- port of capturedecoder.cpp CaptureDecoder::jamcrc.
Reflected CRC-32 (poly 0x04C11DB7, init 0, reflect in/out, no final xor),
matching the C++ exactly. Used as the engine's 'jamcrc' CRC algo and by oracles."""

def _reflect8(x):
    r = 0
    for i in range(8):
        if x & (1 << i): r |= 1 << (7 - i)
    return r

def _reflect32(x):
    r = 0
    for i in range(32):
        if x & (1 << i): r |= 1 << (31 - i)
    return r

def jamcrc(data, frm, length):
    crc = 0
    for i in range(frm, frm + length):
        crc ^= _reflect8(data[i]) << 24
        crc &= 0xFFFFFFFF
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if (crc & 0x80000000) else (crc << 1) & 0xFFFFFFFF
    return _reflect32(crc)

def rfidcrc30(data, frm, length):
    """Anx-D RFID CRC-30 (poly 0x2030B9C7, init/xorout 0x3FFFFFFF) over
    `length` bytes from `frm`, with the LAST byte masked to its low 2 bits
    (the high 6 bits hold the start of the stored CRC field). Mirrors
    decodeRfid: CRC over tag bytes 0..12 with d[12] &= 0x03, where the tag
    begins at frame byte 1, so callers pass from=1 len=13."""
    d = bytearray(data[frm:frm + length])
    if d:
        d[-1] &= 0x03
    MASK, POLY, TOP = 0x3FFFFFFF, 0x2030B9C7, 1 << 29
    crc = 0x3FFFFFFF
    for byte in d:
        crc = (crc ^ (byte << 22)) & MASK
        for _ in range(8):
            crc = (((crc << 1) ^ POLY) & MASK) if (crc & TOP) else ((crc << 1) & MASK)
    return (crc ^ 0x3FFFFFFF) & MASK

CRC_ALGOS = { "jamcrc": jamcrc, "rfidcrc30": rfidcrc30 }
