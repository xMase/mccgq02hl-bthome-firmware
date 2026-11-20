# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# Read-only: ask the Secure Engine for the debug lock state over DCI. Changes nothing on the chip.
# Run from the project folder: openocd -f swd/read-lock-status.tcl
#
# The stock firmware puts the chip in deep sleep, which turns debug off, and debug does not answer
# while RESETn is held low. So pulse RESETn (pad A -> debugprobe GP1) and read right after boot,
# while the chip is still awake. Several delays, because the awake window is unknown.
source [find interface/cmsis-dap.cfg]
transport select swd
source [find target/silabs/xg21.cfg]
# The wires to the test pads are long: 1 MHz (the config default) gives SWD JUNK acks.
adapter speed 100
reset_config srst_only srst_nogate

# init fails when the chip sleeps; the adapter is up anyway, so the reset pulses below still work.
catch init

set done 0
foreach delay_ms {1 5 20 50 100 200} {
    for {set try 1} {$try <= 3 && !$done} {incr try} {
        adapter assert srst
        sleep 10
        adapter deassert srst
        sleep $delay_ms
        if {[catch {silabs_s2_dci_read_se_status} err]} {
            echo "delay $delay_ms ms try $try: failed: $err"
        } else {
            echo "delay $delay_ms ms try $try: OK"
            set done 1
        }
    }
    if {$done} break
}
shutdown
