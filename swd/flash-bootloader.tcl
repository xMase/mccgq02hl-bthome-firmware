# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# WRITES FLASH. Erases 0x0-0x11FFF, writes the AppLoader bootloader at 0x0, verifies it. Writes
# nothing else: not the app area, NVM3 (0x74000-0x7DFFF), the last page (0x7E000-0x7FFFF),
# USERDATA, the debug lock or the SE OTP.
# WARNING: a wrong flash can lock the chip for good. Before the first run, and after any change of
# the probe or the chip, check that the debug lock is off and device erase is enabled. The CPU is left halted: the old app area may hold code the new bootloader would
# start, so write the app next with swd/flash-app.tcl, which resets. To keep the app, reset by hand.
# Run from the project folder:
#   openocd -c "set BTL build/bootloader/bootloader-crc.bin" -f swd/flash-bootloader.tcl
if {![info exists BTL] || ![file exists $BTL]} {
    echo "RESULT: set BTL to an existing bootloader .bin linked at 0x0"
    shutdown
}
if {[file size $BTL] > 0x12000} {
    echo "RESULT: $BTL is larger than 0x12000 and would reach the app at 0x12000"
    shutdown
}
source swd/halt-after-reset.tcl
if {![halt_after_reset]} {
    echo "RESULT: could not halt the CPU"
    shutdown
}
flash erase_address 0x0 0x12000
flash write_image $BTL 0x0 bin
verify_image $BTL 0x0 bin
echo "RESULT: $BTL written and verified at 0x0; CPU halted, run swd/flash-app.tcl next"
shutdown
