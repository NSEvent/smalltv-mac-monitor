#!/usr/bin/env python3
"""Write Wi-Fi and metrics settings into a prebuilt SmallTV Mac firmware image.

Usage: python3 scripts/configure-bin.py <cube.conf> <firmware.bin> [-o out.bin]

cube.conf is plain KEY=value lines (see cube.conf.example). The output image
joins your Wi-Fi on first boot, so it contains your Wi-Fi password: don't
share it or commit it. No compiler needed; Python 3 standard library only.
"""
import argparse
import struct
import sys
import time
from pathlib import Path

MAGIC = b"SMALLTV-MAC-CFG1"
# magic, rev, ssid, pass, url, fallback, host, rot, bright, pad (firmware/src/config.cpp)
LAYOUT = struct.Struct("<16sI33s65s160s160s25sBB47s")
assert LAYOUT.size == 512


def read_conf(path):
    vals = {}
    for n, raw in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            sys.exit(f"{path}:{n}: expected KEY=value")
        key, value = line.split("=", 1)
        vals[key.strip().upper()] = value.strip()
    return vals


def field(vals, key, size, required=False):
    value = vals.get(key, "")
    if required and not value:
        sys.exit(f"cube.conf: {key} is required")
    data = value.encode("utf-8")
    if len(data) >= size:
        sys.exit(f"cube.conf: {key} is too long (max {size - 1} bytes)")
    return data


def url_field(vals, key, required=False):
    value = vals.get(key, "")
    if value and not value.startswith("http://"):
        sys.exit(f"cube.conf: {key} must start with http:// (the cube has no TLS)")
    return field(vals, key, 160, required)


def number(vals, key, default, lo, hi):
    raw = vals.get(key, "") or str(default)
    try:
        value = int(raw)
    except ValueError:
        sys.exit(f"cube.conf: {key} must be a number")
    if not lo <= value <= hi:
        sys.exit(f"cube.conf: {key} must be {lo}-{hi}")
    return value


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("conf")
    ap.add_argument("firmware")
    ap.add_argument("-o", "--output")
    args = ap.parse_args()

    vals = read_conf(args.conf)
    host = vals.get("HOST", "") or "smalltv-mac"
    if not all(ch.isalnum() or ch == "-" for ch in host):
        sys.exit("cube.conf: HOST must be letters, digits, and dashes")
    vals["HOST"] = host

    image = bytearray(Path(args.firmware).read_bytes())
    at = image.find(MAGIC)
    if at < 0 or image.find(MAGIC, at + 1) >= 0:
        sys.exit("no single settings slot found; use a firmware.bin built from this repo")
    if image[0] != 0xE9:
        sys.exit("not an ESP8266 firmware image")

    rev = int(time.time())
    slot = LAYOUT.pack(
        MAGIC,
        rev,
        field(vals, "SSID", 33, required=True),
        field(vals, "PASS", 65),
        url_field(vals, "URL", required=True),
        url_field(vals, "FALLBACK"),
        field(vals, "HOST", 25),
        number(vals, "ROTATION", 0, 0, 3),
        number(vals, "BRIGHTNESS", 30, 5, 100),
        b"",
    )
    image[at:at + LAYOUT.size] = slot

    out = Path(args.output or f"smalltv-{host}.bin")
    out.write_bytes(image)
    print(f"wrote {out} ({len(image)} bytes): host={host} url={vals['URL']}")
    print("This file contains your Wi-Fi password. Don't share or commit it.")


if __name__ == "__main__":
    main()
