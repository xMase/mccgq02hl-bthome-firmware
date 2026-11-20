# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# WRITES FLASH. Erases 0x12000-0x73FFF, writes an application image at 0x12000, verifies it, resets.
# Keeps NVM3 (0x74000-0x7DFFF): the encryption counter must never restart under the same bindkey.
# After restore-stock.tcl the stock NVM3 sits in that range: add -c "set ERASE_NVM3 1" (erases up to
# 0x7FFFF) and use a new bindkey (see README).
# WARNING: a wrong flash can lock the chip for good. Check that the debug lock is off and device
# erase is enabled before the first run. The bootloader 0x0-0x11FFF is
# never touched. Flash the -crc image: the bootloader checks its CRC32 at every boot.
# Run from the project folder:
#   openocd -c "set APP build/debug/build/debug/mccgq02hl_bthome-crc.bin" -f swd/flash-app.tcl
if {![info exists APP] || ![file exists $APP]} {
    echo "RESULT: set APP to an existing .bin linked at 0x12000"
    shutdown
}
if {[file size $APP] > 0x62000} {
    echo "RESULT: $APP is larger than 0x62000 and would reach NVM3 at 0x74000"
    shutdown
}
source swd/halt-after-reset.tcl
if {![halt_after_reset]} {
    echo "RESULT: could not halt the CPU"
    shutdown
}
flash erase_address 0x12000 [expr {[info exists ERASE_NVM3] ? 0x6e000 : 0x62000}]
flash write_image $APP 0x12000 bin
verify_image $APP 0x12000 bin
echo "RESULT: $APP written and verified at 0x12000"
reset_and_run
shutdown
