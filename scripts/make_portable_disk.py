#!/usr/bin/env python3
"""Create deterministic 32 MiB FAT16 Celeste launcher disk image."""
from __future__ import annotations
import argparse
from pathlib import Path

SECTOR = 512
TOTAL_SECTORS = 65536
FAT_SECTORS = 256
FAT_COUNT = 2
ROOT_ENTRIES = 512
ROOT_SECTORS = ROOT_ENTRIES * 32 // SECTOR
DATA_START = 1 + FAT_COUNT * FAT_SECTORS + ROOT_SECTORS
IMAGE_SIZE = TOTAL_SECTORS * SECTOR
LAUNCHERS = {b"MACARM64   ": "Celeste-Mac-arm64", b"MACINTEL  ": "Celeste-Mac-amd64", b"WINDOWS EXE": "Celeste-Windows.exe", b"LINUX64    ": "Celeste-Linux-amd64"}
README = b"""Celeste Classic Portable\r\n\r\nRun launcher matching your computer:\r\nMACARM64   Apple Silicon macOS\r\nMACINTEL   Intel macOS\r\nWINDOWS.EXE Windows\r\nLINUX64    Linux x86_64\r\n\r\nLauncher opens local page; no browser USB permission.\r\nOn Flipper start Celeste Classic then enable bridge in USB menu.\r\nNo autorun, install, or self-delete. Eject disk manually when finished.\r\n"""

def set_fat16_entry(fat: bytearray, cluster: int, value: int) -> None:
    fat[cluster * 2:cluster * 2 + 2] = value.to_bytes(2, "little")

def clusters_for(size: int) -> int:
    return max(1, (size + SECTOR - 1) // SECTOR)

def write_file(image: bytearray, fat: bytearray, first_cluster: int, data: bytes) -> int:
    count = clusters_for(len(data))
    for index in range(count):
        cluster = first_cluster + index
        set_fat16_entry(fat, cluster, 0xFFFF if index == count - 1 else cluster + 1)
        offset = (DATA_START + cluster - 2) * SECTOR
        chunk = data[index * SECTOR:(index + 1) * SECTOR]
        image[offset:offset + len(chunk)] = chunk
    return first_cluster + count

def root_entry(name: bytes, cluster: int, size: int) -> bytes:
    entry = bytearray(32); entry[:11] = name; entry[11] = 0x20
    entry[26:28] = cluster.to_bytes(2, "little"); entry[28:32] = size.to_bytes(4, "little")
    return bytes(entry)

def build_image(files: dict[bytes, bytes]) -> bytes:
    image = bytearray(IMAGE_SIZE); boot = memoryview(image)[:SECTOR]
    boot[:3] = b"\xEB\x3C\x90"; boot[3:11] = b"CELESTE "
    boot[11:13] = SECTOR.to_bytes(2, "little"); boot[13] = 1; boot[14:16] = (1).to_bytes(2, "little"); boot[16] = FAT_COUNT
    boot[17:19] = ROOT_ENTRIES.to_bytes(2, "little"); boot[19:21] = b"\0\0"; boot[21] = 0xF8; boot[22:24] = FAT_SECTORS.to_bytes(2, "little")
    boot[24:26] = (63).to_bytes(2, "little"); boot[26:28] = (255).to_bytes(2, "little"); boot[32:36] = TOTAL_SECTORS.to_bytes(4, "little")
    boot[36] = 0x80; boot[38] = 0x29; boot[39:43] = (0x43454C45).to_bytes(4, "little"); boot[43:54] = b"CELESTE USB"; boot[54:62] = b"FAT16   "; boot[510:512] = b"\x55\xAA"
    fat = bytearray(FAT_SECTORS * SECTOR); fat[:4] = b"\xF8\xFF\xFF\xFF"; root = (1 + FAT_COUNT * FAT_SECTORS) * SECTOR; cluster = 2
    for index, (name, data) in enumerate(files.items()):
        image[root + index * 32:root + (index + 1) * 32] = root_entry(name, cluster, len(data)); cluster = write_file(image, fat, cluster, data)
    for copy in range(FAT_COUNT): image[SECTOR + copy * len(fat):SECTOR + (copy + 1) * len(fat)] = fat
    return bytes(image)

def main() -> None:
    root = Path(__file__).resolve().parents[1]; parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--portable-dir", type=Path, default=root / "dist" / "portable"); parser.add_argument("--output", type=Path, default=root / "dist" / "celeste_portable.img")
    args = parser.parse_args()
    missing = [source for source in LAUNCHERS.values() if not (args.portable_dir / source).is_file()]
    if missing:
        parser.error("missing portable launchers: " + ", ".join(missing))
    files = {name: (args.portable_dir / source).read_bytes() for name, source in LAUNCHERS.items()}; files[b"README  TXT"] = README
    args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_bytes(build_image(files)); print(args.output)

if __name__ == "__main__": main()
