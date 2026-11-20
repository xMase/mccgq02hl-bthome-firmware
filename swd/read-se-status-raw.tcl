# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# Read-only: print every word of the Secure Engine status reply (DCI command 0xFE010000).
# The stock proc silabs_s2_dci_read_se_status prints only the debug lock word; this also shows
# the secure boot word. Changes nothing on the chip. Run from the project folder: openocd -f swd/read-se-status-raw.tcl
source [find interface/cmsis-dap.cfg]
transport select swd
source [find target/silabs/xg21.cfg]
adapter speed 100
reset_config srst_only srst_nogate
catch init

proc read_se_status_raw {} {
    silabs_s2_dci_connect
    poll off
    silabs_s2_dci_write_cmd 8
    silabs_s2_dci_write_cmd 0xFE010000
    set recvlen [silabs_s2_dci_read_response]
    echo [format "response header 0x%08x" $recvlen]
    if {$recvlen & 0xFFFF0000} { poll on; error "command response not OK" }
    incr recvlen -4
    for {set i 0} {$recvlen > 0} {incr i} {
        set recvlen [expr {$recvlen - 4}]
        echo [format "word %d = 0x%08x" $i [silabs_s2_dci_read_response]]
    }
    poll on
}

# Same reset-and-retry loop as read-lock-status.tcl: the stock firmware sleeps within ms of boot.
set done 0
foreach delay_ms {1 5 20 50} {
    for {set try 1} {$try <= 3 && !$done} {incr try} {
        adapter assert srst
        sleep 10
        adapter deassert srst
        sleep $delay_ms
        if {[catch {read_se_status_raw} err]} {
            echo "delay $delay_ms ms try $try: failed: $err"
        } else {
            set done 1
        }
    }
    if {$done} break
}
shutdown
