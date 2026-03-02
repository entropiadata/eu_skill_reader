# Tools

Developer utility scripts for the EU Skill Reader project. Requires Python 3.6+.

## crop_points_column.py

Crops and normalizes the POINTS column from Entropia Universe skill window screenshots. Detects the POINTS header and progress bars, crops with 4px padding on all sides. Pre-cropped/narrow images get right-side trimming and height capped at 329px. Requires [Pillow](https://pypi.org/project/Pillow/) (`pip install Pillow`).

**Usage:**

```
python tools/crop_points_column.py screenshot.png                      # outputs screenshot_points.png
python tools/crop_points_column.py screenshot.png -o custom_name.png   # custom output name
python tools/crop_points_column.py *.png -d output_folder              # batch process into a folder
python tools/crop_points_column.py -i screenshots/ -d crops/           # process all PNGs from a folder
```

## strip_png_metadata.py

Strips non-essential metadata chunks from PNG files to remove PII and other unwanted data (EXIF, text comments, timestamps, etc.). Keeps only chunks required for correct image rendering.

**Usage:**

```
python tools/strip_png_metadata.py <path> [path ...]
```

Paths can be individual `.png` files or directories (searched recursively).

**Example:**

```
python tools/strip_png_metadata.py resources/ testdata/
```
