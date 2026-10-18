#!/usr/bin/env python3
"""Render the PhotoDeck SVG master and assemble deterministic native icon assets.

Development-only dependencies: Inkscape and Pillow. Normal app builds use the
checked-in assets and do not require either tool. Run from any working directory.
"""
from __future__ import annotations

import argparse
import io
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SIZES = (16, 24, 32, 48, 64, 128, 256, 512, 1024)
ICO_SIZES = tuple(size for size in SIZES if size <= 256)
# PNG-backed ICNS representations, including native and Retina menu/Dock sizes.
ICNS_TYPES = ((b"icp4", 16), (b"icp5", 32), (b"icp6", 64), (b"ic07", 128),
              (b"ic08", 256), (b"ic09", 512), (b"ic10", 1024),
              (b"ic11", 32), (b"ic12", 64), (b"ic13", 256), (b"ic14", 512))


def png_bytes(image: Image.Image) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=False, compress_level=9)
    return buffer.getvalue()


def generate(output: Path, inkscape: str) -> None:
    output.mkdir(parents=True, exist_ok=True)
    icons = output / "icons"
    icons.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="photodeck-icon-") as temporary:
        rendered = Path(temporary) / "master.png"
        subprocess.run([inkscape, str(ROOT / "packaging/photodeck.svg"),
                        "--export-area-page", "--export-width=4096",
                        f"--export-filename={rendered}"], check=True,
                       env={**os.environ, "INKSCAPE_PROFILE_DIR": str(Path(temporary) / "profile"),
                            "XDG_CACHE_HOME": str(Path(temporary) / "cache")})
        with Image.open(rendered) as source:
            master = source.convert("RGBA")
        frames = {size: master.resize((size, size), Image.Resampling.LANCZOS)
                  for size in SIZES}
    encoded = {size: png_bytes(frame) for size, frame in frames.items()}
    for size, data in encoded.items():
        (icons / f"photodeck-{size}.png").write_bytes(data)
    (output / "photodeck.png").write_bytes(encoded[512])
    # Modern Windows supports PNG-backed ICO frames. Reuse their exact bytes.
    directory = bytearray(struct.pack("<HHH", 0, 1, len(ICO_SIZES)))
    offset = 6 + 16 * len(ICO_SIZES)
    for size in ICO_SIZES:
        edge = size if size < 256 else 0
        directory.extend(struct.pack("<BBBBHHII", edge, edge, 0, 0, 1, 32,
                                     len(encoded[size]), offset))
        offset += len(encoded[size])
    (output / "photodeck.ico").write_bytes(bytes(directory) + b"".join(encoded[size] for size in ICO_SIZES))
    chunks = b"".join(kind + struct.pack(">I", 8 + len(encoded[size])) + encoded[size]
                      for kind, size in ICNS_TYPES)
    (output / "photodeck.icns").write_bytes(b"icns" + struct.pack(">I", 8 + len(chunks)) + chunks)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "packaging",
                        help="output directory; defaults to the checked-in packaging directory")
    parser.add_argument("--inkscape", default=shutil.which("inkscape"), help="Inkscape executable")
    args = parser.parse_args()
    if not args.inkscape:
        parser.error("Inkscape is required to render the SVG master")
    generate(args.output.resolve(), args.inkscape)


if __name__ == "__main__":
    main()
