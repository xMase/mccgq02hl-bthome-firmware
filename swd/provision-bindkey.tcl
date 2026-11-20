# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# CHANGES THE BINDKEY: puts the 16 bytes of firmware/secrets.h into the app's RAM hand-off
# (g_bindkey_handoff); the app stores them in NVM3 key 0x00003 at boot and wipes the hand-off.
# The key changes, so Home Assistant must get the same key. The key is
# read from secrets.h here and never printed. Main flash and NVM3 are not written by this script.
# The image on the chip must be the one built from ELF: the symbol addresses come from it.
# ELF defaults to the debug build; override with -c "set ELF <path>". The release build sleeps with
# debug off, so there the script cannot verify the hand-off.
# Every run adds 1 to the diagnostics reset count (count_1 in Home Assistant): the restart below is a
# SYSRESETREQ, a SYSREQ reset without a valid bootloader reset word (firmware/diag.c:8).
# Run from the project folder: openocd -f swd/provision-bindkey.tcl
if {![info exists ELF]} {
    set ELF build/debug/build/debug/mccgq02hl_bthome.out
}
set SECRETS firmware/secrets.h
set MAGIC 0x4B455931

proc parse_key {text} {
    if {![regexp {BTHOME_BINDKEY\s*\{([^\}]*)\}} $text -> body]} {
        return {}
    }
    set key {}
    foreach b [regexp -all -inline {0[xX][0-9a-fA-F]{2}} $body] {
        lappend key [expr {$b + 0}]
    }
    return $key
}

# Address of a symbol from the nm output, or "" when absent.
proc sym_addr {nm_out name} {
    foreach line [split $nm_out "\n"] {
        if {[regexp "^(\[0-9a-fA-F\]+)\\s+\\S\\s+$name\$" [string trim $line] -> a]} {
            return [expr {"0x$a" + 0}]
        }
    }
    return ""
}

# --- main
if {![file exists $SECRETS]} {
    echo "RESULT: $SECRETS missing: run tools/gen-bindkey.sh first"
    shutdown
}
set f [open $SECRETS]
set text [read $f]
close $f
set key [parse_key $text]
set text ""
if {[llength $key] != 16} {
    echo "RESULT: no 16-byte BTHOME_BINDKEY in $SECRETS"
    shutdown
}

set nm [lindex [glob -nocomplain sdk/home/.silabs/slt/installs/conan/p/gcc-*/p/bin/arm-none-eabi-nm] 0]
if {![file exists $ELF] || $nm eq ""} {
    echo "RESULT: ELF $ELF or the SDK nm is missing; nothing written"
    shutdown
}
if {[catch {exec $nm $ELF} nm_out]} {
    echo "RESULT: nm failed on $ELF; nothing written"
    shutdown
}
set main_addr [sym_addr $nm_out main]
set hand_addr [sym_addr $nm_out g_bindkey_handoff]
if {$main_addr eq "" || $hand_addr eq ""} {
    echo "RESULT: main or g_bindkey_handoff not in $ELF; nothing written"
    shutdown
}
set bp_addr [expr {$main_addr & ~1}]  ;# clear the Thumb bit in case the symbol value carries it (nm prints main even)

source swd/halt-after-reset.tcl
if {![halt_after_reset]} {
    echo "RESULT: could not halt the CPU"
    shutdown
}
lassign [restart_to_reset_vector] restarted cleared
if {!$cleared} {
    echo "RESULT: VC_CORERESET may still be set: pull the battery (POR) so the chip can sleep"
}
if {!$restarted} {
    echo "RESULT: no halt on the reset vector; nothing written"
    shutdown
}

# At main the C startup has zeroed .bss, so the hand-off holds no stale bytes and nothing later clears it.
bp $bp_addr 2 hw
resume
set reached 0
for {set i 0} {$i < 50 && !$reached} {incr i} {
    sleep 100
    set reached [expr {[efm32.cpu curstate] eq "halted"}]
}
rbp $bp_addr
if {!$reached} {
    echo "RESULT: the CPU did not reach main; nothing written"
    shutdown
}

# Key first, magic last (the layout is { uint32 magic; uint8 key[16]; }).
write_memory [expr {$hand_addr + 4}] 8 $key
write_memory $hand_addr 32 $MAGIC
resume
sleep 2000
if {[catch {halt 200}] || [efm32.cpu curstate] ne "halted"} {
    echo "RESULT: key written to RAM; not verified (no halt): check the packets"
} elseif {[lindex [read_memory $hand_addr 32 1] 0] + 0 == 0} {
    echo "RESULT: key stored in NVM3 (0x00003)"
} else {
    echo "RESULT: the app did not take the key (magic still set)"
}
catch {resume}
shutdown
