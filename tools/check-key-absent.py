#!/usr/bin/env python3
# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

"""Count the bindkey bytes of firmware/secrets.h in each given file; prints counts only.

The key must not be in any build output (spec, "Bindkey"). Exit 1 if any count is not 0.
Searches the raw bytes only, so pass the raw images: the ".bin" and ".out" files. (".hex"/".s37"
split the bytes into records with address and checksum, a text search can miss a split key.)
Usage: tools/check-key-absent.py build/debug/**/*.bin build/debug/**/*.out ...
"""

import re
import sys
from pathlib import Path

secrets = Path(__file__).resolve().parent.parent / "firmware" / "secrets.h"
match = re.search(r"BTHOME_BINDKEY\s*\{([^}]*)\}", secrets.read_text())
if not match:
    sys.exit("BTHOME_BINDKEY not found in secrets.h")
body = match.group(1)
key = bytes(int(b, 16) for b in re.findall(r"0[xX]([0-9a-fA-F]{2})", body))
if len(key) != 16:
    sys.exit("bindkey in secrets.h is not 16 bytes")

bad = 0
for name in sys.argv[1:]:
    data = Path(name).read_bytes()
    count = data.count(key)
    bad += count
    print(f"{name}: raw={count}")
sys.exit(1 if bad else 0)
