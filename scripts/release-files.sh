#!/usr/bin/env bash
# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# Build the debug, release and bootloader images and collect the ready-made files of a release,
# with SHA256SUMS, into <dir>. Run by the release workflow; run it locally on the same commit to
# compare the sums. With a tag (vMAJOR.MINOR.PATCH), it first checks that the tag matches
# SL_APPLICATION_VERSION in firmware/mccgq02hl_bthome.slcp.
# Usage: scripts/release-files.sh <dir> [tag]
set -euo pipefail
cd "$(dirname "$0")/.."
[ $# -ge 1 ] || {
  echo "usage: $0 <dir> [tag]" >&2
  exit 2
}
DIR=$1 TAG=${2:-}
if [ -n "$TAG" ]; then
  hex=$(grep -A1 'name: SL_APPLICATION_VERSION' firmware/mccgq02hl_bthome.slcp | grep -o '0x[0-9a-fA-F]\{8\}')
  version=v$(((hex >> 16) & 0xff)).$(((hex >> 8) & 0xff)).$((hex & 0xff))
  [ "$TAG" = "$version" ] || {
    echo "tag $TAG does not match SL_APPLICATION_VERSION $hex ($version)" >&2
    exit 1
  }
fi
mkdir -p "$DIR"
for mode in debug release bootloader; do ./build.sh "$mode"; done
APP=mccgq02hl_bthome
cp build/debug/build/debug/$APP-crc.bin "$DIR/$APP-debug-crc.bin"
cp build/debug/build/debug/$APP.out "$DIR/$APP-debug.out"
cp build/release/build/release/$APP-crc.bin "$DIR/$APP-release-crc.bin"
cp build/release/build/release/$APP.gbl "$DIR/$APP-release.gbl"
cp build/bootloader/bootloader-crc.bin build/bootloader/bootloader.gbl "$DIR/"
(cd "$DIR" && sha256sum -- *.bin *.out *.gbl > SHA256SUMS && cat SHA256SUMS)
