#!/usr/bin/env python3
"""Reset the knob over USB Serial/JTAG and print its log for N seconds (default 10).

  scripts/bootlog.py [seconds]

Nothing else may hold the port (close any serial terminal first). KNOB_PORT overrides the port."""
import os, re, sys, time
import serial

PORT = os.environ.get("KNOB_PORT", "/dev/ttyACM0")
ANSI = re.compile(r"\x1b\[[0-9;]*m")

def main(argv):
    seconds = float(argv[1]) if len(argv) > 1 else 10.0
    s = serial.Serial(PORT, 115200, timeout=0.2)
    s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False   # hard reset, as esptool does it
    end = time.time() + seconds
    while time.time() < end:
        raw = s.readline()
        if raw: print(ANSI.sub("", raw.decode(errors="replace")).rstrip("\r\n"))
    s.close()
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
