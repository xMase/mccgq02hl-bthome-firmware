# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

"""Capture the sensor's advertisements through one ESPHome Bluetooth proxy and decode each new
payload with bthome-ble (the parser Home Assistant uses) and the bindkey from firmware/secrets.h.

Usage: ESPHOME_KEY=<proxy api key> .tmp/venv/bin/python -I tests/capture_decode.py <proxy ip> <sensor MAC> <minutes>

ESPHome allows one advertisement subscriber per proxy: this script takes the slot from Home
Assistant, so it restarts the proxy at the end to give it back.
"""

import asyncio
import os
import re
import sys
import time
from pathlib import Path

from aioesphomeapi import APIClient, ButtonInfo
from bthome_ble import BTHomeBluetoothDeviceData
from bleak.backends.device import BLEDevice
from habluetooth import BluetoothServiceInfoBleak

UUID = "0000fcd2-0000-1000-8000-00805f9b34fb"
SECRETS = Path(__file__).resolve().parent.parent / "firmware" / "secrets.h"


def bindkey() -> bytes:
    text = SECRETS.read_text()
    match = re.search(r"BTHOME_BINDKEY\s*\{([^}]*)\}", text)
    assert match, "BTHOME_BINDKEY not found in secrets.h"
    body = match.group(1)
    key = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", body))
    assert len(key) == 16, "bindkey in secrets.h is not 16 bytes"
    return key


def bthome_service_data(adv: bytes) -> bytes | None:
    """Return the BTHome service data (device info onwards) from raw AD bytes, or None."""
    i = 0
    while i + 1 < len(adv):
        length = adv[i]
        if length == 0 or i + 1 + length > len(adv):
            return None
        if adv[i + 1] == 0x16 and adv[i + 2 : i + 4] == b"\xd2\xfc":
            return adv[i + 4 : i + 1 + length]
        i += 1 + length
    return None


# Encrypted service data: device info, objects, counter (4, LE), MIC (4).
assert bthome_service_data(bytes([2, 1, 6, 6, 0x16, 0xD2, 0xFC, 0x41, 1, 2])) == bytes([0x41, 1, 2])
assert bthome_service_data(bytes([5, 0x16, 0xD2, 0xFC])) is None  # truncated field


def decode(parser: BTHomeBluetoothDeviceData, data: bytes, mac: str) -> dict:
    info = BluetoothServiceInfoBleak(
        "sensor", mac, -60, {}, {UUID: data}, [UUID], "proxy", BLEDevice(mac, "sensor", None), None, False, 0.0, None
    )
    update = parser.update(info)
    if not parser.bindkey_verified:
        return {"error": "MIC or nonce wrong"}
    values = {k.key: v.native_value for k, v in update.entity_values.items()}
    binary = {k.key: v.native_value for k, v in update.binary_entity_values.items()}
    events = [e.event_type for e in update.events.values()]
    device = update.devices.get(None)
    return {
        "door": binary.get("door"),
        "count_1": values.get("count_1"),
        "count_2": values.get("count_2"),
        "voltage": values.get("voltage"),
        "cause": values.get("raw"),
        "sw_version": device.sw_version if device else None,
        "events": events,
    }


async def main(host: str, mac: str, minutes: float) -> None:
    key = bindkey()
    client = APIClient(host, 6053, None, noise_psk=os.environ["ESPHOME_KEY"])
    await client.connect(login=True)
    start = time.monotonic()
    packets = 0
    last: bytes | None = None
    counters: list[int] = []
    errors = 0

    def on_adv(msg) -> None:
        nonlocal packets, last, errors
        for adv in msg.advertisements:
            addr = f"{adv.address:012X}"
            if ":".join(addr[i : i + 2] for i in range(0, 12, 2)) != mac:
                continue
            data = bthome_service_data(adv.data)
            if data is None:
                continue
            packets += 1
            if data == last:
                continue  # a repeat of the payload on air: same counter, same bytes
            last = data
            # A fresh parser per payload: bthome-ble drops counters it has already seen.
            result = decode(BTHomeBluetoothDeviceData(bindkey=key), data, mac)
            counter = int.from_bytes(data[-8:-4], "little")
            errors += "error" in result
            counters.append(counter)
            print(
                f"{time.monotonic() - start:7.1f} s  rssi {adv.rssi:4}  len {len(adv.data):2}  "
                f"counter {counter}  {result}",
                flush=True,
            )

    unsub = client.subscribe_bluetooth_le_raw_advertisements(on_adv)
    try:
        await asyncio.sleep(minutes * 60)
    finally:
        unsub()
        entities, _ = await client.list_entities_services()
        restart = [e for e in entities if isinstance(e, ButtonInfo) and "restart" in e.object_id]
        if restart:
            client.button_command(restart[0].key)
            await asyncio.sleep(1)
        else:
            print("WARNING: no restart button; restart the proxy so HA gets advertisements again")
        await client.disconnect()

    rising = all(b > a for a, b in zip(counters, counters[1:], strict=False))
    print(f"{packets} packets, {len(counters)} payloads, {errors} decode errors, counter strictly rising: {rising}")


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1], sys.argv[2].upper(), float(sys.argv[3])))
