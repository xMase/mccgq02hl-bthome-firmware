Ready-made firmware files, built by the release workflow from this tag with the pinned SDK and
tools (`tools/sdk-fetch.sh`). Check them first: `sha256sum -c SHA256SUMS`.

| File | Use |
|------|-----|
| `bootloader-crc.bin` | first install: flash over SWD (`swd/flash-bootloader.tcl`) |
| `mccgq02hl_bthome-debug-crc.bin` | first install: flash over SWD (`swd/flash-app.tcl`) |
| `mccgq02hl_bthome-debug.out` | bindkey provisioning: `-c "set ELF <path>"` for `swd/provision-bindkey.tcl` |
| `mccgq02hl_bthome-release-crc.bin` | the release build over SWD, after the bindkey |
| `mccgq02hl_bthome-release.gbl` | OTA update of the app (`tools/ota.py`) |
| `bootloader.gbl` | OTA upgrade of the bootloader (`tools/ota.py`) |

Follow the README of this tag: back up the stock flash before any write.

License: the files contain this project's code (GPL-3.0-only with the SDK exception in
`LICENSE-EXCEPTION`; `bootloader/` under Zlib) and parts of the Silicon Labs Simplicity SDK,
which fall under the Silicon Labs Master Software License Agreement. You may use them only on
Silicon Labs devices. Not affiliated with Xiaomi or Silicon Labs; use at your own risk.
