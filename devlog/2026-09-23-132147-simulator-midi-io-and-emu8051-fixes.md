# Simulator MIDI I/O and emu8051 Fixes

_2026-09-23_

![MMT-8 simulator playing part 00](assets/2026-09-23-132147-simulator-midi-io-and-emu8051-fixes.png)

## What happened

I came back to the MMT-8 simulator to wire up MIDI, and MIDI itself turned out to be the easy part: a cycle-counted UART in the hardware layer, an ALSA sequencer bridge, and the simulated MMT-8 showed up in `aconnect -l` like any other device. The hard part was what happened next — pressing PLAY spat out phantom note events and stopping a recording left the display reading `SELECT PART 250`. Tracing IRAM writes instruction by instruction led to the emu8051 core itself: `MOV direct,@Ri` had its operands swapped, so a `MOV DPL,@R0` in the firmware was quietly overwriting the per-track pointer table, and a differential test against the MCS-51 spec turned up three more silent bugs (auxiliary carry, `XCHD`, `DA A` carry). With those fixed, the original 1987 firmware passes its own RAM, EPROM and MIDI diagnostics, records a phrase from a keyboard, and plays it back out with MIDI clock — a real Alesis MMT-8 running as a Linux MIDI device.

Along the way the headless mode grew scripted button presses and an LCD change log, which let me press all 48 key-matrix positions and read the firmware's reaction off the display to recover the real button map (43 of 45 buttons, EDIT and NAME still hiding).

## Files touched

  - README.md — simulator section, key matrix, LED latch bits, file layout
  - sim/README.md — rewritten: usage, MIDI, options, self-test, emulator patches
  - sim/mmt8_hw.c, sim/mmt8_hw.h — 8051 UART emulation (TX/RX, TI/RI, FIFOs)
  - sim/mmt8_midi.c, sim/mmt8_midi.h — ALSA sequencer MIDI IN / MIDI OUT ports
  - sim/main.c — CLI options, headless mode, scripted presses, LCD log, loopback, screenshot
  - sim/mmt8_gui.c — verified button matrix, status-latch LEDs, software renderer fallback
  - sim/opcodes.c — emu8051 fixes: MOV direct,@Ri, AC flag, XCHD, DA A
  - sim/tests/cputest.c, sim/Makefile — differential CPU core test (`make test`)

## Tweet draft

Got MIDI working on my Alesis MMT-8 simulator: the original 1987 firmware now runs as an ALSA MIDI device on Linux, records from a keyboard and plays back with clock. The real fight was four silent bugs in the 8051 emulator core, found by watching one byte of RAM change. [link]

---

_commit: ad07112 · screenshot: captured (sim --screenshot)_
