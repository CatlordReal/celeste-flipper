#!/usr/bin/env python3
"""Build the 10x10 FAP launcher icon from Madeline's original idle sprite."""
from pathlib import Path
import re
from PIL import Image

root = Path(__file__).resolve().parents[1]
source = (root / 'assets.h').read_text().split('gfx_pixels[8192] = {', 1)[1].split('}', 1)[0]
pixels = list(map(int, re.findall(r'\d+', source)))
icon = Image.new('1', (10, 10), 1)
for y in range(8):
    for x in range(8):
        n = y * 128 + 8 + x
        colour = (pixels[n // 2] >> (4 * (n % 2))) & 15
        if colour not in (0, 15):
            icon.putpixel((x + 1, y + 1), 0)
icon.save(root / 'celeste_icon.png')
