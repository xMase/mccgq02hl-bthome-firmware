#!/usr/bin/env bash
# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# Download the Silicon Labs SDK and the build tools into sdk/, check each archive against its
# pinned SHA-256, unpack them and configure slc. Used by the release workflow; works on any
# x86_64 Linux with curl, unzip, tar (bzip2, xz) and python3; Simplicity Commander also needs
# libGL, GLib and D-Bus (Ubuntu: libgl1 libglib2.0-0t64 libdbus-1-3). Needs about 3.5 GB.
# Usage: tools/sdk-fetch.sh   (sdk/ must not exist yet; sdk/downloads/ is kept for a second run)
set -euo pipefail
cd "$(dirname "$0")/.."
S=$PWD/sdk
case "$(ls -A "$S" 2> /dev/null)" in
  "" | downloads) ;;
  *)
    echo "sdk/ already holds files; remove it (or all but sdk/downloads) first" >&2
    exit 1
    ;;
esac
SILABS=https://updates.silabs.com/studio/v6/updates/update_site/archives
ARM=https://armkeil.blob.core.windows.net/developer/Files/downloads/gnu/14.2.rel1/binrel
# sha256  url (one line per archive; the file name is the last part of the url)
ARCHIVES="
c7cf5c002e26f6561fbbd392b9c6e159c607f77d92cbb55abad0ff7b0ff4cbf8 https://github.com/SiliconLabs/simplicity_sdk/releases/download/v2025.6.3/simplicity-sdk.zip
4e7643d8ddfb4354b7b4232b6e2da76f8c839287bcef4289646e72cd16996087 $SILABS/slc-cli/6.0.25/slc-cli.linux.gtk.x86_64.zip
9ea81a91628a4f7a52bb89f16eace0f2979a3d158804a6d12d6277a4c0e4de1f $SILABS/python/3.10.4/python.3.10.4.gtk.linux.x86_64.zip
5382fa98bcc66fc3aef48792a3e84328eca16afa9bf97517527e92812516ee50 $SILABS/java21/21.0.6/linux.x86_64_21.0.6.zip
956ae8e9e1600e492e1c4e57d271941ebf83bde6bfe23530a4e09ea300fdea32 $SILABS/commander/1.25.0/Commander_linux_x86_64_1v25p0b1995.tar.bz
62a63b981fe391a9cbad7ef51b17e49aeaa3e7b0d029b36ca1e9c3b2a9b78823 $ARM/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz
"
DL=$S/downloads
mkdir -p "$DL"
while read -r sum url; do
  [ -n "$url" ] || continue
  f=$DL/${url##*/}
  if ! echo "$sum  $f" | sha256sum -c --quiet 2> /dev/null; then
    curl -fsSL --retry 3 -o "$f.part" "$url"
    echo "$sum  $f.part" | sha256sum -c --quiet
    mv "$f.part" "$f"
  fi
done <<< "$ARCHIVES"

# Exit 1 is a warning only: some SDK test file names are UTF-8 without the zip UTF-8 flag.
unzip -q "$DL/simplicity-sdk.zip" -d "$S/simplicity_sdk" || [ $? -eq 1 ]
unzip -q "$DL/slc-cli.linux.gtk.x86_64.zip" -d "$S"
# slc runs the SDK template generators with this Python (it has jinja2), passed as --tool-path.
unzip -q "$DL/python.3.10.4.gtk.linux.x86_64.zip" -d "$S"
unzip -q "$DL/linux.x86_64_21.0.6.zip" -d "$S" && mv "$S/linux.x86_64_21.0.6" "$S/java"
tar -xjf "$DL/Commander_linux_x86_64_1v25p0b1995.tar.bz" -C "$S"
mkdir "$S/gcc" && tar -xJf "$DL/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz" -C "$S/gcc" --strip-components=1

# shellcheck source=tools/sdk-env.sh
. tools/sdk-env.sh
HOME=$S/home slc configuration --sdk "$SDK" --gcc-toolchain "$GCC_DIR"
HOME=$S/home slc signature trust --sdk "$SDK"
echo "sdk/ ready: ./build.sh [release|bootloader]"
