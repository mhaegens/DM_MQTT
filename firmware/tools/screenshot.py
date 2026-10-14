#!/usr/bin/env python3
"""Trigger and capture a screenshot from the ESP32 device.

Usage: python3 tools/screenshot.py [/dev/cu.usbmodemXXXX] [output.png]
       --screen tanks|debug|profiles --boot-wait 14

The script sends 'S' to the device serial port, waits for the binary
frame (framed with SCRN/SCRN_END markers), converts RGB565 to RGB888,
and saves the result as a PNG.

Requires: pyserial, Pillow
  pip3 install pyserial Pillow
"""
import serial
import argparse
import struct
import time
import re
import zlib
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', nargs='?', default=find_port())
    parser.add_argument('output', nargs='?', default='screenshot.png')
    parser.add_argument('--screen', choices=['tanks', 'debug', 'profiles'])
    parser.add_argument('--boot-wait', type=float, default=14)
    args = parser.parse_args()
    port, out = args.port, args.output

    print(f"Connecting to {port}...")
    # The CH343 programming connection can reset this board when opened.
    # Hold one connection through startup, navigation and the entire capture.
    s = serial.Serial(port=None, baudrate=115200, timeout=.2)
    s.dtr = False
    s.rts = False
    s.port = port
    with s:
        ready = time.monotonic() + args.boot_wait
        while time.monotonic() < ready:
            s.read(s.in_waiting or 1)
        if args.screen:
            s.write({'tanks': b'T', 'debug': b'D', 'profiles': b'C'}[args.screen])
            time.sleep(2)
        s.reset_input_buffer()
        s.write(b'S')
        s.flush()
        print("Waiting for SCRN header...")
        deadline = time.monotonic() + 145
        while time.monotonic() < deadline:
            line = s.readline().decode(errors='ignore').strip()
            if line == 'SCRN':
                break
        else:
            raise IOError('No screenshot header received before timeout')

        w   = int(s.readline())
        h   = int(s.readline())
        fmt = s.readline().decode(errors='ignore').strip()
        if fmt not in ('RGB565', 'RLE565') or w <= 0 or h <= 0 or w * h > 4_000_000:
            raise IOError(f'Invalid screenshot header: {w}x{h} {fmt}')
        print(f"Receiving {w}x{h} {fmt} ({w * h * 2} bytes)...")
        expected = w * h * 2
        raw = bytearray()
        if fmt == 'RLE565':
            while time.monotonic() < deadline:
                line = s.readline().strip()
                if line == b'SCRN_END':
                    break
                if not line:
                    continue
                if len(line) > 512 or len(line) % 8 or not re.fullmatch(b'[0-9a-fA-F]+', line):
                    raise IOError('Invalid screenshot run data; no image saved')
                for i in range(0, len(line), 8):
                    count = int(line[i:i+4], 16)
                    color = int(line[i+4:i+8], 16)
                    if count == 0 or len(raw) + count * 2 > expected:
                        raise IOError('Invalid screenshot run length; no image saved')
                    raw.extend(struct.pack('<H', color) * count)
            else:
                raise IOError('Screenshot transfer timed out; no image saved')
        else:
            while len(raw) < expected and time.monotonic() < deadline:
                raw.extend(s.read(expected - len(raw)))
        if len(raw) != expected:
            raise IOError(f"Serial timeout: received {len(raw)}/{expected} bytes")
        s.timeout = 2
        if fmt == 'RGB565':
            s.readline()          # consume newline before SCRN_END
            end = s.readline().decode(errors='ignore').strip()
            if end != 'SCRN_END':
                raise IOError(f"Expected SCRN_END, got '{end}'")
        marker = re.fullmatch(r'SCRN_CRC32 ([0-9a-fA-F]{8})', s.readline().decode(errors='ignore').strip())
        if not marker or zlib.crc32(raw) != int(marker.group(1), 16):
            raise IOError('Screenshot checksum missing or incorrect; no image saved')

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
