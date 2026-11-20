# Quality gate

Run `scripts/quality-check.sh [target...]` from the repo root. Targets: `lint format types secrets
sast vulns structure coverage contracts debt` (no target = all). The script changes no source file,
writes full logs to `.tmp/quality/<check>.log`, prints `ok` or `failed` per check and exits non-zero
on any failure. A plan or branch is done only when it runs green on the final code, or every failure
is recorded here as an approved decision.

Setup: pip tools install with `--require-hashes` from `tools/quality/requirements-dev.txt` into
`.tmp/quality/venv` (rebuilt when the file changes). The C checks run in the image built from
`tools/quality/Containerfile` (tag = hash of the file). Shell, secrets and vulnerability checks run
from container images pinned by tag and digest. The host tests and C coverage run in the distrobox
`efr32`, as the host tests always did; `./build.sh release` runs on the host, once per gate run. Runtime Python dependencies are hashed in `tools/requirements.txt` (from
`tools/requirements.in`); regenerate both lock files with `pip-compile --generate-hashes
--allow-unsafe` (pip-tools, in a throwaway venv under `.tmp/`). `grype` reads `tools/requirements.txt`.

Limits (ratchets) live in `tools/quality/limits.env`: Lizard CCN, function length, parameters,
nesting and duplication, file length per language, C line and branch coverage, Python coverage.
Lower a limit when the code improves. Never raise one without the owner's approval.

Config files: `pyproject.toml` (ruff, mypy, bandit, vulture, deptry), `.clang-format`,
`.clang-tidy`, `.gitleaks.toml`, `tools/quality/cppcheck-suppressions.txt` (each entry has a reason).
shfmt runs with `-i 2 -ci -sr`.

Approved tools (2026-11-19; do not substitute without the owner's approval):

| Category | Tool | Version | License | Source | Rejected alternative | Last release / last commit / active maintainers (2026-11-19) |
|---|---|---|---|---|---|---|
| Lint + format, Python | ruff (`check`, `format --check`) | 0.17.5 | MIT | pypi.org/project/ruff | pylint as linter: slower, separate formatter | 2026-11-12 / 2026-11-19 / 6 core |
| Typecheck, Python | mypy (`check_untyped_defs`) | 2.4.1 | MIT | pypi.org/project/mypy | pyright: downloads Node at run time | 2026-10-28 / 2026-11-18 / 5 core |
| Format + lint, C | clang-format, clang-tidy (`bugprone-*`, `misc-header-include-cycle`, `clang-analyzer-*`) | 22.1.8-4.fc44 | Apache-2.0 with LLVM exception | Fedora 44 build of llvm-project | ssciwr PyPI wheels: small packaging repos | 2026-06-16 (23.1.6 is newer) / 2026-11-19 / many |
| Typecheck, C | gcc `-Wall -Wextra -Werror` (host tests); `-Wall -Wextra` plus a fail on any `warning:` (`./build.sh release`) | toolchain | GPL-3.0+ | gcc.gnu.org | none: already in the build |  |
| Lint, shell | shellcheck | 0.11.0 | GPL-3.0 | github.com/koalaman/shellcheck | bashate: style only | 2025-08-04, newest release, over 12 months / 2026-11-09 / 1 owner + 2 regular |
| Format, shell | shfmt | 3.14.1 | BSD-3-Clause | github.com/mvdan/sh | beautysh: no diff-check mode | 2026-09-06 / 2026-11-18 / 1 |
| Secrets, history + working tree | gitleaks (`git` mode, and `dir` mode on a copy of the non-ignored files) | 8.30.1 | MIT | github.com/gitleaks/gitleaks | trufflehog: verifies against live services | 2026-03-21 / 2026-10-23 / about 5 |
| SAST, C | cppcheck | 2.22.0-1.fc44 | GPL-3.0 | github.com/cppcheck-opensource/cppcheck | flawfinder: pattern grep, many false positives | 2026-09-19 / 2026-11-18 / 3 core |
| SAST, Python | bandit | 1.9.4 | Apache-2.0 | pypi.org/project/bandit | semgrep: registry rules not OSI, metrics on by default | 2026-02-25 / 2026-11-09 / about 8 |
| Vulnerabilities High+ with a fix, Python | grype `--only-fixed --fail-on high` | 0.122.1 | Apache-2.0 | github.com/anchore/grype | pip-audit: no severity filter | 2026-11-18 / 2026-11-18 / 5 core |
| Duplication, complexity, length, parameters, nesting | lizard | 1.26.1 | MIT | github.com/terryyin/lizard | jscpd: duplication only, needs Node | 2026-11-16 / 2026-11-19 / 1 |
| Import cycles, Python | pylint (`cyclic-import` only) | 4.1.4 | GPL-2.0+ | pypi.org/project/pylint | import-linter: needs a package | 2026-11-14 / 2026-11-18 / several core |
| Dead code, Python | vulture (`min_confidence` 60) | 2.16 | MIT | pypi.org/project/vulture | ruff F401/F841: imports and locals only | 2026-03-25 / 2026-11-16 / 1 main + contributors |
| Dead code, C | cppcheck `unusedFunction` | 2.22.0-1.fc44 | GPL-3.0 | as above | as above | as above |
| Unused dependencies, Python | deptry | 0.25.1 | MIT | pypi.org/project/deptry | none | 2026-03-18 / 2026-11-09 / 1 |
| Coverage, C host tests | gcovr (gcc `--coverage`) | 8.8 | BSD-3-Clause | pypi.org/project/gcovr | lcov: no fail-under | 2026-10-27 / 2026-11-12 / 3 core |
| Coverage, Python | coverage.py | 7.17.3 | Apache-2.0 | pypi.org/project/coverage | pytest-cov: only a wrapper | 2026-11-12 / 2026-11-19 / 1 main + contributors |
| Lint, GitHub Actions | actionlint (image with shellcheck for the `run:` blocks) | 1.7.12, `rhysd/actionlint:1.7.12@sha256:b1934ee5f1c509618f2508e6eb47ee0d3520686341fec936f3b79331f9315667` | MIT | github.com/rhysd/actionlint | yamllint: YAML syntax only, no Actions schema | 2026-03-30 / 2026-04-19 / about 4, one main |
| Format check, YAML | yamlfmt `-lint` (defaults) | 0.21.0, `yamlfmt_0.21.0_Linux_x86_64.tar.gz` + sha256 from `checksums.txt` | Apache-2.0 | github.com/google/yamlfmt | yamllint: linter, not formatter | 2026-01-05 / 2026-10-13 / about 8 |
| SAST, GitHub Actions | zizmor `--offline` | 1.32.0, PyPI, hashed in `tools/quality/requirements-dev.txt` | MIT | github.com/zizmorcore/zizmor | poutine: telemetry on by default | 2026-11-02 / 2026-11-19 / about 3, one main |
| Own scripts | layer boundaries, unused files, flash-map contract, long files, `shortcut:` list | | | `scripts/quality-check.sh` | |  |

The three GitHub Actions tools (approved 2026-11-20) cover the release workflow
(`.github/workflows/`), which is release-path code: lint, format and SAST only. The gate downloads
yamlfmt into `.tmp/quality/bin/` and checks its pinned SHA-256. Not verified: that actionlint and
yamlfmt send no telemetry (their docs are silent). The release scripts `tools/sdk-fetch.sh` and
`scripts/release-files.sh` are shell, so shellcheck and shfmt cover them.

Exception (approved 2026-11-19): shellcheck 0.11.0 (2025-08-04) is older than the 12-month release
rule, but it is the newest release, the repository is active and no other tool lints shell for bugs.
Recheck at the next gate change.
Left out: Tcl (OpenOCD scripts need hardware).
Import cycles in C: `misc-header-include-cycle` runs inside the clang-tidy check, with the SDK
header cycles ignored (vendor code). Layer rule: `bthome`, `counter`, `diag`, `door` and `battery`
include only project headers, the C standard library and `psa/crypto.h`; only `hw.c` and `app.c`
include SDK headers. C vulnerabilities have no scanner: the SDK is Simplicity SDK 2025.6.3 with
mbedTLS 3.6.2 (`sdk/simplicity_sdk/util/third_party/mbedtls`). Check the Silicon Labs and mbedTLS
advisories by hand when the SDK changes.
Configuration: cppcheck (no `-D`) and clang-tidy analyse the debug configuration, which holds
every release line; release-only warnings fail `release-build`. cppcheck gets no SDK include paths
(cppcheck manual advice); clang-tidy sees the SDK. `tests/ota-speed/` is a throwaway spike, out of
the SAST scope.
Coverage scope: C coverage counts the five pure modules and `app.c`. `tests/host/test_app.c` runs
`app.c` unchanged on a fake SDK (`tests/host/sdk/`, `tests/host/fake_sdk.c`), one forked process per
power cycle, and fails when a packet carries a counter at or above the stored NVM3 mark or one
counter goes out with two payloads. Only `hw.c` needs the chip and is not in the denominator; Python
coverage counts `tools/` and `tests/host/` as run by `ota.py --self-check`, `check_bthome.py` and
`check-image.py` on the release image. bandit skips B101 (assert) only in `tests/`;
`tools/` uses explicit exits. The release check builds `build/release` again with `./build.sh release`
(deterministic output, gitignored).
