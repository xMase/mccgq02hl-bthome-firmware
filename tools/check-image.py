# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

"""Check a firmware .bin against the AppLoader bootloader's app check (bootload_verifyApplication).

The app start comes from the build's linker script and must be 0x12000 (after the bootloader).
Usage: python3 -I tools/check-image.py <image-crc.bin> <linkerfile.ld>
"""

import re
import struct
import sys
import zlib

BASE = 0x12000  # first byte after the bootloader (0x0-0x11FFF)
FLASH_END = 0x80000
NVM3_BASE = 0x74000  # NVM3 lives above the image; the image must not reach it
SRAM = (0x20000000, 0x20010000)
MAGIC = bytes.fromhex("13b779fac925ddb7adf3cfe0f1b614b8")
SIGNATURE_CRC32 = 2
CRC32_RESIDUE = 0xDEBB20E3  # BTL_CRC32_END, btl_crc32.h: CRC over the data plus its CRC


def need(ok: object, msg: str) -> None:
    """Exit with msg unless ok (not an assert: -O strips it)."""
    if not ok:
        sys.exit(f"check-image: {msg}")


img = open(sys.argv[1], "rb").read()
origin = re.search(r"FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*(0x[0-9a-fA-F]+)", open(sys.argv[2]).read())
need(origin and int(origin[1], 16) == BASE, f"app linked at {origin and origin[1]}, expected {BASE:#x}")
sp, reset = struct.unpack_from("<II", img, 0)
props = struct.unpack_from("<I", img, 13 * 4)[0]
need(SRAM[0] <= sp <= SRAM[1], f"initial SP {sp:#x} not in RAM")
need(BASE <= (reset & ~1) <= FLASH_END and reset & 1, f"reset vector {reset:#x} not a Thumb address in the app")
need(BASE + len(img) <= NVM3_BASE, f"image ends at {BASE + len(img):#x}, past NVM3 {NVM3_BASE:#x}")
off = props - BASE
need(64 <= off < len(img) - 24, f"App Properties pointer {props:#x} outside the image")
need(img[off : off + 16] == MAGIC, "App Properties magic missing")
# structVersion = minor << 8 | major (application_properties.h)
version, sig_type, sig_loc = struct.unpack_from("<III", img, off + 16)
need(version & 0xFF <= 1, f"App Properties struct version {version:#x}: major above 1")
need(sig_type == SIGNATURE_CRC32, f"signatureType {sig_type}, expected {SIGNATURE_CRC32} (CRC32)")
# The bootloader runs the CRC from the app start to signatureLocation + 4 (btl_bootload.c).
end = sig_loc + 4 - BASE
need(0 < end <= len(img), f"signatureLocation {sig_loc:#x} outside the image")
need(zlib.crc32(img[:end]) ^ 0xFFFFFFFF == CRC32_RESIDUE, "app CRC32 does not match")
print(
    f"image ok: {len(img)} bytes at {BASE:#x}-{BASE + len(img) - 1:#x}, SP {sp:#x}, reset {reset:#x}, "
    f"App Properties {props:#x} version {version:#x}, CRC32 at {sig_loc:#x}"
)
