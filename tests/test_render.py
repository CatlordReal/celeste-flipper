#!/usr/bin/env python3
"""Focused native viewport, sprite geometry, text and freeze regressions."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
lib = Path(tempfile.gettempdir()) / 'celeste-render-test.dylib'
subprocess.run(['cc', '-DHOST_TEST', '-std=c11', '-Wall', '-Wextra', '-shared',
                str(ROOT / 'render.c'), '-o', str(lib)], check=True)
g = C.CDLL(str(lib))
g.render_callback.argtypes = [C.c_int]
frame = (C.c_uint8 * 8192).in_dll(g, 'render_frame')
mono = (C.c_uint8 * 1024)()

def call(kind, *args):
    g.render_callback(kind, *args)

def begin(room=0):
    call(8, 0, 0, 128, 128, 0)
    call(13, room, 0, 0, 0, 0, 0, 4)

def spr(tile, x, y, flip=0):
    call(1, tile, x, y, 1, 1, flip, 0)

def bit(x, y):
    return (mono[y * 16 + x // 8] >> (x % 8)) & 1

def colour(x, y):
    n = y * 128 + x
    return (frame[n // 2] >> (4 * (n % 2))) & 15

def render(mode=0):
    original = bytes(frame)
    g.render_mono(mono, mode)
    assert bytes(frame) == original, 'LCD conversion modified USB colour frame'
    return bytes(mono)

def markers():
    # Encode each world row in a seven-bit strip; row zero identifies crop top.
    for y in range(128):
        for b in range(7):
            if y & (1 << b):
                call(8, 120+b, y, 120+b, y, 7)

def top():
    return sum(bit(120+b, 0) << b for b in range(7))

# Exact original 8x8 silhouettes, all animation frames, directions and parities.
for tile in range(1, 8):
    for flip in (0, 1):
        for parity in (0, 1):
            g.render_init(); begin(); markers(); spr(tile, 32+parity, 60+parity)
            render(); crop = top()
            for y in range(8):
                for x in range(8):
                    c = colour(32+parity+x, 60+parity+y)
                    assert bit(32+parity+x, 60+parity+y-crop) == (c not in (0, 15)), (tile, flip, parity, x, y)
            assert render() == bytes(mono)

# Tight follower: 2px dead zone, <=4px lag, edge clamps, room snap and freeze.
g.render_init()
for y in (60, 61, 62, 66, 77, 89, 101, 120, 119, 90, 64, 30, 0):
    begin(); markers(); spr(1, 32, y); first = render(); crop = top()
    target = min(64, max(0, y-28))
    assert 0 <= crop <= 64 and abs(crop-target) <= 4, (y, crop, target)
    assert render() == first, 'Repeated draw/frozen game moved viewport'
begin(16); markers(); spr(1, 32, 100); render(); assert top() == 64
# After death the camera holds; a newly spawned player gets a valid initial view.
begin(16); markers(); render(); assert top() == 64
begin(16); markers(); spr(1, 32, 32); render(); assert top() == 4

# Timer/altitude, credits, memorial and summit remain fully visible at either edge.
for player_y in (0, 120):
    for mode in (0, 1):
        for text, x, y, c, screen_y in (
            (b'00:00:00', 5, 5, 7, 5), (b'100 m', 54, 62, 7, 12),
            (b'matt thorson', 42, 96, 5, 48), (b'noel berry', 46, 102, 5, 54),
            (b'A', 8, 96, 0, 30), (b'B', 8, 103, 0, 37),
            (b'C', 8, 110, 0, 44), (b'deaths:123', 48, 24, 7, 24)):
            g.render_init(); begin(); call(8, 0, 0, 127, 127, 7); spr(1, 110, player_y)
            call(7, C.c_char_p(text), x, y, c); render(mode)
            sx = (128-len(text)*4+1)//2 if y == 62 else x
            assert all(bit(xx, screen_y-1) == 0 for xx in range(sx-1,sx+len(text)*4)), text
            assert all(bit(xx, screen_y+5) == 0 for xx in range(sx-1,sx+len(text)*4)), text
            assert any(bit(xx, yy) for xx in range(sx,sx+len(text)*4) for yy in range(screen_y,screen_y+5)), text
            begin(); render(mode); assert not any(mono), 'Stale text or player after clear'
print('PASS: native 8x8 animations, camera dead zone/clamps/room/death/freeze, all text panels at both viewport edges and modes; USB frame unchanged.')
