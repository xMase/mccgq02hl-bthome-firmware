#!/usr/bin/env bash
# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# Quality gate: ./scripts/quality-check.sh [target...]
# Targets: lint format types secrets sast vulns structure coverage contracts debt (default: all).
# Changes no source file. Full logs go to .tmp/quality/<check>.log. Exit status is non-zero when
# any check fails. Tools and versions: see docs/quality-gate.md. Ratchet limits live in
# tools/quality/limits.env: lower a limit when the code improves, never raise one without the owner.
# shellcheck disable=SC2086  # the file lists in $PY, $SH and $C_ALL are split on purpose
# shellcheck disable=SC2329  # the c_* functions run through check()
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
ROOT=$PWD
Q=$ROOT/.tmp/quality
mkdir -p "$Q"
export PYTHONDONTWRITEBYTECODE=1
# shellcheck source=tools/quality/limits.env
. tools/quality/limits.env

GROUPS_ALL="lint format types secrets sast vulns structure coverage contracts debt"
TARGETS=${*:-$GROUPS_ALL}
for t in $TARGETS; do
  case " $GROUPS_ALL " in
    *" $t "*) ;;
    *)
      echo "usage: $0 [$GROUPS_ALL]" >&2
      exit 2
      ;;
  esac
done
want() {
  case " $TARGETS " in *" $1 "*) return 0 ;; esac
  return 1
}

# --- tool setup -------------------------------------------------------------------------------
PYBIN=$Q/venv/bin
SHELLCHECK_IMAGE=docker.io/koalaman/shellcheck:v0.11.0@sha256:61862eba1fcf09a484ebcc6feea46f1782532571a34ed51fedf90dd25f925a8d
SHFMT_IMAGE=docker.io/mvdan/shfmt:v3.14.1@sha256:8c06884a35683d8763fba6c0d9484d11a61a96da65f8d497d6f625825a6043f2
GITLEAKS_IMAGE=ghcr.io/gitleaks/gitleaks:v8.30.1@sha256:c00b6bd0aeb3071cbcb79009cb16a60dd9e0a7c60e2be9ab65d25e6bc8abbb7f
GRYPE_IMAGE=docker.io/anchore/grype:v0.122.1@sha256:661583704a253c0448ed63998b5ea2cd9d5a76ee71eedc4aed439b8ce3e93a1c
CIMAGE=localhost/mccgq02hl-quality:$(sha256sum tools/quality/Containerfile | cut -c1-12)
PODMAN=(podman run --rm --security-opt label=disable -v "$ROOT:/src" -w /src)

setup_venv() {
  local stamp
  stamp=$(sha256sum tools/quality/requirements-dev.txt | cut -d' ' -f1)
  [ -x "$PYBIN/python" ] && [ "$(cat "$Q/venv.stamp" 2> /dev/null)" = "$stamp" ] && return 0
  rm -rf "$Q/venv"
  python3 -m venv "$Q/venv" &&
    "$PYBIN/pip" install -q --require-hashes -r tools/quality/requirements-dev.txt &&
    echo "$stamp" > "$Q/venv.stamp"
}
setup_image() {
  podman image exists "$CIMAGE" || podman build -q -t "$CIMAGE" -f tools/quality/Containerfile tools/quality
}
REL_BIN=build/release/build/release/mccgq02hl_bthome-crc.bin
REL_BUILT= # never inherited: a stale value would skip the build
# One fresh release build per gate run, shared by clang-tidy, release-build and py-coverage.
setup_release_build() {
  [ -n "${REL_BUILT:-}" ] && return "$REL_BUILT"
  ./build.sh release > "$Q/release-build.out" 2>&1
  REL_BUILT=$?
  return "$REL_BUILT"
}

# Tracked and untracked-but-not-ignored files: new files are checked before they are committed.
files() { git ls-files -co --exclude-standard -- "$@"; }
PY=$(files '*.py')
SH=$(files '*.sh')
C_ALL=$(files '*.c' '*.h')
# The generated Silicon Labs files are not tracked; every tracked C file is project code.

FAILED=0
RESULTS=()
check() { # check <name> <function>
  local name=$1 log=$Q/$1.log
  shift
  if "$@" > "$log" 2>&1; then
    echo "ok      $name"
    RESULTS+=("ok $name")
  else
    echo "failed  $name  (log: .tmp/quality/$name.log)"
    RESULTS+=("failed $name")
    FAILED=1
  fi
}

# --- lint -------------------------------------------------------------------------------------
c_ruff() { "$PYBIN/ruff" check --no-cache $PY; }
c_shellcheck() { "${PODMAN[@]}" "$SHELLCHECK_IMAGE" -x -P /src $SH; }
c_clang_tidy() {
  setup_release_build || {
    cat "$Q/release-build.out"
    return 1
  }
  # shellcheck source=tools/sdk-env.sh
  . tools/sdk-env.sh
  local gcc=/src/${GCC_DIR#"$ROOT"/}
  # shellcheck disable=SC2016  # make expands the $(...) in this makefile text
  printf 'include mccgq02hl_bthome.project.mak\nprint:\n\t@echo $(C_DEFS) $(INCLUDES)\n' > "$Q/flags.mk"
  (cd build/release && make -s -f "$Q/flags.mk" SDK_PATH=/src/sdk/simplicity_sdk \
    PKG_PATH=/src/sdk/home/.silabs/slt/installs print) > "$Q/cflags.txt" || return 1
  # clang-tidy checks the build's include graph (misc-header-include-cycle) and the code (bugprone, analyzer).
  # shellcheck disable=SC2016  # the container shell expands $f, $GCC and $rc
  "${PODMAN[@]}" -e GCC="$gcc" -w /src/build/release "$CIMAGE" sh -c '
    rc=0
    for f in /src/firmware/*.c; do
      echo "== $f"
      clang-tidy "$f" -- --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -std=c18 \
        -isystem $GCC/arm-none-eabi/include -isystem $GCC/lib/gcc/arm-none-eabi/*/include \
        $(cat /src/.tmp/quality/cflags.txt) || rc=1
    done
    exit $rc'
}

# --- format -----------------------------------------------------------------------------------
c_ruff_format() { "$PYBIN/ruff" format --no-cache --check $PY; }
c_clang_format() { "${PODMAN[@]}" "$CIMAGE" clang-format --dry-run --Werror $C_ALL; }
c_shfmt() { "${PODMAN[@]}" "$SHFMT_IMAGE" -i 2 -ci -sr -d $SH; }

# --- types ------------------------------------------------------------------------------------
c_mypy() { "$PYBIN/mypy" $PY; }
# gcc -Wall -Wextra -Werror: the host tests (distrobox, as the project does) and the release firmware.
c_host_tests() {
  distrobox enter efr32 -- make -C tests/host clean test B=../../.tmp/quality/host-build PYTHON="$PYBIN/python"
}
# The SDK makefile has no -Werror: any compiler warning in the release build fails the check.
c_release_build() {
  setup_release_build
  local rc=$?
  cat "$Q/release-build.out"
  [ $rc = 0 ] && ! grep -q 'warning:' "$Q/release-build.out"
}

# --- secrets ----------------------------------------------------------------------------------
# Git mode scans the history; dir mode scans a copy of the tracked and untracked-but-not-ignored
# files, so ignored files (dumps/, firmware/secrets.h) are never read.
c_gitleaks() {
  local tree=$Q/gitleaks-tree rc=0
  rm -rf "$tree" && mkdir -p "$tree" || return 1
  git ls-files -z -co --exclude-standard | tar --null -T - --ignore-failed-read -cf - | tar -xf - -C "$tree" || return 1
  "${PODMAN[@]}" "$GITLEAKS_IMAGE" git --no-banner --redact -c .gitleaks.toml . || rc=1
  "${PODMAN[@]}" "$GITLEAKS_IMAGE" dir --no-banner --redact -c .gitleaks.toml .tmp/quality/gitleaks-tree || rc=1
  return $rc
}

# --- sast -------------------------------------------------------------------------------------
c_cppcheck() {
  "${PODMAN[@]}" "$CIMAGE" cppcheck --std=c11 --enable=warning,style,performance,portability --inline-suppr \
    --suppressions-list=tools/quality/cppcheck-suppressions.txt --error-exitcode=1 --check-level=exhaustive \
    -I firmware --template='{file}:{line}: {severity}: {id}: {message}' -q firmware tests/host
}
c_bandit() { "$PYBIN/bandit" -q -c pyproject.toml -r $PY; }

# --- vulns ------------------------------------------------------------------------------------
c_grype() {
  mkdir -p "$Q/grype-cache"
  podman run --rm --security-opt label=disable -e GRYPE_CHECK_FOR_APP_UPDATE=false -e GRYPE_DB_CACHE_DIR=/cache \
    -v "$Q/grype-cache:/cache" -v "$ROOT/tools/requirements.txt:/scan/requirements.txt:ro" -w /scan \
    "$GRYPE_IMAGE" dir:/scan --only-fixed --fail-on high
}

# --- structure --------------------------------------------------------------------------------
c_lizard() {
  local out rate
  out=$("$PYBIN/lizard" -C "$LIZARD_CCN_MAX" -L "$LIZARD_LENGTH_MAX" -a "$LIZARD_PARAMS_MAX" -ENS \
    -T max_nested_structures="$LIZARD_NESTING_MAX" -Eduplicate -w $PY $C_ALL)
  local rc=$?
  echo "$out"
  rate=$(sed -n 's/^Total duplicate rate: \([0-9.]*\)%.*/\1/p' <<< "$out")
  echo "duplicate rate ${rate}% (limit ${LIZARD_DUP_PCT_MAX}%)"
  [ -n "$rate" ] || rc=1
  awk -v r="$rate" -v m="$LIZARD_DUP_PCT_MAX" 'BEGIN { exit !(r > m) }' && rc=1
  return $rc
}
longest() { LC_ALL=C xargs wc -l | grep -v ' total$' | sort -rn | head -1; }
c_file_length() {
  local c py rc=0
  c=$(echo "$C_ALL" | longest)
  py=$(echo "$PY" | longest)
  echo "longest C file:      $c (limit $FILE_LINES_C_MAX)"
  echo "longest Python file: $py (limit $FILE_LINES_PY_MAX)"
  [ "$(echo "$c" | awk '{print $1}')" -le "$FILE_LINES_C_MAX" ] || rc=1
  [ "$(echo "$py" | awk '{print $1}')" -le "$FILE_LINES_PY_MAX" ] || rc=1
  return $rc
}
c_pylint_cycles() { PYLINTHOME="$Q/pylint" "$PYBIN/pylint" --disable=all --enable=cyclic-import $PY; }
# Layer boundary: the pure modules include only project headers and the C standard library or PSA.
c_layers() {
  local rc=0 f inc
  for f in firmware/{bthome,counter,diag,door,battery}.[ch]; do
    while read -r inc; do
      case $inc in
        \<std*.h\> | \<string.h\> | \"psa/crypto.h\") ;;
        \"*.h\") [ -f "firmware/$(tr -d '"' <<< "$inc")" ] || {
          echo "$f: unknown header $inc"
          rc=1
        } ;;
        *)
          echo "$f: layer violation: #include $inc"
          rc=1
          ;;
      esac
    done < <(sed -n 's/^#include[[:space:]]*\(.*\)$/\1/p' "$f")
  done
  return $rc
}
c_vulture() { "$PYBIN/vulture" $PY; }
c_cppcheck_unused() {
  "${PODMAN[@]}" "$CIMAGE" cppcheck --std=c11 --enable=unusedFunction --inline-suppr \
    --suppressions-list=tools/quality/cppcheck-suppressions.txt --error-exitcode=1 \
    -I firmware --template='{file}:{line}: {severity}: {id}: {message}' -q firmware tests/host
}
c_deptry() { "$PYBIN/deptry" .; }
c_unused_files() {
  local rc=0 f n
  local fw
  mapfile -t fw < <(grep '^firmware/' <<< "$C_ALL")
  for f in "${fw[@]}"; do
    grep -q "path: ${f#firmware/}\$" firmware/mccgq02hl_bthome.slcp || {
      echo "$f is not in the slcp"
      rc=1
    }
  done
  grep -q 'wildcard test_\*\.c' tests/host/Makefile || {
    echo "tests/host/Makefile does not glob test_*.c"
    rc=1
  }
  for f in tests/host/test_*.c; do
    n=${f##*/test_}
    [ -f "firmware/$n" ] || {
      echo "$f tests no firmware/$n"
      rc=1
    }
  done
  return $rc
}

# --- coverage ---------------------------------------------------------------------------------
COV=$Q/cov
c_c_coverage() {
  rm -rf "$COV"
  distrobox enter efr32 -- bash -c "
    set -e
    make -C tests/host test B=../../.tmp/quality/cov PYTHON='$PYBIN/python' \
      CFLAGS='-std=c11 -Wall -Wextra -Werror -O0 --coverage -I$ROOT/firmware'
    '$PYBIN/gcovr' --root '$ROOT' --object-directory '$COV' --filter '$ROOT/firmware/' \
      --txt --txt-metric line --fail-under-line '$COV_C_LINE_MIN' --fail-under-branch '$COV_C_BRANCH_MIN'
    '$PYBIN/gcovr' --root '$ROOT' --object-directory '$COV' --filter '$ROOT/firmware/' --txt --txt-metric branch"
}
c_py_coverage() {
  export COVERAGE_FILE=$Q/coverage.data
  "$PYBIN/coverage" erase
  "$PYBIN/coverage" run -a --source=tools,tests/host tools/ota.py --self-check &&
    "$PYBIN/coverage" run -a --source=tools,tests/host tests/host/check_bthome.py "$COV/test_bthome.out" &&
    setup_release_build &&
    "$PYBIN/coverage" run -a --source=tools,tests/host tools/check-image.py "$REL_BIN" build/release/autogen/linkerfile.ld &&
    "$PYBIN/coverage" report --fail-under="$COV_PY_MIN"
}

# --- contracts --------------------------------------------------------------------------------
# Flash map: app starts at 0x12000, NVM3 at 0x74000, flash ends at 0x80000. Every file that states a
# boundary or a size derived from them must agree.
c_flash_map() {
  local app=$((0x12000)) nvm3=$((0x74000)) end=$((0x80000)) rc=0
  expect() { # expect <file> <fixed string>
    grep -qF -- "$2" "$1" || {
      echo "$1: missing '$2'"
      rc=1
    }
  }
  local app_h nvm3_h size_h full_h
  app_h=$(printf '0x%x' $app)
  nvm3_h=$(printf '0x%x' $nvm3)
  size_h=$(printf '0x%x' $((nvm3 - app)))
  full_h=$(printf '0x%x' $((end - app)))
  expect tools/check-image.py "BASE = $app_h"
  expect tools/check-image.py "NVM3_BASE = $nvm3_h"
  expect tools/ota.py "APP_BASE = $app_h"
  expect bootloader/bootloader.slcp "value: \"$size_h\""
  expect swd/flash-app.tcl "> $size_h"
  expect swd/flash-app.tcl "erase_address $app_h"
  expect swd/flash-app.tcl "$full_h : $size_h"
  expect swd/flash-app.tcl "write_image \$APP $app_h bin"
  expect swd/flash-app.tcl "verify_image \$APP $app_h bin"
  expect swd/flash-bootloader.tcl "> $app_h"
  expect swd/flash-bootloader.tcl "erase_address 0x0 $app_h"
  expect README.md "app $app_h-0x73FFF, NVM3 $nvm3_h-0x7DFFF"
  return $rc
}

# --- debt -------------------------------------------------------------------------------------
c_shortcuts() {
  git grep -nI 'shortcut:' -- . ':!scripts/quality-check.sh' || true
}

# --- run --------------------------------------------------------------------------------------
NEED_VENV=0
NEED_IMAGE=0
for t in $TARGETS; do
  case $t in
    lint) NEED_VENV=1 NEED_IMAGE=1 ;;
    format | sast | structure) NEED_VENV=1 NEED_IMAGE=1 ;;
    types | coverage) NEED_VENV=1 ;;
  esac
done
[ $NEED_VENV = 1 ] && { setup_venv > "$Q/setup-venv.log" 2>&1 || {
  echo "failed  setup-venv (log: .tmp/quality/setup-venv.log)"
  exit 1
}; }
[ $NEED_IMAGE = 1 ] && { setup_image > "$Q/setup-image.log" 2>&1 || {
  echo "failed  setup-image (log: .tmp/quality/setup-image.log)"
  exit 1
}; }

want lint && {
  check ruff c_ruff
  check shellcheck c_shellcheck
  check clang-tidy c_clang_tidy
}
want format && {
  check ruff-format c_ruff_format
  check clang-format c_clang_format
  check shfmt c_shfmt
}
want types && {
  check mypy c_mypy
  check host-tests c_host_tests
  check release-build c_release_build
}
want secrets && check gitleaks c_gitleaks
want sast && {
  check cppcheck c_cppcheck
  check bandit c_bandit
}
want vulns && check grype c_grype
want structure && {
  check lizard c_lizard
  check file-length c_file_length
  check pylint-cycles c_pylint_cycles
  check layers c_layers
  check vulture c_vulture
  check cppcheck-unused c_cppcheck_unused
  check deptry c_deptry
  check unused-files c_unused_files
}
want coverage && {
  check c-coverage c_c_coverage
  check py-coverage c_py_coverage
}
want contracts && check flash-map c_flash_map
want debt && {
  check shortcuts c_shortcuts
  echo "        shortcut: lines are listed in .tmp/quality/shortcuts.log ($(wc -l < "$Q/shortcuts.log") found)"
}

if [ $FAILED = 0 ]; then echo "quality gate: all checks ok"; else echo "quality gate: FAILED"; fi
exit $FAILED
