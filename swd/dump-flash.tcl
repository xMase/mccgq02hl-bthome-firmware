# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# Read-only: halt the CPU right after a reset pulse, then dump the flash. Writes nothing to the chip.
# Run from the project folder: openocd -f swd/dump-flash.tcl   (dumps go to dumps/mccgq02hl-*.bin)
#
# The stock firmware sleeps and the sleep turns debug off, so the CPU must be halted while it is
# still awake after boot. A halted CPU does not sleep. Pad A (RESETn) is on debugprobe GP1.
source [find interface/cmsis-dap.cfg]
transport select swd
source [find target/silabs/xg21.cfg]
# The wires to the test pads are long: 1 MHz (the config default) gives SWD JUNK acks.
adapter speed 100
reset_config srst_only srst_nogate

# init fails when the chip sleeps; the adapter is up anyway, so the reset pulses below still work.
catch init

set halted 0
for {set try 1} {$try <= 100 && !$halted} {incr try} {
    adapter assert srst
    sleep 10
    adapter deassert srst
    # The Secure Engine boots first: the CPU is reachable only ~12 ms after reset (50 ms: 4 of 5 halts).
    sleep 50
    if {![catch {efm32.cpu arp_examine} err] && ![catch {halt 100} err]
        && [efm32.cpu curstate] eq "halted"} {
        set halted 1
        echo "try $try: CPU halted"
    } else {
        echo "try $try: no halt: $err"
    }
}
if {!$halted} {
    echo "RESULT: could not halt the CPU"
    shutdown
}

# Series 2 DEVINFO: MSIZE at 0x0FE0800C, bits 15:0 = flash size in kB.
set msize [read_memory 0x0FE0800C 32 1]
set flash_kb [expr {$msize & 0xFFFF}]
echo [format "MSIZE 0x%08x: flash %d kB, RAM %d kB" $msize $flash_kb [expr {($msize >> 16) & 0x7FF}]]

dump_image dumps/mccgq02hl-devinfo.bin 0x0FE08000 0x400
dump_image dumps/mccgq02hl-userdata.bin 0x0FE00000 0x400
dump_image dumps/mccgq02hl-flash.bin 0x00000000 [expr {$flash_kb * 1024}]
echo "RESULT: dump done"

catch silabs_s2_dci_read_se_status
# Let the stock firmware run again.
resume
shutdown
