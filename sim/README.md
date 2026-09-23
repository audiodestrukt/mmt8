# MMT-8 Simulator

A hardware-level simulator for the **Alesis MMT-8 MIDI sequencer**. It runs the
unmodified original firmware binary (`alesis_mmt8_v111.bin`) on an 8051 CPU
emulator, with custom C code implementing the RAM, address decoding, I/O
latches, LCD, keyboard matrix and UART from the schematic. SDL2 provides a GUI
showing the LCD display, LEDs and clickable buttons; the MIDI IN and MIDI OUT
jacks are exposed as ALSA sequencer ports, so the simulated MMT-8 can be wired
to real keyboards, synths and DAWs like any other MIDI device.

## Status

Working:

- Firmware boots, runs its main loop and passes its own built-in diagnostics
  (RAM, EPROM checksum, LED, MIDI loopback).
- LCD, the eight track LEDs and the mode/transport LEDs.
- Every front-panel button except EDIT and NAME has a verified matrix
  position (see [Keyboard matrix](#keyboard-matrix-6-columns-x-8-rows)).
- MIDI IN and MIDI OUT through ALSA. Real-time recording, playback, MIDI
  clock/start/stop output and MIDI ECHO all work: record a phrase from a
  keyboard, press STOP, press PLAY and it comes back out.
- Headless mode with scripted button presses, an LCD change log and a raw MIDI
  byte trace, for automated testing.

Not implemented: tape sync in/out (the self-test's TAPE step reports an
error), the footswitch input, the metronome click output, LCD custom
characters, and the EDIT / NAME button positions.

## Building

### Dependencies

- GCC
- SDL2 and SDL2_ttf development libraries
- ALSA development library (`libasound2-dev`)

On Debian/Ubuntu:

```
sudo apt install libsdl2-dev libsdl2-ttf-dev libasound2-dev
```

### Compile

```
cd sim
make          # builds ./mmt8sim
make test     # builds and runs tests/cputest (emu8051 core self-check)
```

## Running

```
./mmt8sim                              # uses ../firmware/alesis_mmt8_v111.bin
./mmt8sim /path/to/alesis_mmt8_v111.bin  # explicit path
```

Click the buttons with the mouse. Press **Escape** or close the window to exit.

On startup the simulator registers an ALSA sequencer client named
`MMT-8 Simulator` with two ports:

| Port | Direction | Purpose |
|------|-----------|---------|
| `MIDI IN`  (port 0) | writable | the MMT-8's MIDI IN jack |
| `MIDI OUT` (port 1) | readable | the MMT-8's MIDI OUT jack |

Connect them with `aconnect`, `qjackctl`, Helvum, or any other ALSA/PipeWire
patchbay. Port names must be given numerically to `aconnect`:

```
aconnect -l                                   # find client numbers
aconnect "Keystation:0" "MMT-8 Simulator:0"   # keyboard -> MMT-8 MIDI IN
aconnect "MMT-8 Simulator:1" "FLUID Synth:0"  # MMT-8 MIDI OUT -> synth
```

Or let the simulator connect for you:

```
./mmt8sim --midi-in 24:0 --midi-out 128:0
```

### A first session

1. Connect a keyboard (or `aplaymidi`) to `MIDI IN` and a synth to `MIDI OUT`.
2. Hold **REC**, press **PLAY**. The display shows `RECRDING PART 00` and a
   five-beat count-down at 120 BPM, then `BEAT 001`, `BEAT 002`, ...
3. Play something, then press **STOP**.
4. Press **PLAY**. The recorded phrase plays back, with MIDI clock (`F8`),
   Start (`FA`) and Stop (`FC`) on the output. **MIDI ECHO** merges incoming
   MIDI to the output while recording.

### Command-line options

| Option | Effect |
|--------|--------|
| `-H`, `--headless` | run without the SDL window (still needs no display) |
| `-n`, `--no-midi` | do not create the ALSA ports |
| `-t`, `--midi-trace` | print every MIDI byte in/out to stderr |
| `-l`, `--lcd-log` | print the LCD contents (and LED latches) whenever they change |
| `-L`, `--loopback` | feed MIDI OUT straight back into MIDI IN as raw bytes |
| `-i`, `--midi-in ADDR` | connect ALSA port `ADDR` to MIDI IN |
| `-o`, `--midi-out ADDR` | connect MIDI OUT to ALSA port `ADDR` |
| `-p`, `--press C,R[@MS]` | press key-matrix column `C` row `R`; optional start time in emulated ms (repeatable) |
| `-d`, `--hold MS` | how long each scripted press is held (default 100) |
| `-x`, `--exit-after MS` | exit after `MS` ms of emulated time and dump the LCD |

Scripted presses without `@MS` start 1.5 s after boot and are spaced 400 ms
apart. At exit, headless runs print the LCD and UART statistics (bytes
received, transmitted, and how many the firmware's ISR actually consumed).

### Firmware self-test

Holding LOOP and QUANTIZE at power-on puts the real MMT-8 into its diagnostic
mode. In the simulator:

```
./mmt8sim -H -n -L -l -p 5,2@0 -p 5,3@0 -d 3000 -x 12000
```

prints `ALL RAM OK`, `EPROM OK`, `LED TEST`, `MIDI IN/OUT OK` (thanks to
`--loopback`) and finally `TAPE I/O ERROR`, because tape sync is not emulated.
Holding ERASE, PAGE UP and PAGE DOWN at power-on (`-p 0,2@0 -p 2,6@0 -p 2,7@0`)
performs the firmware's "clear all memory" procedure.

## Architecture

```
sim/
├── Makefile          Build system (make, make test, make clean)
├── main.c            CLI options, main loop, timing, MIDI pump, scripted presses
├── emu8051.h         emu8051 header (jarikomppa/emu8051, patched)
├── core.c            emu8051 core: timers, interrupts
├── opcodes.c         emu8051 opcodes (patched, see below)
├── disasm.c          emu8051 disassembler
├── mmt8_hw.h/.c      Address decode, RAM, I/O latches, keyboard matrix, LCD, UART
├── mmt8_gui.h/.c     SDL rendering: LCD, buttons, LEDs, mouse input
├── mmt8_midi.h/.c    ALSA sequencer bridge (MIDI IN / MIDI OUT ports)
└── tests/cputest.c   Differential test of the CPU core against a reference model
```

### Data flow

```
┌────────────────────────────────────────────────────────────────────────┐
│ main.c  –  main loop (1 machine cycle per real microsecond)            │
│   tick(&cpu)        →  emu8051 executes one machine cycle              │
│                          ├─ CODE read   → cpu.mCodeMem (32 KB ROM)     │
│                          ├─ XDATA r/w   → mmt8_hw (RAM, latches, LCD)  │
│                          ├─ P1 read     → mmt8_hw keyboard rows        │
│                          └─ SBUF/SCON   → mmt8_hw UART                 │
│   mmt8_hw_tick()    →  UART shifts bytes in/out, raises TI/RI          │
│   pump_midi()       →  UART FIFOs  ⇄  mmt8_midi  ⇄  ALSA sequencer     │
│   SDL_PollEvent     →  mmt8_gui  →  key_matrix updates                 │
│   gui_render        →  reads LCD/LED state from mmt8_hw                │
└────────────────────────────────────────────────────────────────────────┘
```

## Design Details

### CPU Emulation (emu8051)

The emulator core is [jarikomppa/emu8051](https://github.com/jarikomppa/emu8051),
an 8051 emulator in C. It provides function-pointer callbacks for external
memory reads/writes and SFR register access, which is exactly the seam needed
to intercept I/O.

The upstream core needed several patches before the MMT-8 firmware ran
correctly. Each is marked `Patched:` in the source:

| Where | Bug | Symptom before the fix |
|-------|-----|------------------------|
| `opcodes.c` MOVX @Ri (0xE2/E3/F2/F3) | used only Ri as the address; the 8051 uses `P2:Ri` | firmware could not reach its XDATA pages and never booted |
| `opcodes.c` MOV direct,@Ri (0x86/0x87) | source and destination swapped | `MOV DPL,@R0` overwrote the firmware's per-track pointer table; stopping a recording corrupted the part number and played phantom notes |
| `opcodes.c` add/sub flags | auxiliary carry taken from bit 2 instead of bit 3 | wrong `DA A` results, i.e. wrong BCD arithmetic |
| `opcodes.c` XCHD | read ACC after it had already been modified | memory nibble never written |
| `opcodes.c` DA A | carry set for results 0x9A–0x9F | edge case only |

`tests/cputest.c` (`make test`) exercises the arithmetic, flag, BCD, MOV, XCH,
logic and branch instructions with random operands against a reference model
written from the MCS-51 manual, so regressions in the core are caught before
they turn into mysterious firmware behaviour. The firmware's own RAM and EPROM
self-tests are a second, independent check.

**Firmware loading.** The 32 KB ROM image is loaded directly into `mCodeMem`
via `fread()` (raw binary, not Intel HEX). XDATA is zero-filled at start; the
firmware notices the missing RAM signature (`0x27 0xB5` at XDATA `0x02FE`) and
runs its cold-start initialisation, exactly as a real unit with a dead memory
battery would.

**Memory map:**

| Region | Size | Backing |
|--------|------|---------|
| CODE (27C256 EPROM) | 32 KB | `cpu.mCodeMem` |
| XDATA (two 61256 SRAMs) | 64 KB | `cpu.mExtData` |
| Internal RAM (80C31) | 256 B | `cpu.mLowerData` + `cpu.mUpperData` |

### Hardware Emulation (`mmt8_hw.c`)

#### Address Decoding (HC138 U5)

The HC138 decodes XDATA accesses in the 0xFF00–0xFF1F range. Address bit A0
serves as the LCD RS pin. Normal SRAM occupies 0x0000–0xFEFF.

| Address  | Device       | Direction  | Function                     |
|----------|-------------|------------|------------------------------|
| `0xFF00` | HC574       | Write      | LED control latch (always 1 after boot) |
| `0xFF02` | HC574       | Write      | Track LEDs 1–8 (bit n = track n+1, active high) |
| `0xFF04` | HC574       | Read/Write | Mode/transport LED latch (active low, see below) |
| `0xFF06` | U8 HC574    | Write      | Keyboard column select       |
| `0xFF08` | LCD HD44780 | Write      | LCD command register (RS=0)  |
| `0xFF09` | LCD HD44780 | Write      | LCD data register (RS=1)     |
| `0xFF0E` |             | Read/Write | Transport state              |
| `0xFF0F` |             | Read/Write | Beat divider                 |
| `0xFF1A` |             | Read/Write | Click enable                 |

Reads/writes below 0xFF00 pass through to `cpu.mExtData[]` (SRAM).

#### LEDs

The status latch at `0xFF04` is active low; the firmware clears a bit to
light the LED:

| Bit | LED |
|-----|-----|
| 0 | PLAY |
| 1 | RECORD |
| 2 | PART |
| 3 | EDIT (assumed) |
| 4 | SONG |
| 5 | MIDI ECHO |
| 6 | LOOP |

The track LEDs come from `0xFF02` and are active high. The GUI reads both
latches every frame (`mmt8_get_led_data()`, `mmt8_get_status_latch()`).

#### HD44780 LCD Emulation

The LCD is emulated as a state machine tracking:

- `ddram[2][40]` — character buffer (2 lines x 40 characters each)
- `cursor_addr` — current DDRAM write position
- `display_on`, `cursor_on`, `blink_on` — display control flags
- `entry_increment` — cursor direction after write (1 = right, 0 = left)

**Command handling** (RS=0, write to 0xFF08):

| Command      | Action                                      |
|-------------|---------------------------------------------|
| `0x01`      | Clear display, cursor to 0                   |
| `0x02`      | Return home (cursor to 0)                    |
| `0x04–0x07` | Entry mode set (increment/decrement)         |
| `0x08–0x0F` | Display on/off, cursor on/off, blink on/off  |
| `0x20–0x3F` | Function set (accepted, 8-bit 2-line mode)   |
| `0x80+addr` | Set DDRAM address (line 1: 0x00+, line 2: 0x40+) |

**Data handling** (RS=1, write to 0xFF09):
Write character at cursor position, then advance cursor per entry mode.

The visible display is the first 16 characters of each line.

#### Keyboard Matrix (6 columns x 8 rows)

The keyboard scanner in the firmware:
1. Writes a column select byte to 0xFF06 (one bit low = that column active)
2. Reads P1 to get the row state (pressed keys pull bits low)

The column select pattern rotates through 0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF
(bits 0–5 active-low). A pre-check writes 0x80 to detect if any key is pressed
before scanning individual columns. The P1 SFR read callback ORs the pressed
rows of every selected column into the result.

The mapping was determined empirically, by pressing each of the 48 positions
in the headless simulator (`--press C,R --hold 2000 --lcd-log`) and watching
what the firmware did:

| Col | Row 0 | Row 1 | Row 2 | Row 3 | Row 4 | Row 5 | Row 6 | Row 7 |
|-----|-------|-------|-------|-------|-------|-------|-------|-------|
| 0 | `<<` | `>>` | ERASE | TRANSPOSE | PLAY | STOP/CONT | COPY | RECORD |
| 1 | Track 1 | Track 2 | Track 3 | Track 4 | Track 5 | Track 6 | Track 7 | Track 8 |
| 2 | TEMPO | `-` | `+` | – | – | – | PAGE UP | PAGE DOWN |
| 3 | CLICK | 6 | 7 | 8 | 9 | 0 | MIDI CHANNEL | TAPE |
| 4 | CLOCK | 1 | 2 | 3 | 4 | 5 | SONG | MERGE |
| 5 | MIDI FILTER | MIDI ECHO | LOOP | QUANTIZE | LENGTH | ? | ? | PART |

PAGE UP / PAGE DOWN were identified from the boot-time "clear memory" check
(ERASE + PAGE UP + PAGE DOWN). EDIT and NAME do nothing visible on an empty
part and have not been pinned down; the GUI provisionally wires them to (5,5)
and (5,6). The mapping lives in `mmt8_gui.c:init_buttons()`.

#### UART (MIDI)

The 80C31 UART runs in mode 1 with Timer 1 as the baud generator
(`TH1 = 0xFF` at 12 MHz gives 31.25 kbaud), so one 10-bit frame takes exactly
320 machine cycles. The emulation replaces emu8051's stub serial port:

- **TX**: a write to `SBUF` (SFR write callback) starts a 320-cycle
  transmission. When it completes, the byte is queued for the host, `TI` is set
  and the serial interrupt is raised. The firmware's ISR then loads the next
  byte from its ring buffer in XDATA page 0.
- **RX**: bytes from the host are shifted in one at a time (320 cycles each)
  and loaded into a separate receive `SBUF` with `RI` set, provided `REN` is
  set and the previous byte has been read. The `SBUF` read callback returns the
  receive register, so TX and RX never clobber each other.
- **Interrupt semantics**: the real serial interrupt is level-sensitive on
  `TI | RI`, but emu8051 models it as an edge flag. A write callback on `SCON`
  re-arms the flag whenever either bit is left set. This makes the firmware's
  software `SETB TI` kick-start work, and lets an ISR that clears `RI` while
  `TI` is pending be re-entered as on real hardware.

`mmt8_hw_tick()` advances the UART once per machine cycle. FIFOs in both
directions decouple it from the host.

### MIDI Bridge (`mmt8_midi.c`)

Uses the ALSA sequencer API with `snd_midi_event` to convert between raw UART
bytes and sequencer events. Outgoing bytes are assembled into complete
messages (so a synth never sees half a note-on); incoming events are decoded
with running status disabled so the firmware always receives explicit status
bytes. SysEx is passed through in both directions. The bridge is polled from
the main loop every millisecond; `--loopback` bypasses it and feeds raw bytes
straight back, which is what the firmware's MIDI self-test needs (it sends
`0x00 0x55`, not a valid MIDI message).

### GUI (`mmt8_gui.c`)

The GUI renders an 820x500 window using SDL2 + SDL2_ttf:

- **LCD display**: Amber text (#FFAA00) on dark background (#332200) in a
  bordered rectangle. Monospace font, 2 lines x 16 characters.
- **Buttons**: Rectangles with text labels arranged to approximate the MMT-8
  front panel. Darken on press.
- **LEDs**: Small squares above buttons that have indicators, driven by the
  two LED latches described above. Red for REC, green otherwise.
- **Font discovery**: Tries several common monospace font paths
  (DejaVu Sans Mono, Liberation Mono, FreeMono). Falls back to no text if none
  found.
- Falls back to SDL's software renderer when no accelerated one is available
  (e.g. `SDL_VIDEODRIVER=dummy`).

### Main Loop (`main.c`)

The 80C31 runs at 12 MHz with a divide-by-12 clock, giving 1,000,000 machine
cycles per second. Each `tick()` executes one machine cycle.

The main loop:
1. Measures real elapsed time with `clock_gettime(CLOCK_MONOTONIC)`
2. Converts to machine cycles (1 cycle = 1 microsecond)
3. Caps at 50,000 cycles per frame to prevent spiral-of-death
4. Executes that many `tick()` + `mmt8_hw_tick()` calls
5. Applies scripted key presses, pumps MIDI, logs the LCD if asked
6. Polls SDL events and renders (GUI mode) or sleeps 1 ms (headless)

## Debugging the firmware with the simulator

The headless options make it easy to script experiments and to see what the
firmware is doing:

```
# What does key (3,6) do? Hold it for 2 s and log the display.
./mmt8sim -H -n -l -p 3,6 -d 2000 -x 2600

# Record nothing for a few beats, stop, then look at the LENGTH page.
./mmt8sim -H -n -l -p 0,7@1500 -p 0,4@1900 -p 0,5@4500 -p 5,4@5000 -d 600 -x 5400

# Watch the raw MIDI bytes the firmware sends when PLAY is pressed.
./mmt8sim -H -n -t -p 0,4 -x 3000
```

For deeper problems, `main.c`'s cycle loop is the place to add a PC breakpoint
or a watch on IRAM/XDATA (compare a few bytes after every `tick()` and print
`cpu.mPC` when they change). That is how the `MOV direct,@Ri` bug was found:
watching the per-track pointer table at IRAM 0x5D change on an instruction
that should only have read it.

## Known Limitations and Future Work

- **EDIT and NAME** buttons are not yet located in the matrix.
- **Tape sync, footswitch, click output** are not emulated; the self-test's
  TAPE step fails.
- **Timer/interrupt accuracy** — emu8051's built-in timer handling covers
  Timer 0 (sequencer clock) and Timer 1 (baud rate). EXT_INT0 is used by the
  firmware for tape sync and is never triggered.
- **LCD read-back** is not implemented (the firmware doesn't read the busy
  flag — it uses software delay loops instead).
- **No CGRAM** — custom character definitions are not implemented in the LCD
  emulation, so any custom glyphs will display as spaces.
- **Battery-backed RAM** is not persisted between runs; every start is a cold
  start with empty memory. Saving/restoring `cpu.mExtData` to a file would
  give the simulator a memory battery.
