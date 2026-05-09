#!/usr/bin/env python3
"""Trigger and capture a screenshot from the ESP32 device.

Usage: python3 tools/screenshot.py [/dev/cu.usbmodemXXXX] [output.png]

The script sends 'S' to the device serial port, waits for the binary
frame (framed with SCRN/SCRN_END markers), converts RGB565 to RGB888,
and saves the result as a PNG.

Requires: pyserial, Pillow
  pip3 install pyserial Pillow
"""
import serial
import sys
import struct
from PIL import Image


def find_port():
    import glob
    candidates = (glob.glob('/dev/cu.usbmodem*')
                  + glob.glob('/dev/ttyUSB*')
                  + glob.glob('/dev/ttyACM*'))
    if candidates:
        return candidates[0]
    return '/dev/cu.usbmodem1'


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    out  = sys.argv[2] if len(sys.argv) > 2 else 'screenshot.png'

    print(f"Connecting to {port}...")
    with serial.Serial(port, 115200, timeout=120) as s:
        s.reset_input_buffer()
        s.write(b'S')
        s.flush()
        print("Waiting for SCRN header...")
        while True:
            line = s.readline().decode(errors='ignore').strip()
            if line == 'SCRN':
                break

        w   = int(s.readline())
        h   = int(s.readline())
        fmt = s.readline().decode(errors='ignore').strip()
        print(f"Receiving {w}x{h} {fmt} ({w * h * 2} bytes)...")
        expected = w * h * 2
        raw = b''
        while len(raw) < expected:
            chunk = s.read(expected - len(raw))
            if not chunk:
                raise IOError(f"Serial timeout: received {len(raw)}/{expected} bytes")
            raw += chunk
        s.readline()          # consume newline before SCRN_END
        end = s.readline().decode(errors='ignore').strip()
        if end != 'SCRN_END':
            print(f"Warning: expected SCRN_END, got '{end}'")

    # Convert RGB565 -> RGB888
    pixels = []
    for i in range(0, len(raw), 2):
        px = struct.unpack_from('<H', raw, i)[0]
        r = ((px >> 11) & 0x1F) * 255 // 31
        g = ((px >>  5) & 0x3F) * 255 // 63
        b = ( px        & 0x1F) * 255 // 31
        pixels.append((r, g, b))

    img = Image.new('RGB', (w, h))
    img.putdata(pixels)
    img.save(out)
    print(f"Saved {out} ({w}x{h})")


if __name__ == '__main__':
    main()
