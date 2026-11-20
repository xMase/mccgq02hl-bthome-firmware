# MCCGQ02HL BTHome firmware

Custom firmware for the Xiaomi Mi Door and Window Sensor 2 (MCCGQ02HL). It sends encrypted
BTHome v2 advertisements, so Home Assistant reads the sensor with its built-in BTHome integration
through any Bluetooth proxy. No Mi Home, no Xiaomi account, no Xiaomi key.

The door state goes out on every door change and again in a periodic repeat. The stock firmware
sends the door state only on events. With this firmware, one missed advertisement does not leave
Home Assistant wrong until the next event: the next idle packet corrects it.

You install it once over SWD (you solder wires to test pads). Later updates go over the air
through an ESPHome Bluetooth proxy, with no probe.

## Status and limits

Release 1.0.5 runs on a real unit and reports to Home Assistant.

What works:

- Encrypted BTHome v2 (AES-CCM). The bindkey lives in NVM3 on the chip, never in the image.
- Door state in every packet. A door change sends a 3 s burst (50 ms interval), then one packet
  every 10 s.
- Light every 60 s, as a raw ADC code (not lux). Home Assistant does the thresholds.
- Battery voltage measured under radio load: once 10 s after boot, then every hour.
- Button (PB0): one, two or three quick clicks send a `press`, `double_press` or `triple_press`
  event. Hold PB0 for 3 s to open the OTA bootloader. The LED blinks once at boot and on every
  door change.
- Diagnostics in Home Assistant: a count of anomalous resets (watchdog, lockup, fault) and the
  last reset cause. They survive a battery change.
- Replay protection: the encryption counter never goes back, also across battery changes and OTA.
- OTA updates of the app and of the bootloader through an ESPHome proxy.

Limits and risks:

- The firmware version and the device name update in Home Assistant only from a packet with a
  press event. After an OTA, press the button once. `tools/ota.py --versions` also reads the
  version from the bootloader.
- The press event, the reset cause and the version travel only in 3 s bursts (boot, press). If
  the proxies miss the whole burst, press the button again. Light and battery wait for the end of
  a burst, and the door state is not in a press packet.
- A packet stays on air at least 1 s before a packet of another kind replaces it. A door change
  right after a click can reach Home Assistant up to 1 s later.
- There is no OTA protection beyond the PB0 gesture. Anyone in radio range while the bootloader
  is open can upload a firmware. The sensor sends no BTHome packets while it is open. Since
  bootloader 0x03010006 the open session ends after 15 minutes.
- OTA is refused (five blinks) when the loaded battery is below 2.0 V. This threshold is
  provisional. A bootloader upgrade has no fallback if power fails: fit a fresh CR2032 first.
- A lost NVM3 (device erase, restore to stock) loses the bindkey and resets the counter. The
  sensor then needs a new key, entered again in Home Assistant.
- SWD does not answer while the release app sleeps (debug is off in sleep). It works in the
  bootloader. The probe scripts catch the chip awake by pulsing the reset pad.
- Not measured yet: average current against stock, bootloader current, and range at the door's
  usual place.
- Light sensor: in the dark the reading needs several seconds to settle after the sensor is
  powered, so a short settle gives values that cannot tell dark from small light. The firmware
  powers the sensor for 10 s every 60 s. Treat the light value as a coarse level.

## Hardware

- Chip: Silicon Labs EFR32BG21A010F512 (QFN32, Cortex-M33, 512 kB flash, 64 kB RAM).
  Marking `BG21 A010HN`, FCC ID 2ANJHMCCGQ02HL.
- FCC internal photos: [`docs/fcc-internal-photos.pdf`](docs/fcc-internal-photos.pdf). The images
  extracted from it and their crops are in [`docs/fcc-extract/`](docs/fcc-extract/)
  (`img-001.png` front, `img-002.png` back).
- Marked crops of the FCC photos in `docs/photos/`:
  [`back-pads-lettered.png`](docs/photos/back-pads-lettered.png) (test pads),
  [`chip-debug-pins.png`](docs/photos/chip-debug-pins.png) (debug pins),
  [`fcc-chip-marking.png`](docs/photos/fcc-chip-marking.png). The FCC sample is a 2021 board; a
  2022 board has the same pad and chip layout.
- Chip debug pins: SWCLK = PA01 (pin 18), SWDIO = PA02 (pin 19), RESETn = pin 9.

### Test pads and probe wiring

Pad letters are as in `docs/photos/back-pads-lettered.png`. Probe: a Raspberry Pi Pico (RP2040)
running [debugprobe](https://github.com/raspberrypi/debugprobe) (CMSIS-DAP), used with OpenOCD.

| Pad | Function | Connect to |
|-----|----------|------------|
| A | RESETn (sits under the button) | probe GP1 |
| D | GND (battery -) | probe GND |
| E | VCC (battery +) | not connected: the CR2032 powers the sensor |
| G | SWCLK | probe GP2 |
| H | SWDIO | probe GP3 |
| B, C, F | unknown | - |
| I, J | holes for the button legs | - |

Notes:

- Keep the battery fitted and in firm contact. A battery that shifts cuts power and gives failed
  halts.
- The RP2040 pulls GP1 low at reset. A wire from GP1 to pad A then holds the chip in reset, and
  the LED stops blinking at power-on. Leave pad A unconnected unless the probe drives it.
- The wires to the pads are long. Every OpenOCD script sets `adapter speed 100` (100 kHz). Faster
  clocks give `SWD ack not OK ... JUNK`. Keep that setting.
- OpenOCD needs the Silicon Labs Series 2 target (`target/silabs/xg21.cfg`). OpenOCD 0.12.0+dev
  (snapshot 2026-07-11) from Fedora has it. `swd/60-debugprobe.rules` is a udev rule for the probe
  (`2e8a:000c`), so OpenOCD runs without root.

## Ready-made files

Each GitHub release carries ready-made files, built by `.github/workflows/release.yml` from the
release tag with the pinned SDK and tools. With them you need no SDK. Check them first:
`sha256sum -c SHA256SUMS`.

| File | Build output it replaces |
|------|--------------------------|
| `bootloader-crc.bin` | `build/bootloader/bootloader-crc.bin` |
| `bootloader.gbl` | `build/bootloader/bootloader.gbl` |
| `mccgq02hl_bthome-debug-crc.bin` | `build/debug/build/debug/mccgq02hl_bthome-crc.bin` |
| `mccgq02hl_bthome-debug.out` | `build/debug/build/debug/mccgq02hl_bthome.out` |
| `mccgq02hl_bthome-release-crc.bin` | `build/release/build/release/mccgq02hl_bthome-crc.bin` |
| `mccgq02hl_bthome-release.gbl` | `build/release/build/release/mccgq02hl_bthome.gbl` |

In the commands below, pass the ready-made file where a command names the build output. To check
a release against its source, check out the tag and run `tools/sdk-fetch.sh`, then
`scripts/release-files.sh dist <tag>`, and compare `dist/SHA256SUMS` with the release one. Every
file except the debug `.out` (it holds the build paths) must match. In a test, a checkout at `/w` gave
other images: `slc` wrote absolute source paths there and linked the files in another order.

## Build

You need Simplicity SDK 2025.6.3, slc-cli 6.0.25, the Silicon Labs Python 3.10.4 and Java 21
packs, Arm GCC 14.2.rel1 and Simplicity Commander 1v25. All of them live under `sdk/`
(gitignored). `tools/sdk-fetch.sh` downloads them (about 1.1 GB, 3.5 GB unpacked), checks each
archive against a pinned SHA-256, unpacks them and configures `slc`, on any x86_64 Linux.
Simplicity Commander needs libGL, GLib and D-Bus (on Ubuntu: `libgl1 libglib2.0-0t64 libdbus-1-3`).
A manual setup with Silicon Labs `slt` also works: see the comments in `tools/sdk-env.sh`.

```bash
tools/sdk-fetch.sh
```

`slc` keeps its configuration in `sdk/home` (the scripts set `HOME=$PWD/sdk/home`), but it writes
the SDK trust into your real home (`~/SimplicityStudio`, `~/.config/SimplicityStudio`).

```bash
./build.sh            # debug build
./build.sh release    # release build: sleeps in EM2 with debug off
./build.sh bootloader # AppLoader bootloader
```

Outputs, all under `build/` (gitignored):

| Build | File | Use |
|-------|------|-----|
| release | `build/release/build/release/mccgq02hl_bthome-crc.bin` | flash over SWD |
| release | `build/release/build/release/mccgq02hl_bthome.gbl` | OTA input |
| debug | `build/debug/build/debug/mccgq02hl_bthome-crc.bin` and `.out` | flash and provisioning over SWD |
| bootloader | `build/bootloader/bootloader-crc.bin` | flash over SWD |
| bootloader | `build/bootloader/bootloader.gbl` | OTA bootloader upgrade |

Each build adds the app CRC32 with Simplicity Commander and runs `tools/check-image.py`. It checks
the image against the bootloader's app check (link address, stack pointer, reset vector, App
Properties, CRC32, end below NVM3). A failed step removes `build/<mode>`, so no image of a failed
build stays on disk. If `firmware/secrets.h` exists, `tools/check-key-absent.py` also checks that
the bindkey is not in the image.

## First install over SWD

Every script runs from the project folder, for example in a distrobox with OpenOCD:
`distrobox enter efr32 -- openocd -f swd/<script>.tcl`. Run it directly if OpenOCD is on your host.

### 1. Back up the stock flash

> **Warning.** Do this before any write. The backup is your only way back to the stock firmware.
> It holds Xiaomi firmware and per-device secrets (the BLE address and keys). Never publish,
> share or commit it. Never overwrite it. `dumps/` is gitignored for this reason.

`swd/dump-flash.tcl` only reads. It pulses RESETn, halts the CPU right after boot (a halted CPU
stays awake) and writes `dumps/mccgq02hl-*.bin`: the 512 kB main flash, USERDATA and DEVINFO.
It can need several tries (usually 5 to 10 pulses).

```bash
distrobox enter efr32 -- openocd -f swd/dump-flash.tcl
```

Dump twice and compare SHA-256 sums. Keep the files, and write the sums in a `SHA256SUMS` file
next to them.

### 2. Bootloader and app

> **Warning.** These steps erase and write flash. A failed write can leave the sensor with no
> working firmware. Only the stock backup brings it back. Do step 1 first.
>
> **Warning.** The scripts never touch the debug lock or the Secure Engine OTP. Do not write
> either of them: both are irreversible. Do not run `silabs_s2_dci_device_erase` or any other
> device erase either: it wipes the whole chip.
>
> **Warning.** The first bootloader write replaces the stock bootloader at 0x0-0x11FFF. After it,
> the way back to stock needs your backup (section "Way back to stock").

First check that the debug lock is off. `swd/read-lock-status.tcl` only reads:

```bash
distrobox enter efr32 -- openocd -f swd/read-lock-status.tcl
```

Then write the bootloader. The script erases 0x0-0x11FFF, writes the image and verifies it. It
leaves the CPU halted.

```bash
./build.sh bootloader
distrobox enter efr32 -- openocd -c "set BTL build/bootloader/bootloader-crc.bin" -f swd/flash-bootloader.tcl
```

Then write the app. The script erases 0x12000-0x73FFF, writes the image, verifies it and resets.
It keeps NVM3 (0x74000-0x7DFFF), so the encryption counter does not restart. Flash the `-crc`
image: the bootloader checks its CRC32 at every boot. For the first install use the debug build:
the next step needs it.

```bash
./build.sh
distrobox enter efr32 -- openocd -c "set APP build/debug/build/debug/mccgq02hl_bthome-crc.bin" -f swd/flash-app.tcl
```

After a restore to stock, NVM3 still holds the stock data. Add `-c "set ERASE_NVM3 1"` to the
command to erase up to 0x7FFFF, and use a new bindkey.

### 3. Bindkey

> **Warning.** `firmware/secrets.h` holds the key. It is gitignored. Never commit or share it.
> The key on the chip must match the key in Home Assistant. If NVM3 is erased, the key is gone:
> make a new one, do not reuse the old key.

Generate a key. The script writes `firmware/secrets.h`, and refuses to overwrite an existing file
or to write into a path that git does not ignore.

```bash
tools/gen-bindkey.sh
```

Write the key to the chip. `swd/provision-bindkey.tcl` puts the 16 bytes into a RAM hand-off in
the running app. The app stores them in NVM3 at boot and wipes the hand-off. The script does not
print the key and does not write main flash. Use the debug build on the chip: the release build
sleeps with debug off, so the script cannot verify that the app took the key. Every run adds 1 to
the reset count in Home Assistant, because it restarts the chip.

```bash
distrobox enter efr32 -- openocd -f swd/provision-bindkey.tcl
```

The script reads the addresses of `main` and the hand-off from the debug `.out` with the SDK `nm`,
or with `arm-none-eabi-nm` or `nm` from your PATH. With the ready-made files, pass the `.out`:
`-c "set ELF <path>/mccgq02hl_bthome-debug.out"` before `-f`.

Now install the release build with the same `flash-app.tcl` command, using the release `-crc.bin`.
NVM3 is kept, so the key stays.

Enter the key in Home Assistant:

1. Make sure a Bluetooth adapter or an ESPHome Bluetooth proxy is in range of the sensor.
2. The BTHome integration discovers the sensor. Open the discovered flow in Settings > Devices &
   services, or add it by hand: Add Integration > BTHome, then select the sensor.
3. In the Bindkey field, paste the 32 hex characters of the key. They are the 16 bytes in
   `firmware/secrets.h` (`BTHOME_BINDKEY`), written without `0x`, commas or spaces. Home
   Assistant checks the key against a received packet and refuses a wrong one.

## OTA updates through an ESPHome Bluetooth proxy

After the first install you do not need the probe. `tools/ota.py` uploads a new app or a new
bootloader through one ESPHome Bluetooth proxy.

Set up the Python environment once (from the hashed lock file):

```bash
python3 -m venv venv && venv/bin/pip install --require-hashes -r tools/requirements.txt
```

> **Warning.** While the bootloader is open, anyone in radio range can upload a firmware. The
> sensor sends no BTHome packets in that time. Do it with the sensor at hand.
>
> **Warning.** A bootloader upgrade has no fallback if power fails. Fit a fresh CR2032 first.
> The script also restarts the proxy at the end, so other devices on that proxy lose their
> Bluetooth for a moment.

1. Enter the bootloader: hold PB0 (the button) for 3 s. The LED blinks three times, then once
   every 2 s. Five blinks mean the battery is below 2.0 V and OTA is refused.
2. Run the upload. `<proxy-ip>` is the address of the proxy, `<sensor-mac>` the sensor's
   Bluetooth address, `ESPHOME_KEY` the proxy's API encryption key.

```bash
ESPHOME_KEY=<proxy-api-key> venv/bin/python -I tools/ota.py <proxy-ip> <sensor-mac> [image]
```

`image` defaults to the release `-crc.s37`. A `.gbl`, `.s37` or `.hex` file is accepted. The tool
wraps an addressed image in a GBL file with Simplicity Commander. It accepts only plain app and
bootloader GBL files: no Secure Engine, encrypted, compressed or delta content, and no program
tag below 0x12000.

Other commands:

```bash
# read the OTA, bootloader and app versions from the open bootloader; writes nothing
ESPHOME_KEY=<proxy-api-key> venv/bin/python -I tools/ota.py --versions <proxy-ip> <sensor-mac>
# check the GBL parser
python3 -I tools/ota.py --self-check
```

To upgrade the bootloader: run `./build.sh bootloader`, upload `build/bootloader/bootloader.gbl`,
then upload the app again. The bootloader installs the new version only if its version is higher,
and the staged copy overwrites the app.

Limits of the OTA session:

- Writes go with response, so they are lossless. A release image of about 118 kB takes about
  35 s, with the connection setup. Writes without response lose chunks through a proxy: do not
  use them.
- The bootloader closes an idle connection after 60 s. Since bootloader 0x03010006 it closes any
  connection 15 min after entry, also during an upload.
- A broken or half-written app does not start. The bootloader stays open: run the upload again.
  An app that faults at boot is replaced the same way: hold PB0 while you insert the battery.
- The app in flash needs a valid CRC32. Always upload a `-crc` image.
- After an OTA, Home Assistant can take a few minutes to show packets again, because the proxy
  restarts. Wait, or press the button once. A button press also updates the version in Home
  Assistant.

## Way back to stock

> **Warning.** This rewrites the bootloader and the whole flash 0x0-0x7FFFF, NVM3 included. The
> bindkey and the counter are lost. You need your own backup from the first install. Check it
> first: `sha256sum -c SHA256SUMS`, run in the folder that holds your backup.

Pass the path of your own `mccgq02hl-flash.bin` (512 kB) to the script. It stops with a usage
message if the path is unset, missing or not 512 kB.

```bash
distrobox enter efr32 -- openocd -c "set STOCK dumps/<your-backup>/mccgq02hl-flash.bin" -f swd/restore-stock.tcl
```

The script erases and writes 0x0-0x7FFFF, verifies it and resets. A restore of a backup was tested
on a real unit: the stock sensor came back with its own address.

## Flash map

| Range | Content |
|-------|---------|
| 0x00000-0x11FFF | AppLoader bootloader (`bootloader/`) |
| 0x12000-0x73FFF | application, with a CRC32 in its App Properties |
| 0x74000-0x7DFFF | NVM3: encryption counter mark, diagnostics, bindkey |
| 0x7E000-0x7FFFF | last page, left empty |

On one line: bootloader 0x0-0x11FFF, `app 0x12000-0x73FFF, NVM3 0x74000-0x7DFFF`, last page
0x7E000-0x7FFFF empty.

A bootloader upgrade is staged at 0x18000, inside the app area. That is why the app must be
uploaded again after it. A half-written app does not run: the bootloader enters its recovery mode
when the reset vector is erased, and the CRC32 is a second check.

## Development

Host unit tests run `app.c` unchanged on a fake SDK. They need gcc, make and `mbedtls-devel` 3.6:

```bash
distrobox enter efr32 -- make -C tests/host test
```

The quality gate runs lint, format, type, secrets, SAST, vulnerability, structure, coverage and
contract checks. It changes no source file, writes full logs to a log folder and exits non-zero on
any failure:

```bash
scripts/quality-check.sh [target...]
```

Targets: `lint format types secrets sast vulns structure coverage contracts debt`. No target runs
all. Tools, versions and limits are in [`docs/quality-gate.md`](docs/quality-gate.md).

A release is a pushed tag `vMAJOR.MINOR.PATCH` equal to `SL_APPLICATION_VERSION` in
`firmware/mccgq02hl_bthome.slcp`. The workflow `.github/workflows/release.yml` then runs
`tools/sdk-fetch.sh` and `scripts/release-files.sh`, and publishes the files with the notes in
`docs/release-notes.md`.

## License

- Own code: GPL-3.0-only, with an additional permission under GPLv3 section 7 for linking with
  the Silicon Labs Simplicity SDK. See [`LICENSE`](LICENSE) and
  [`LICENSE-EXCEPTION`](LICENSE-EXCEPTION).
- `bootloader/`: Zlib, see [`bootloader/LICENSE`](bootloader/LICENSE). The build merges these
  files into Silicon Labs bootloader sources.
- Built firmware files contain parts of the Silicon Labs SDK. Those parts fall under the Silicon
  Labs Master Software License Agreement. You may use them only on Silicon Labs devices.

This project is not affiliated with Xiaomi or Silicon Labs. Use it at your own risk: flashing can
make a sensor unusable.
