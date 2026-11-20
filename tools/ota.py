# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

"""OTA update of the sensor through one ESPHome Bluetooth proxy, in the Silicon Labs AppLoader.

Usage: ESPHOME_KEY=<proxy api key> .tmp/venv/bin/python -I tools/ota.py <proxy ip> <chip MAC> [image]
       ESPHOME_KEY=<proxy api key> .tmp/venv/bin/python -I tools/ota.py --versions <proxy ip> <chip MAC>
       python3 -I tools/ota.py --self-check

First hold PB0 for 3 s: the sensor reboots into the AppLoader (advertises with the same address).
[image] is a .gbl file or an addressed app image (.s37, .hex); default: the release -crc.s37.
An image is wrapped in a GBL with Simplicity Commander; a .bin has no address and is refused (COMMANDER from
tools/sdk-env.sh). Only plain app and bootloader GBLs are accepted: no SE, encrypted, compressed or delta content;
no program tag below 0x12000.

Sequence (SDK app/bluetooth/example_host/bt_host_ota_dfu/main.c): write 0x00 to OTA control
(OTA_BEGIN, :306, :415), write the GBL to OTA data in (MTU-3)-byte chunks (:216-222; always with
response: lossless on both proxies, README, Task 4), write 0x03 to OTA control (OTA_END, :287).
--versions only reads the OTA, AppLoader, bootloader and application versions (:889-890, :908-912,
:935-936, :452-455 and :956-960) and writes nothing.
Like tests/ota_speed.py it takes the proxy's advertisement slot from Home Assistant, so it
restarts the proxy at the end.
"""

import asyncio
import contextlib
import os
import struct
import subprocess  # nosec B404  # runs only Simplicity Commander
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RELEASE_IMAGE = ROOT / "build/release/build/release/mccgq02hl_bthome-crc.s37"
COMMANDER = os.environ.get("COMMANDER", str(ROOT / "sdk/home/.silabs/slt/installs/archive/commander/commander"))

OTA_CONTROL = "f7bf3564-fb6d-4e53-88a4-5e37e0326063"  # in_place_ota_dfu.xml
OTA_DATA = "984227f3-34fc-4045-a5d0-2c581f81a153"  # bt_host_ota_dfu/main.c:99
OTA_NAMES = (b"\x04\x09OTA", b"\x0a\x09MCCGQ-OTA")  # name AD (bootloader.slcp), then bootloaders <= 0x03010005
OTA_BEGIN, OTA_END = b"\x00", b"\x03"

# platform/bootloader/parser/gbl/btl_gbl_format.h:49-73
GBL_TAG_HEADER_V3 = 0x03A617EB
GBL_TAG_APPLICATION = 0xF40A0AF4
GBL_TAG_BOOTLOADER = 0xF50909F5
GBL_TAG_PROG = 0xFE0101FE  # payload starts with the target address (u32 LE)
GBL_TAG_ERASEPROG = 0xFD0303FD  # same layout
GBL_TAG_END = 0xFC0404FC
ALLOWED_TAGS = {GBL_TAG_HEADER_V3, GBL_TAG_APPLICATION, GBL_TAG_BOOTLOADER, GBL_TAG_END}
ALLOWED_TAGS |= {GBL_TAG_PROG, GBL_TAG_ERASEPROG}  # program tags: check_gbl also checks their address
# Version characteristics read by --versions: uuid -> name (bt_host_ota_dfu/main.c:100-103)
VERSION_CHARS = {
    "4cc07bcf-0868-4b32-9dad-ba4cc41e5316": "OTA version",
    "4f4a2368-8cca-451e-bfff-cf0e2ee23e9f": "AppLoader version",
    "25f05c0a-e917-46e9-b2a5-aa2be1245afe": "Bootloader version",
    "0d77cc11-4ac1-49f2-bfa9-cd96ac7a92f8": "Application version",
}
APP_BASE = 0x12000  # below it: AppLoader and bootloader


def gbl_tags(gbl: bytes, prog_addrs: list[int] | None = None) -> list[int]:
    """Tag IDs of a GBL: a sequence of (tag id u32 LE, length u32 LE, payload).
    Appends the address of each program tag to prog_addrs."""
    tags, i = [], 0
    while i < len(gbl):
        if i + 8 > len(gbl):
            raise ValueError("GBL: truncated tag header")
        tag, length = struct.unpack_from("<II", gbl, i)
        if tag in (GBL_TAG_PROG, GBL_TAG_ERASEPROG) and prog_addrs is not None:
            if length < 4:
                raise ValueError("GBL: program tag too short for its address")
            prog_addrs.append(struct.unpack_from("<I", gbl, i + 8)[0])
        i += 8 + length
        if i > len(gbl):
            raise ValueError("GBL: tag runs past the end of the file")
        tags.append(tag)
    return tags


def check_gbl(gbl: bytes) -> None:
    """Raise ValueError if the GBL is malformed or carries a tag outside ALLOWED_TAGS (not an assert: -O strips it)."""
    addrs: list[int] = []
    tags = gbl_tags(gbl, addrs)
    if not tags:
        raise ValueError("GBL is empty")
    if tags[0] != GBL_TAG_HEADER_V3:
        raise ValueError(f"GBL does not start with a v3 header: {tags[0]:#x}")
    if tags[-1] != GBL_TAG_END:
        raise ValueError("GBL does not end with the end tag")
    for a in addrs:
        if a < APP_BASE:
            raise ValueError(f"GBL program tag at {a:#x}, below the app base {APP_BASE:#x}: refused")
    for t in tags:
        if t not in ALLOWED_TAGS:
            raise ValueError(f"GBL tag {t:#010x} is not allowed (SE, encrypted, compressed, delta or other): refused")


def make_gbl(image: Path) -> bytes:
    if image.suffix == ".gbl":
        return image.read_bytes()
    if image.suffix not in (".s37", ".hex"):
        sys.exit(f"{image}: a .bin has no address info; use the -crc.s37 (or .hex, .gbl)")
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "app.gbl"
        subprocess.run(  # nosec B603  # argv list, no shell; COMMANDER comes from the operator environment
            [COMMANDER, "gbl", "create", str(out), "--app", str(image)],
            check=True,
            env={**os.environ, "HOME": str(ROOT / "sdk/home")},
        )
        return out.read_bytes()


def decode_version(name: str, raw: bytes) -> str:
    """Format one version value as main.c prints it; the SDK ignores a value of the wrong length."""
    if name == "OTA version" and len(raw) == 1:
        return f"OTA protocol version {raw[0]:#04x}"
    if name == "AppLoader version" and len(raw) == 8:
        return "AppLoader version " + ".".join(map(str, struct.unpack("<4H", raw)))
    if name in ("Bootloader version", "Application version") and len(raw) == 4:
        return f"{name} {struct.unpack('<I', raw)[0]:#010x}"
    raise ValueError(f"{name}: unexpected length {len(raw)}: {raw.hex()}")


def self_check() -> None:
    for name, raw, want in (
        ("OTA version", b"\x03", "OTA protocol version 0x03"),
        ("AppLoader version", struct.pack("<4H", 1, 2, 3, 4), "AppLoader version 1.2.3.4"),
        ("Bootloader version", bytes.fromhex("78563412"), "Bootloader version 0x12345678"),
        ("Application version", bytes(4), "Application version 0x00000000"),
    ):
        if decode_version(name, raw) != want:
            raise SystemExit(f"self-check failed: {name} {raw.hex()} decoded wrong")
    try:
        decode_version("OTA version", b"")
    except ValueError:
        pass
    else:
        raise SystemExit("self-check failed: a bad version length was accepted")

    def tag(t: int, payload: bytes = b"") -> bytes:
        return struct.pack("<II", t, len(payload)) + payload

    good = tag(GBL_TAG_HEADER_V3, bytes(8)) + tag(GBL_TAG_APPLICATION, bytes(28)) + tag(GBL_TAG_END, bytes(4))
    check_gbl(good)
    for bad in (
        tag(GBL_TAG_HEADER_V3) + tag(0x5EA617EB, bytes(4)) + tag(GBL_TAG_END),  # SE upgrade
        tag(GBL_TAG_HEADER_V3) + tag(GBL_TAG_PROG, bytes(8)) + tag(GBL_TAG_END),
        tag(GBL_TAG_HEADER_V3) + tag(GBL_TAG_PROG, bytes(2)) + tag(GBL_TAG_END),
        tag(GBL_TAG_HEADER_V3) + tag(0xF90707F9, bytes(4)) + tag(GBL_TAG_END),  # ENC_GBL_DATA
        tag(GBL_TAG_HEADER_V3) + tag(0xFD0505FD, bytes(4)) + tag(GBL_TAG_END),  # PROG_LZ4
        good[:-1],
        good[8:],
        b"",
        good[:-12],
        tag(GBL_TAG_HEADER_V3) + tag(GBL_TAG_ERASEPROG, bytes(8)) + tag(GBL_TAG_END),
    ):
        try:
            check_gbl(bad)
        except ValueError as e:
            print("refused as expected:", e)
        else:
            raise SystemExit("self-check failed: a bad GBL was accepted")
    if RELEASE_IMAGE.exists():
        addrs: list[int] = []
        gbl = make_gbl(RELEASE_IMAGE)
        check_gbl(gbl)
        print("release GBL tags:", " ".join(f"{t:#010x}" for t in gbl_tags(gbl, addrs)))
        print("release GBL program addresses:", " ".join(f"{a:#010x}" for a in addrs))
    else:
        print("release image: skipped (not built)")
    print("self-check OK")


async def session(host: str, mac: str, work) -> None:
    """Wait for the AppLoader advert, clear the proxy cache, connect, run work(client, address, chars, mtu),
    then disconnect and restart the proxy."""
    key = os.environ.get("ESPHOME_KEY")
    if not key:
        sys.exit("ESPHOME_KEY is not set (proxy API key)")
    from aioesphomeapi import APIClient, BluetoothProxyFeature, ButtonInfo

    client = APIClient(host, 6053, None, noise_psk=key)
    await client.connect(login=True)
    try:
        info = await client.device_info()
        if client.api_version is None:
            sys.exit("no API version after connect")
        flags = info.bluetooth_proxy_feature_flags_compat(client.api_version)
        address = int(mac.replace(":", ""), 16)
        seen = asyncio.get_running_loop().create_future()

        def on_adv(msg) -> None:
            for adv in msg.advertisements:
                # The app shares the MAC: only the AppLoader's advert carries its name.
                if adv.address == address and any(n in adv.data for n in OTA_NAMES) and not seen.done():
                    seen.set_result(adv.address_type)

        unsub = client.subscribe_bluetooth_le_raw_advertisements(on_adv)
        try:
            try:
                address_type = await asyncio.wait_for(seen, 120)
            except asyncio.TimeoutError:
                sys.exit("no AppLoader advert seen in 120 s: hold PB0 for 3 s first")
            state = {}

            def on_state(connected: bool, mtu: int, _err: int) -> None:
                if connected:  # a disconnect reports mtu 0
                    state["mtu"] = mtu

            # The proxy caches GATT services per address: the spike's stale services would hide the AppLoader's.
            if flags & BluetoothProxyFeature.CACHE_CLEARING:
                cleared = await client.bluetooth_device_clear_cache(address)
                if not cleared.success:
                    print(f"WARNING: proxy GATT cache clear failed (error {cleared.error}); continuing")
            await client.bluetooth_device_connect(
                address, on_state, timeout=20, feature_flags=flags, address_type=address_type
            )
            services = await client.bluetooth_gatt_get_services(address)
            chars = {c.uuid.lower(): c for s in services.services for c in s.characteristics}
            await work(client, address, chars, state["mtu"])
        finally:
            unsub()
            with contextlib.suppress(Exception):  # best-effort disconnect in cleanup
                await client.bluetooth_device_disconnect(address)
            entities, _ = await client.list_entities_services()
            restart = [e for e in entities if isinstance(e, ButtonInfo) and "restart" in e.object_id]
            if restart:
                client.button_command(restart[0].key)
                await asyncio.sleep(1)
            else:
                print("WARNING: no restart button; restart the proxy so HA gets advertisements again")
    finally:
        await client.disconnect()


async def ota(host: str, mac: str, gbl: bytes) -> None:
    async def work(client, address, chars, mtu) -> None:
        for uuid in (OTA_CONTROL, OTA_DATA):
            if uuid not in chars:
                sys.exit(
                    f"OTA characteristic {uuid} not found: the proxy may serve a stale GATT cache "
                    "for this address, or the device is not in the AppLoader"
                )
        control, data = chars[OTA_CONTROL], chars[OTA_DATA]
        chunk = mtu - 3
        print(f"connected, MTU {mtu}, sending {len(gbl)} B in {chunk} B chunks")
        await client.bluetooth_gatt_write(address, control.handle, OTA_BEGIN, response=True)
        for i in range(0, len(gbl), chunk):
            await client.bluetooth_gatt_write(address, data.handle, gbl[i : i + chunk], response=True)
            print(f"\r{min(i + chunk, len(gbl)) * 100 // len(gbl)} %", end="", flush=True)
        print()
        try:  # the AppLoader may reset before it acknowledges the end
            await client.bluetooth_gatt_write(address, control.handle, OTA_END, response=True)
            print("OTA sent; the sensor reboots into the new app")
        except Exception as e:
            print(f"end write failed ({e!r}): result unconfirmed, check the new version in the boot packet")

    await session(host, mac, work)


async def versions(host: str, mac: str) -> None:
    async def work(client, address, chars, mtu) -> None:
        for uuid, name in VERSION_CHARS.items():
            if uuid not in chars:
                print(f"{name}: not present")
                continue
            try:
                print(decode_version(name, bytes(await client.bluetooth_gatt_read(address, chars[uuid].handle))))
            except Exception as e:
                print(f"{name}: read failed ({e!r})")

    await session(host, mac, work)


if __name__ == "__main__":
    args = sys.argv[1:]
    if args == ["--self-check"]:
        self_check()
    elif len(args) == 3 and args[0] == "--versions":
        asyncio.run(versions(args[1], args[2]))
    elif len(args) in (2, 3):
        gbl = make_gbl(Path(args[2]) if len(args) == 3 else RELEASE_IMAGE)
        check_gbl(gbl)
        asyncio.run(ota(args[0], args[1], gbl))
    else:
        sys.exit(__doc__)
