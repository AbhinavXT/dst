#!/usr/bin/env python3
# Session 316: stands in for the updater's tools/fw_sign.py in the tests, so
# the flasher's call (arguments, passphrase, errors) is checked without the
# 'cryptography' package or a private key. "sign IN OUT --key K --key-id N":
# OUT becomes the image signed by the real tool (session 313's fixture) when
# IN is its stamped original, else IN + a copy of that signature block.
# FW_SIGN_PASSPHRASE "wrong" fails the way the real tool does.
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
args = sys.argv[1:]
if len(args) != 7 or args[0] != "sign" or args[3] != "--key" or args[5] != "--key-id":
    sys.exit("fake_fw_sign: unexpected arguments %r" % args)
if os.environ.get("FW_SIGN_PASSPHRASE") == "wrong":
    print("Traceback (most recent call last):\n  ...", file=sys.stderr)
    print("ValueError: Bad decrypt. Incorrect password?", file=sys.stderr)
    sys.exit(1)
signed = open(os.path.join(here, "KAVACH_Input_Card_signed_1.2.9.appimage"), "rb").read()
body = open(args[1], "rb").read()
open(args[2], "wb").write(body + signed[-72:])
print("Signed %s: key %s (passphrase %s)" % (args[2], args[6],
      "set" if os.environ.get("FW_SIGN_PASSPHRASE") else "none"))
