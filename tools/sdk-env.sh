# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# shellcheck shell=bash
# Source this file: puts slc, Java 21 and ARM GCC (installed by slt under sdk/home) on PATH.
# Setup (once): sdk/slt/slt from slt-cli-1.1.0-linux-x64.zip; with HOME=$PWD/sdk/home run
# `slt install slc-cli` and `slt install gcc-arm-none-eabi`; unzip simplicity-sdk.zip v2025.6.3
# (GitHub SiliconLabs/simplicity_sdk) into sdk/simplicity_sdk; then `slc configuration --sdk
# $SDK --gcc-toolchain $GCC_DIR` and `slc signature trust --sdk $SDK`, both with HOME=sdk/home.
S=$(cd "$(dirname "${BASH_SOURCE[0]}")/../sdk" && pwd)
I=$S/home/.silabs/slt/installs
export SDK=$S/simplicity_sdk
export GCC_DIR=$I/conan/p/gcc-a469753773d4f2/p
# Simplicity Commander 1v25 (unzipped under sdk/home by hand): app/bootloader CRC32, GBL files.
export COMMANDER=$I/archive/commander/commander
export PATH=$I/archive/java21-v21.0.6/linux.x86_64_21.0.6/jre/bin:$I/archive/slc-cli-v6.0.25/slc_cli:$GCC_DIR/bin:$PATH
