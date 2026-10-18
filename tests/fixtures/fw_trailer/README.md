# fw_trailer

`KAVACH_Input_Card_stamped_1.2.9.appimage`: 5000 random bytes (seed 298)
stamped by the updater repo's own `tools/fw_stamp.py` (updater 2.6.1):

    python fw_stamp.py stamp body.bin KAVACH_Input_Card_stamped_1.2.9.appimage --card 2 --version 1.2.9

so the flasher's trailer reader is checked against the real tool's output
(session 298). `fw_stamp.py show` reports: card 2 (Input), version 1.2.9,
hw rev 0, image 5000 bytes + 24-byte trailer, built 2026-10-16 23:11:20
(local time, UNIX 0x6AD261C0).

`KAVACH_Input_Card_signed_1.2.9.appimage`: the stamped image above signed
by the updater's own `tools/fw_sign.py` (updater 2.7.0) with a throw-away
key (not kept) and key id 1 (session 313):

    python fw_sign.py sign KAVACH_Input_Card_stamped_1.2.9.appimage KAVACH_Input_Card_signed_1.2.9.appimage --key test.pem --key-id 1

`fw_sign.py show` reports: signed, key 1, version 1, alg 1 (Ed25519),
body 5024 bytes (+72 signature), trailer present.

`fw_pubkeys_two.h`, `fw_pubkeys_swapped.h`, `fw_pubkeys_one.h`: written by
`fw_sign.py header` (CRLF, as it writes them) from two throw-away public
keys (session 314): "other" and the "test" key that signed the image above.
`two` is other, test (key 1 matches: `fw_sign.py verify --header` says OK),
`swapped` is test, other (key 1 does not match), `one` is other only (key 1
unknown). `fw_pubkeys_none.h` is the updater 2.7.0 repo's shipped header,
with no keys.

Key headers (session 314), written by `fw_sign.py header` (CRLF, as the
tool writes them) from that throw-away key (`test`, the one that signed the
image above) and a second throw-away key (`other`). Public keys only:

- `fw_pubkeys_two.h`: other, test (key 1 matches the signed image);
- `fw_pubkeys_swapped.h`: test, other (key 1 does not match);
- `fw_pubkeys_one.h`: other (key 1 is unknown);
- `fw_pubkeys_none.h`: the updater's own shipped file (2.7.0), no keys.

`fw_sign.py verify --header fw_pubkeys_two.h` says "Signature OK (key 1)",
and with `fw_pubkeys_swapped.h` "Signature does NOT match (key 1)".
