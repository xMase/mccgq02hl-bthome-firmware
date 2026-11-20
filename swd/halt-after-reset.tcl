# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# Shared by the flash scripts: connect, then halt the CPU right after a reset pulse.
# The firmware sleeps and the sleep turns debug off, so the CPU must be halted while it is still
# awake after boot. A halted CPU does not sleep. Pad A (RESETn) is on debugprobe GP1.
source [find interface/cmsis-dap.cfg]
transport select swd
source [find target/silabs/xg21.cfg]
# The wires to the test pads are long: 1 MHz (the config default) gives SWD JUNK acks.
adapter speed 100
reset_config srst_only srst_nogate
catch init

proc halt_after_reset {} {
    for {set try 1} {$try <= 100} {incr try} {
        adapter assert srst
        sleep 10
        adapter deassert srst
        # The Secure Engine boots first: the CPU is reachable only ~12 ms after reset.
        sleep 50
        if {![catch {efm32.cpu arp_examine} err] && ![catch {halt 100} err]
            && [efm32.cpu curstate] eq "halted"} {
            echo "try $try: CPU halted"
            return 1
        }
        echo "try $try: no halt: $err"
    }
    return 0
}

proc reset_and_run {} {
    adapter assert srst
    sleep 10
    adapter deassert srst
}

# Restart the halted core from its reset vector with debug kept on (DEMCR.VC_CORERESET +
# AIRCR.SYSRESETREQ): 50 ms after a pin reset the app is already past main.
# Returns {restarted cleared}: halted on the reset vector; VC_CORERESET cleared again.
proc restart_to_reset_vector {} {
    # Set failed: VC_CORERESET is not set, so there is nothing to clear.
    if {[catch {mww 0xE000EDFC [expr {[read_memory 0xE000EDFC 32 1] | 0x1}]}]} {
        return {0 1}
    }
    catch {mww 0xE000ED0C 0x05FA0004}
    set ok [expr {![catch {wait_halt 2000}] && [efm32.cpu curstate] eq "halted"}]
    # A later reset must not stop the core: VC_CORERESET left set keeps the chip awake.
    set cleared [expr {![catch {mww 0xE000EDFC [expr {[read_memory 0xE000EDFC 32 1] & ~0x1}]}]}]
    return [list $ok $cleared]
}
