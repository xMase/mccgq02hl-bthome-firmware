# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

"""Measure GATT write-without-response speed through one ESPHome Bluetooth proxy.

Usage: ESPHOME_KEY=<api key> python -I tests/ota_speed.py <proxy ip> <chip MAC> <bytes>

Talks to the throwaway spike in tests/ota-speed/: writes <bytes> to `sink` in (MTU-3)-byte chunks
without response, then reads `count` (bytes the chip received).
It takes the proxy's advertisement slot from Home Assistant, so it restarts
the proxy at the end.
"""

import argparse
import asyncio
import contextlib
import os
import struct
import time

from aioesphomeapi import APIClient, ButtonInfo

SINK = "7a1c0001-5a3e-4f6b-9a52-0c1f6e0d0001"
COUNT = "7a1c0002-5a3e-4f6b-9a52-0c1f6e0d0001"
POLL_S = 0.5  # count poll interval
IDLE_S = 5  # stop when count has not changed for this long
OVERALL_S = 600


async def main(host: str, mac: str, total: int, response: bool) -> None:
    client = APIClient(host, 6053, None, noise_psk=os.environ["ESPHOME_KEY"])
    await client.connect(login=True)
    try:
        info = await client.device_info()
        assert client.api_version is not None, "no API version after connect"
        flags = info.bluetooth_proxy_feature_flags_compat(client.api_version)
        address = int(mac.replace(":", ""), 16)
        seen = asyncio.get_running_loop().create_future()

        def on_adv(msg) -> None:
            for adv in msg.advertisements:
                if adv.address == address and not seen.done():
                    seen.set_result(adv.address_type)

        unsub = client.subscribe_bluetooth_le_raw_advertisements(on_adv)
        try:
            address_type = await asyncio.wait_for(seen, 120)
            state = {}

            def on_state(connected: bool, mtu: int, _err: int) -> None:
                if connected:  # a disconnect reports mtu 0
                    state["mtu"] = mtu

            await client.bluetooth_device_connect(
                address, on_state, timeout=20, feature_flags=flags, address_type=address_type
            )
            services = await client.bluetooth_gatt_get_services(address)
            chars = {c.uuid.lower(): c for s in services.services for c in s.characteristics}
            sink, count = chars[SINK], chars[COUNT]
            mtu = state["mtu"]
            chunk = mtu - 3
            mode = "with response" if response else "without response"
            print(f"connected, MTU {mtu}, writing {total} B {mode} in {chunk} B chunks")

            async def read_count() -> int:
                return struct.unpack("<I", await client.bluetooth_gatt_read(address, count.handle))[0]

            # Without response, bluetooth_gatt_write only queues the message and returns: the real
            # rate is the chip's count over time, not the time the loop takes.
            start = time.monotonic()
            sent = 0
            while sent < total:
                n = min(chunk, total - sent)
                await client.bluetooth_gatt_write(address, sink.handle, bytes(n), response=response)
                sent += n
            received, last_change = 0, time.monotonic()  # the idle wait starts after the writes
            while received < sent and time.monotonic() - last_change < IDLE_S:
                if time.monotonic() - start > OVERALL_S:
                    print("overall timeout")
                    break
                await asyncio.sleep(POLL_S)
                value = await read_count()
                if value != received:
                    received, last_change = value, time.monotonic()
            elapsed = last_change - start
            loss_pct = f"{100 * (sent - received) / sent:.1f} %" if sent else "n/a"
            rate = f"{received / elapsed:.0f} B/s" if elapsed else "n/a"
            print(f"mode {mode}: sent {sent} B, received {received} B, lost {sent - received} B ({loss_pct})")
            print(f"time to last count change {elapsed:.1f} s = {rate}")
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
                print("WARNING: no restart button; restart the device so HA gets advertisements again")
    finally:
        await client.disconnect()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("host", help="proxy IP")
    parser.add_argument("mac", help="chip MAC, for example 00:11:22:33:44:55")
    parser.add_argument("bytes", type=int, help="bytes to write")
    parser.add_argument("--response", action="store_true", help="write with response (reliable mode)")
    args = parser.parse_args()
    asyncio.run(main(args.host, args.mac, args.bytes, args.response))
