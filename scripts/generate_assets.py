#!/usr/bin/env python3
"""Convert ccleste's indexed BMP assets to compact, immutable C arrays."""
import pathlib, sys
from PIL import Image
source = pathlib.Path(sys.argv[1])
out = pathlib.Path(__file__).resolve().parents[1] / 'assets.h'
with out.open('w') as f:
    f.write('/* Generated from ccleste indexed BMPs; see THIRD_PARTY.md. */\n#pragma once\n#include <stdint.h>\n')
    for name in ('gfx', 'font'):
        im = Image.open(source / f'{name}.bmp')
        assert im.mode == 'P' and im.width == 128 and im.height <= 128
        padded = Image.new('P', (128,128)); padded.paste(im); im = padded
        pixels = list(im.getdata())
        data = [pixels[i] | (pixels[i+1] << 4) for i in range(0,len(pixels),2)]
        f.write(f'static const uint8_t {name}_pixels[8192] = {{\n')
        for i in range(0,len(data),32): f.write(','.join(str(x) for x in data[i:i+32])+',\n')
        f.write('};\n')
