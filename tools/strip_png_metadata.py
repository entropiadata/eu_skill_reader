"""Strip non-essential metadata chunks from PNG files.

Keeps only chunks required for correct image display (IHDR, PLTE, tRNS,
cHRM, gAMA, iCCP, sBIT, sRGB, IDAT, IEND). Removes everything else
(tEXt, iTXt, zTXt, pHYs, tIME, eXIf, etc.) that could contain PII or
other unwanted metadata.

Usage:
    python tools/strip_png_metadata.py <path> [path ...]

Paths can be individual .png files or directories (searched recursively).
"""

import struct
import sys
import os

# Chunks required for correct image rendering
KEEP_CHUNKS = {
    b"IHDR", b"PLTE", b"IDAT", b"IEND",
    b"tRNS", b"cHRM", b"gAMA", b"iCCP", b"sBIT", b"sRGB",
}

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def strip_png(path):
    with open(path, "rb") as f:
        data = f.read()

    if data[:8] != PNG_SIGNATURE:
        return None

    out = bytearray(data[:8])
    pos = 8
    stripped = []

    while pos < len(data):
        if pos + 8 > len(data):
            break
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        chunk_type = data[pos + 4 : pos + 8]
        chunk_end = pos + 12 + length
        if chunk_type in KEEP_CHUNKS:
            out.extend(data[pos:chunk_end])
        else:
            stripped.append(chunk_type.decode("ascii", errors="replace"))
        pos = chunk_end

    if stripped:
        with open(path, "wb") as f:
            f.write(out)

    return stripped


def collect_pngs(paths):
    files = []
    for p in paths:
        if os.path.isfile(p):
            if p.lower().endswith(".png"):
                files.append(p)
        elif os.path.isdir(p):
            for root, _, names in os.walk(p):
                for name in sorted(names):
                    if name.lower().endswith(".png"):
                        files.append(os.path.join(root, name))
    return files


def main():
    if len(sys.argv) < 2:
        print("Usage: python strip_png_metadata.py <path> [path ...]")
        print("Paths can be .png files or directories (recursive).")
        sys.exit(1)

    files = collect_pngs(sys.argv[1:])
    if not files:
        print("No PNG files found.")
        sys.exit(1)

    modified = 0
    for path in files:
        stripped = strip_png(path)
        if stripped is None:
            print(f"  SKIP (not PNG): {path}")
        elif stripped:
            print(f"  Stripped: {path}  removed {stripped}")
            modified += 1
        else:
            print(f"  Clean:    {path}")

    print(f"\n{len(files)} files checked, {modified} modified.")


if __name__ == "__main__":
    main()
