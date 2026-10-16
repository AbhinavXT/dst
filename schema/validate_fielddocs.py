"""Session 301: schema/fielddocs.json must be exactly what build_fielddocs.py
makes. The descriptions are edited in the script (where each one sits beside
its annexure clause and page), never in the JSON by hand."""
import os
import subprocess
import sys
import tempfile

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "fielddocs.json")
        subprocess.run([sys.executable, "-I", os.path.join(here, "build_fielddocs.py"), out],
                       check=True, stdout=subprocess.DEVNULL)
        built = open(out, "rb").read().replace(b"\r\n", b"\n")
    # A Windows checkout may carry CRLF line ends; the content is what counts.
    committed = open(os.path.join(here, "fielddocs.json"), "rb").read().replace(b"\r\n", b"\n")
    if built != committed:
        print("FAIL: fielddocs.json differs from build_fielddocs.py's output: "
              "edit the script and run it, do not edit the JSON")
        sys.exit(1)
    print("PASS: fielddocs.json matches build_fielddocs.py")
