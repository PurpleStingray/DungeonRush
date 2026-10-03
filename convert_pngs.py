from pathlib import Path
from PIL import Image

root = Path("res/drawable")

for f in sorted(root.glob("*.png")):
    im = Image.open(f)
    original_mode = im.mode
    im = im.convert("RGBA")          # force color type 6, 8 bits per channel
    im.save(f, "PNG", optimize=False, compress_level=6)
    print(f"{f.name}: {original_mode} -> RGBA")