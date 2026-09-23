#!/usr/bin/env python3
import hashlib
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.make_portable_disk import DATA_START, FAT_SECTORS, README, SECTOR, build_image

def fat_entry(image, cluster):
    fat = image[SECTOR:SECTOR * (1 + FAT_SECTORS)]
    return int.from_bytes(fat[cluster * 2:cluster * 2 + 2], "little")

def extract(image, cluster, size):
    output = bytearray()
    while cluster < 0xFFF8:
        start = (DATA_START + cluster - 2) * SECTOR
        output.extend(image[start:start + SECTOR]); cluster = fat_entry(image, cluster)
    return bytes(output[:size])

class PortableDiskTests(unittest.TestCase):
    def test_fat16_and_exact_files(self):
        files = {b"MACARM64   ": b"arm", b"WINDOWS EXE": b"win" * 300, b"README  TXT": README}
        image = build_image(files)
        self.assertEqual(len(image), 33_554_432); self.assertEqual(image[510:512], b"\x55\xAA"); self.assertEqual(image[54:62], b"FAT16   ")
        root = (1 + 2 * FAT_SECTORS) * SECTOR
        for index, (name, original) in enumerate(files.items()):
            entry = root + index * 32; self.assertEqual(image[entry:entry + 11], name)
            cluster = int.from_bytes(image[entry + 26:entry + 28], "little"); size = int.from_bytes(image[entry + 28:entry + 32], "little")
            self.assertEqual(hashlib.sha256(extract(image, cluster, size)).digest(), hashlib.sha256(original).digest())

if __name__ == "__main__": unittest.main()
