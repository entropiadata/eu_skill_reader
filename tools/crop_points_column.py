#!/usr/bin/env python3
"""
Crop and normalize the POINTS column from an Entropia Universe skill window screenshot.

Detects the POINTS header and progress bars, crops with 4px padding on all sides.
Right edge: 4px after the rightmost progress bar pixel.
Bottom edge: 4px after the last progress bar row (full screenshots only).
Pre-cropped/narrow images get right-side trimming and height capped at 329px.

Usage:
    python crop_points_column.py screenshot.png                    # outputs screenshot_points.png
    python crop_points_column.py screenshot.png -o custom_name.png # custom output name
    python crop_points_column.py *.png -d output_folder            # batch process into a folder
    python crop_points_column.py -i screenshots/ -d crops/         # process all PNGs from a folder
"""

import argparse
import os
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Requires Pillow: pip install Pillow")
    sys.exit(1)

# Maximum expected crop dimensions (1 header + 12 data rows with progress bars).
# Crops exceeding these are flagged as bad detections and discarded.
MAX_CROP_WIDTH = 160
MAX_CROP_HEIGHT = 420


def _is_bar_pixel(r, g, b):
    """Detect teal or gray progress bar pixels."""
    br = (r + g + b) // 3
    is_teal = g > 50 and g > r and b > 30 and br > 40
    is_gray = br > 45 and abs(r - g) < 20 and abs(g - b) < 20
    return is_teal or is_gray


def find_points_header(img):
    """Find the top-left corner of the POINTS header text."""
    w, h = img.size
    px = img.load()
    header_region_h = max(60, h // 6)

    best_top = None
    best_left = None

    for y in range(0, header_region_h):
        bright_xs = []
        for x in range(0, w):
            r, g, b = px[x, y][:3]
            if r > 170 and g > 170 and b > 170:
                bright_xs.append(x)

        if len(bright_xs) < 3:
            continue

        segments = []
        seg_start = bright_xs[0]
        prev = bright_xs[0]
        for bx in bright_xs[1:]:
            if bx - prev > 100:
                segments.append((seg_start, prev))
                seg_start = bx
            prev = bx
        segments.append((seg_start, prev))

        if len(segments) >= 2:
            pts_left, pts_right = segments[-1]
            if best_top is None:
                best_top = y
                best_left = pts_left
            else:
                best_left = min(best_left, pts_left)

    if best_top is None:
        for y in range(0, header_region_h):
            bright_xs = [x for x in range(w)
                         if px[x, y][0] > 170 and px[x, y][1] > 170 and px[x, y][2] > 170]
            if len(bright_xs) >= 3:
                best_top = y
                best_left = min(bright_xs)
                break

    if best_top is None:
        best_left = 0
        best_top = 0

    return best_left, best_top


def find_bar_bounds(img, header_left, header_top):
    """Find the rightmost and bottommost extent of progress bars.

    Progress bars are wide horizontal bands of teal/gray pixels that start
    at the left edge of the column. This distinguishes them from "Total:"
    text which starts offset to the right.
    """
    w, h = img.size
    px = img.load()

    bar_right = header_left
    bar_bottom = header_top

    for y in range(header_top, h):
        bar_xs = []
        scan_right = min(w, header_left + 150)
        for x in range(header_left, scan_right):
            r, g, b = px[x, y][:3]
            if _is_bar_pixel(r, g, b):
                bar_xs.append(x)

        if len(bar_xs) > 30:
            span = max(bar_xs) - min(bar_xs)
            left_edge = min(bar_xs)
            # Real progress bars start near the column left edge
            if span > 50 and left_edge <= header_left + 5:
                right = max(bar_xs)
                if right > bar_right:
                    bar_right = right
                bar_bottom = y

    return bar_right, bar_bottom


def crop_points_column(input_path, output_path=None, padding=4):
    """Crop the POINTS column from a skill window screenshot."""
    img = Image.open(input_path)
    w, h = img.size

    is_precropped = w < 300

    header_left, header_top = find_points_header(img)
    bar_right, bar_bottom = find_bar_bounds(img, header_left, header_top)

    crop_x0 = max(0, header_left - padding)
    crop_y0 = max(0, header_top - padding)
    crop_x1 = min(w, bar_right + padding + 1)

    max_height = 329

    if is_precropped:
        crop_y1 = min(h, crop_y0 + max_height)
        mode = "pre-cropped" + (" (trimmed)" if crop_y0 + max_height < h else "")
    else:
        crop_y1 = min(h, bar_bottom + padding + 1, crop_y0 + max_height)
        mode = "full"

    cropped = img.crop((crop_x0, crop_y0, crop_x1, crop_y1))

    if output_path is None:
        p = Path(input_path)
        output_path = p.parent / f"{p.stem}_points{p.suffix}"

    cropped.save(output_path)
    cw, ch = cropped.size
    print(f"{Path(input_path).name} -> {Path(output_path).name}  ({cw}x{ch})  [{mode}]")
    print(f"  Header at ({header_left}, {header_top}), bars to ({bar_right}, {bar_bottom})")
    print(f"  Crop: ({crop_x0},{crop_y0})-({crop_x1},{crop_y1})")

    return output_path, cw, ch


def _is_real_png(filepath):
    """Check if a file is actually a PNG (not a JPEG with .png extension)."""
    try:
        with open(filepath, "rb") as f:
            header = f.read(8)
        return header[:8] == b"\x89PNG\r\n\x1a\n"
    except (IOError, OSError):
        return False


def _collect_inputs(args):
    """Build the list of input files from args.inputs and/or args.input_dir."""
    files = []

    if args.input_dir:
        input_dir = Path(args.input_dir)
        if not input_dir.is_dir():
            print(f"ERROR: input directory not found: {args.input_dir}")
            sys.exit(1)
        candidates = sorted(input_dir.glob("*.png"))
        for f in candidates:
            if _is_real_png(f):
                files.append(str(f))
            else:
                print(f"  SKIP {f.name} (not a real PNG)")

    if args.inputs:
        files.extend(args.inputs)

    return files


def main():
    parser = argparse.ArgumentParser(
        description="Crop POINTS column from EU skill window screenshots"
    )
    parser.add_argument("inputs", nargs="*", help="Screenshot file(s) to process")
    parser.add_argument("-i", "--input-dir",
                        help="Input directory to process all real .png files from (non-recursive)")
    parser.add_argument("-o", "--output", help="Output filename (single file only)")
    parser.add_argument("-d", "--output-dir", help="Output directory for batch processing")
    parser.add_argument("--padding", type=int, default=4,
                        help="Pixels of padding on all sides (default: 4)")
    args = parser.parse_args()

    if not args.inputs and not args.input_dir:
        parser.error("provide input files and/or --input-dir")

    files = _collect_inputs(args)
    if not files:
        print("No valid PNG files to process.")
        sys.exit(0)

    if args.output and len(files) > 1:
        print("ERROR: -o/--output only works with a single input file")
        sys.exit(1)

    if args.output_dir:
        out_dir = Path(args.output_dir)
        out_dir.mkdir(parents=True, exist_ok=True)

    ok_count = 0
    discard_count = 0

    for inp in files:
        if args.output:
            out = args.output
        elif args.output_dir:
            out = str(Path(args.output_dir) / Path(inp).name)
        else:
            out = None

        try:
            out_path, cw, ch = crop_points_column(inp, out, padding=args.padding)

            if cw > MAX_CROP_WIDTH or ch > MAX_CROP_HEIGHT:
                os.remove(out_path)
                print(f"  DISCARD {Path(out_path).name} ({cw}x{ch}) exceeds "
                      f"max {MAX_CROP_WIDTH}x{MAX_CROP_HEIGHT}")
                discard_count += 1
            else:
                ok_count += 1

        except Exception as e:
            print(f"ERROR processing {inp}: {e}")

    print(f"\nDone: {ok_count} crops saved, {discard_count} discarded")


if __name__ == "__main__":
    main()