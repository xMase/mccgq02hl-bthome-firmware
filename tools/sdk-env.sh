# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# shellcheck shell=bash
# shellcheck disable=SC2034  # SLC_TOOLS is read by the scripts that source this file
# Source this file: exports SDK, GCC_DIR and COMMANDER, sets SLC_TOOLS (extra slc generate
# arguments) and puts slc, Java 21 and ARM GCC on PATH.
# Two layouts of sdk/:
# - tools/sdk-fetch.sh (one command, used by the release workflow): sdk/{simplicity_sdk,gcc,java,
#   python,slc_cli,commander}, recognised by sdk/gcc.
# - slt (by hand): sdk/slt/slt from slt-cli-1.1.0-linux-x64.zip; with HOME=$PWD/sdk/home run
#   `slt install slc-cli` and `slt install gcc-arm-none-eabi`; unzip simplicity-sdk.zip v2025.6.3
#   (GitHub SiliconLabs/simplicity_sdk) into sdk/simplicity_sdk; unzip Simplicity Commander 1v25
#   into sdk/home/.silabs/slt/installs/archive; then `slc configuration --sdk $SDK
#   --gcc-toolchain $GCC_DIR` and `slc signature trust --sdk $SDK`, both with HOME=sdk/home.
S=$(cd "$(dirname "${BASH_SOURCE[0]}")/../sdk" && pwd)
export SDK=$S/simplicity_sdk
if [ -d "$S/gcc" ]; then
  export GCC_DIR=$S/gcc
  # slt registers its Python in sdk/home; here slc gets the Python adapter pack per call.
  SLC_TOOLS=(--tool-path="$S/python")
  export COMMANDER=$S/commander/commander
  export PATH=$S/java/jre/bin:$S/slc_cli:$GCC_DIR/bin:$PATH
else
  I=$S/home/.silabs/slt/installs
  SLC_TOOLS=()
  export GCC_DIR=$I/conan/p/gcc-a469753773d4f2/p
  export COMMANDER=$I/archive/commander/commander
  export PATH=$I/archive/java21-v21.0.6/linux.x86_64_21.0.6/jre/bin:$I/archive/slc-cli-v6.0.25/slc_cli:$GCC_DIR/bin:$PATH
fi
