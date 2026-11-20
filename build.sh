#!/usr/bin/env bash
# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# Build the firmware: ./build.sh (debug), ./build.sh release, or ./build.sh bootloader.
# Output: build/{debug,release}/build/{debug,release}/mccgq02hl_bthome-crc.bin (flash this one),
#         mccgq02hl_bthome.gbl next to it (for tools/ota.py)
#         build/bootloader/bootloader-crc.bin, build/bootloader/bootloader.gbl (GBL with only the
#         bootloader, for tools/ota.py)
set -euo pipefail
cd "$(dirname "$0")"
. tools/sdk-env.sh
MODE=${1:-debug}
case $MODE in
  debug) FLAGS=() ;;
  # The generated Makefile adds C_FLAGS_RELEASE only for the release goal.
  release) FLAGS=(C_FLAGS_RELEASE=-DBUILD_RELEASE) ;;
  # Throwaway GATT write-speed spike (tests/ota-speed): output in build/ota-speed/build/debug/ota_speed.bin.
  ota-speed) FLAGS=() ;;
  bootloader) FLAGS=() ;;
  *)
    echo "usage: $0 [debug|release|ota-speed|bootloader]" >&2
    exit 2
    ;;
esac
PROJECT=mccgq02hl_bthome SLCP=firmware/mccgq02hl_bthome.slcp TARGET=$MODE
if [ "$MODE" = ota-speed ]; then PROJECT=ota_speed SLCP=tests/ota-speed/ota_speed.slcp TARGET=debug; fi
if [ "$MODE" = bootloader ]; then PROJECT=bootloader SLCP=bootloader/bootloader.slcp TARGET=release; fi
OUT=build/$MODE
# slc never rewrites config files that exist, so slcp configuration changes reach the build only
# from a clean generation.
rm -rf "$OUT"
# A failed step or check leaves no output that a flash or OTA tool could take (spec, "Bindkey").
trap 'rm -rf "$OUT"' ERR
# slc keeps its configuration (SDK path, trust, toolchain) under sdk/home.
if [ "$MODE" = bootloader ]; then
  # -cp copies the SDK sources into $OUT, so the wait-loop patch changes the copy, never sdk/.
  HOME=$PWD/sdk/home slc generate -p "$SLCP" -d "$OUT" \
    --with EFR32BG21A010F512IM32 -o makefile "${SLC_TOOLS[@]}" -cp > /dev/null
  patch -d "$OUT"/simplicity_sdk_* -p1 --fuzz=0 --no-backup-if-mismatch < bootloader/apploader-wait.patch
else
  HOME=$PWD/sdk/home slc generate -p "$SLCP" -d "$OUT" \
    --with EFR32BG21A010F512IM32 -o makefile "${SLC_TOOLS[@]}" > /dev/null
fi
make -C "$OUT" -f "$PROJECT.Makefile" -j8 "$TARGET" ARM_GCC_DIR_LINUX="$GCC_DIR" "${FLAGS[@]}"
case $MODE in
  ota-speed) ;;
  # The bootloader carries a CRC32 for the SE's check on a bootloader upgrade, as the SDK
  # post-build (bootloader_series_2.slpb) adds. The linker region (0x0-0x11FFF) bounds its size.
  bootloader)
    "$COMMANDER" convert "$OUT/build/release/bootloader.out" --crc -o "$OUT/bootloader-crc.bin"
    # --bootloader needs the addressed image (.s37): the GBL tag carries the base address.
    "$COMMANDER" convert "$OUT/build/release/bootloader.out" --crc -o "$OUT/bootloader-crc.s37"
    "$COMMANDER" gbl create "$OUT/bootloader.gbl" --bootloader "$OUT/bootloader-crc.s37"
    ;;
  # CRC32 in the App Properties (signatureType 2): the bootloader checks it at every boot, so a
  # damaged app sends the chip into the AppLoader instead of running. It is the second barrier:
  # the GBL parser writes the reset vector last, and the PC word 0xFFFFFFFF enters BADAPP.
  *)
    "$COMMANDER" convert "$OUT/build/$MODE/$PROJECT.out" --crc -o "$OUT/build/$MODE/$PROJECT-crc.bin"
    python3 -I tools/check-image.py "$OUT/build/$MODE/$PROJECT-crc.bin" "$OUT/autogen/linkerfile.ld"
    # The bindkey is provisioned into NVM3 over SWD: no build output may carry it (spec, "Bindkey").
    if [ -f firmware/secrets.h ]; then
      python3 -I tools/check-key-absent.py "$OUT/build/$MODE/$PROJECT.out" "$OUT/build/$MODE/$PROJECT-crc.bin"
    fi
    # The same image with addresses (S-record), the default input of tools/ota.py: written only
    # after both checks pass. A GBL made from the .bin would aim at 0x0.
    "$COMMANDER" convert "$OUT/build/$MODE/$PROJECT.out" --crc -o "$OUT/build/$MODE/$PROJECT-crc.s37"
    # The GBL that tools/ota.py would make from the .s37: OTA without Simplicity Commander.
    "$COMMANDER" gbl create "$OUT/build/$MODE/$PROJECT.gbl" --app "$OUT/build/$MODE/$PROJECT-crc.s37"
    ;;
esac
