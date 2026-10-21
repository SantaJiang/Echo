#!/usr/bin/env python3
"""Regenerate PNG sizes, ICNS and ICO from data/echo.png on macOS.

Uses macOS sips/iconutil and Python's standard library; no build dependency.
The SVG is the editable design source. Export it to the 1024px master PNG
before running this script after a design change.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
data = root / "data"
icons = data / "icons"
icons.mkdir(exist_ok=True)

def resize(size, target):
    subprocess.run(["sips", "-z", str(size), str(size), str(data / "echo.png"),
                    "--out", str(target)], check=True, stdout=subprocess.DEVNULL)

for size in (16, 32, 48, 64, 128, 256, 512):
    resize(size, icons / f"echo-{size}.png")

with tempfile.TemporaryDirectory(prefix="echo-icons-") as temp:
    iconset = Path(temp) / "echo.iconset"
    iconset.mkdir()
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            name = f"icon_{size}x{size}" + ("@2x" if scale == 2 else "") + ".png"
            if size * scale == 1024:
                shutil.copyfile(data / "echo.png", iconset / name)
            else:
                shutil.copyfile(icons / f"echo-{size * scale}.png", iconset / name)
    subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(data / "echo.icns")], check=True)

sizes = (16, 32, 48, 64, 128, 256)
payloads = [(icons / f"echo-{size}.png").read_bytes() for size in sizes]
offset = 6 + 16 * len(sizes)
entries = []
for size, payload in zip(sizes, payloads):
    entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0,
                               1, 32, len(payload), offset))
    offset += len(payload)
(data / "echo.ico").write_bytes(struct.pack("<HHH", 0, 1, len(sizes))
                               + b"".join(entries) + b"".join(payloads))
print("Generated PNG, ICNS and ICO assets.")
