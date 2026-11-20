# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

"""Decode the packet printed by test_bthome with bthome-ble, the parser Home Assistant uses.

Usage: python -I check_bthome.py <test_bthome output file>
(full packet, no voltage, no light, door only, boot packet, press, double press, triple press:
the last eight lines)
"""

import sys

from bthome_ble import BTHomeBluetoothDeviceData
from bleak.backends.device import BLEDevice
from habluetooth import BluetoothServiceInfoBleak

KEY = bytes.fromhex("00112233445566778899aabbccddeeff")  # same test key as test_bthome.c
MAC = "00:11:22:33:44:55"  # neutral test MAC, same as test_bthome.c
UUID = "0000fcd2-0000-1000-8000-00805f9b34fb"


def ads(adv):
    """Split an advert into (type, data) AD structures."""
    out, i = [], 0
    while i < len(adv):
        out.append((adv[i + 1], adv[i + 2 : i + 1 + adv[i]]))
        i += 1 + adv[i]
    return out


def info(adv, t=0.0):
    # The service data AD header (type, UUID) is not part of the service data; the local name
    # (type 0x09) is the device name.
    sd = next(d for ty, d in ads(adv) if ty == 0x16)
    name = next((d.decode() for ty, d in ads(adv) if ty == 0x09), "")
    return BluetoothServiceInfoBleak(
        name, MAC, -60, {}, {UUID: sd[2:]}, [UUID], "host", BLEDevice(MAC, name, None), None, False, t, None
    )


def decode(adv):
    parser = BTHomeBluetoothDeviceData(bindkey=KEY)
    update = parser.update(info(adv))
    assert parser.bindkey_verified, "bindkey not verified: MIC or nonce wrong"
    values: dict[str, object] = {k.key: v.native_value for k, v in update.entity_values.items()}
    binary = {k.key: v.native_value for k, v in update.binary_entity_values.items()}
    values["events"] = [e.event_type for e in update.events.values()]
    print("decoded:", values, binary)
    assert "battery" not in values, values  # voltage only, no percentage
    return values, binary, update


def check(adv, length, voltage, count):
    assert len(adv) == length and adv[0] == length - 1 and adv[1] == 0x16, adv.hex()
    values, binary, _ = decode(adv)
    assert binary.get("door") is True, binary
    # An object left out must not appear: HA keeps its last value for that key.
    assert values.get("voltage") == voltage, values
    # With light there is always a reset count (count_1), so the light code is count_2.
    assert values.get("count_2") == count and ("count_1" in values) == (count is not None), values
    assert values.get("count") is None, values
    # Packet id = counter low byte (0x12345678): bthome-ble drops repeats of an event packet.
    assert values.get("packet_id") == 0x78, values
    assert values["events"] == [], values


full, no_mv, no_light, door_only, boot, press, double, triple = (
    bytes.fromhex(line) for line in open(sys.argv[1]).read().split()[-8:]
)
check(full, 25, 2.78, 1234)
check(no_mv, 22, None, 1234)  # before the first loaded battery sample
check(no_light, 20, 2.78, None)  # before the first 10 s light sample
check(door_only, 17, None, None)  # neither sample yet
# Boot packet: door, raw cause 0x0008 (MSB first), version 0x010203 as sw_version 1.2.3; no
# voltage, light or reset count, no name.
values, binary, update = decode(boot)
assert len(boot) == 25 and binary.get("door") is True, binary
assert values.get("raw") == "0008" and values.get("packet_id") == 0x78, values
assert "voltage" not in values and not any(k.startswith("count") for k in values), values
assert update.devices[None].sw_version == "1.2.3", update.devices
# Press packet: the event, cause, version and the local name (HA names the device "MCCG 4455").
values, binary, update = decode(press)
assert len(press) == 31 and values["events"] == ["press"], values
assert values.get("raw") == "0008" and values.get("packet_id") == 0x78, values
assert "voltage" not in values and not any(k.startswith("count") for k in values), values
assert not binary, binary
assert update.devices[None].sw_version == "1.2.3", update.devices
assert update.devices[None].name == "MCCG 4455", update.devices
# Double and triple press differ from the press packet only in the 3A value.
for adv, event in ((double, "double_press"), (triple, "triple_press")):
    values, binary, _ = decode(adv)
    assert len(adv) == 31 and values["events"] == [event], values
    assert values.get("raw") == "0008" and values.get("packet_id") == 0x78 and not binary, values
# A burst repeats the event packet: one parser (one HA device) must fire the event once.
parser = BTHomeBluetoothDeviceData(bindkey=KEY)
# bthome-ble reuses its update object, so count the events right after each call.
fired = [len(parser.update(info(press, t)).events) for t in (0.0, 0.05)]
assert fired == [1, 0], fired
print("PASS check_bthome")
