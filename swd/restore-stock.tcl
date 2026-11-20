# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# WRITES FLASH. Way back to stock: writes the whole 0x0-0x7FFFF of the verified stock dump (stock
# bootloader included, since the AppLoader bootloader replaced it) and verifies it.
# WARNING: it rewrites the bootloader. Check your backup first (for example with a checksum
# taken when you made it). The backup is the 512 kB file that swd/dump-flash.tcl writes.
# Run from the project folder:
#   openocd -c "set STOCK dumps/mccgq02hl-flash.bin" -f swd/restore-stock.tcl
if {![info exists STOCK] || ![file exists $STOCK] || [file size $STOCK] != 0x80000} {
    echo "RESULT: set STOCK to your 512 kB flash backup (openocd -c \"set STOCK <path>\" -f swd/restore-stock.tcl)"
    shutdown
}
source swd/halt-after-reset.tcl
if {![halt_after_reset]} {
    echo "RESULT: could not halt the CPU"
    shutdown
}
flash erase_address 0x0 0x80000
flash write_image $STOCK 0x0 bin
verify_image $STOCK 0x0 bin
echo "RESULT: stock 0x0-0x7FFFF written and verified"
reset_and_run
shutdown
