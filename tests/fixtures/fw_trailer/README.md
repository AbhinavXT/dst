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
