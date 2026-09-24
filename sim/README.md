# MMT-8 Simulator

A hardware-level simulator for the **Alesis MMT-8 MIDI sequencer**. It runs the
unmodified original firmware binary (`alesis_mmt8_v111.bin`) on an 8051 CPU
emulator, with custom C code implementing the RAM, address decoding, I/O
latches, LCD, keyboard matrix and UART from the schematic. SDL2 provides a GUI
showing the LCD display, LEDs and clickable buttons; the MIDI IN and MIDI OUT
jacks are exposed as ALSA sequencer ports, so the simulated MMT-8 can be wired
to real keyboards, synths and DAWs like any other MIDI device.

![The simulator's front panel](../docs/images/simulator.png)

## Status

Working:

- Firmware boots, runs its main loop and passes its own built-in diagnostics
  (RAM, EPROM checksum, LED, MIDI loopback).
- LCD, the eight track LEDs and the mode/transport LEDs.
- Every front-panel button has a verified matrix position, pinned by the
  behaviour specs (see [Keyboard matrix](#keyboard-matrix-6-columns-x-8-rows)).
- MIDI IN and MIDI OUT through ALSA. Real-time recording, playback, MIDI
  clock/start/stop output and MIDI ECHO all work: record a phrase from a
  keyboard, press STOP, press PLAY and it comes back out.
- Headless mode with scripted button presses, an LCD change log and a raw MIDI
  byte trace, for automated testing.

Not implemented: tape sync in/out (the self-test's TAPE step reports an
error), the footswitch input, the metronome click output, LCD custom
characters.

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
make spec     # from the repo root: runs the behaviour specs in specs/mmt8 (see below)
```

## Running

```
./mmt8sim                              # uses ../firmware/alesis_mmt8_v111.bin
./mmt8sim /path/to/alesis_mmt8_v111.bin  # explicit path
```

Click the buttons with the mouse, or use the keyboard: every button has a
shortcut shown in its corner (Space = PLAY, S = STOP, R = REC, E = ERASE,
C = COPY, G = LENGTH, arrows = << / >>, F1–F8 = tracks, digits, `=` / `-`,
and so on). A key held on the keyboard stays down while you click, which is
how two-button gestures like ERASE + RECORD or RECORD + LENGTH are made.
Press **Escape** or close the window to exit.

On startup the simulator registers an ALSA sequencer client named
`MMT-8 Simulator` with two ports:

| Port | Direction | Purpose |
|------|-----------|---------|
| `MMT-8 MIDI IN`  (port 0) | writable | the MMT-8's MIDI IN jack |
| `MMT-8 MIDI OUT` (port 1) | readable | the MMT-8's MIDI OUT jack |

The port names carry the device name because some applications (JUCE-based
ones such as Surge XT) list ALSA ports by port name alone.

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
| `-s`, `--screenshot BMP` | save the window to a BMP file at exit |
| `-m`, `--memory FILE` | battery-backed RAM image to load at start and save at exit |
| `--fresh` | start with empty memory, ignoring the saved image |
| `--load-syx FILE` | replay a `.syx` memory dump into MIDI IN after boot |
| `-S`, `--script` | deterministic stdin protocol for the spec suite (see below) |

Scripted presses without `@MS` start 1.5 s after boot and are spaced 400 ms
apart. At exit, headless runs print the LCD and UART statistics (bytes
received, transmitted, and how many the firmware's ISR actually consumed).

### Memory and backups

The real MMT-8 keeps its two SRAMs alive with a battery. The simulator does
the same with a file: in GUI mode the 64 KB XDATA image is saved to
`~/.local/share/mmt8sim/memory.bin` on exit and restored at the next start
as a warm boot, so parts and songs survive restarts. `--memory FILE` moves the
image elsewhere (and enables it for headless runs), `--fresh` ignores it for
one run. The firmware's own "clear memory" combination (ERASE + PAGE UP +
PAGE DOWN at power-on) still works and is saved like anything else.

Backups use the firmware's SysEx dump, the same one a MIDI filer would take:

- **Ctrl+S** in the GUI performs TAPE, page down, RECORD for you (SEND ALL
  PARTS & SONGS OUT MIDI) and writes the message to
  `~/.local/share/mmt8sim/mmt8-YYYYMMDD-HHMMSS.syx`.
- **Ctrl+L** replays the newest `.syx` in that directory into MIDI IN; the
  firmware loads it and lands on SELECT SONG 99, as on the real unit.
- `--load-syx FILE` does the same from the command line, 1.5 s after boot.

A dump taken from a real MMT-8 over MIDI (`amidi -r`, or any SysEx librarian)
is the same format, so the simulator can carry a real unit's memory.

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
| `0xFF02` | HC574       | Write      | Track LEDs 1–8 (bit n = track n+1, active low) |
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
| 3 | EDIT |
| 4 | SONG |
| 5 | MIDI ECHO |
| 6 | LOOP |

The track LEDs come from `0xFF02` and are active low as well: at rest all
eight are lit (all tracks on) and a track button clears one. The GUI reads both
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
| 5 | MIDI FILTER | MIDI ECHO | LOOP | QUANTIZE | LENGTH | PART | EDIT | NAME |

PAGE UP / PAGE DOWN were identified from the boot-time "clear memory" check
(ERASE + PAGE UP + PAGE DOWN); EDIT and NAME only respond on a part that has
data. The three unused positions in column 2 do nothing. The table lives in
`mmt8_keys.c` and every position is pinned by the `button-*.yaml` specs in
`specs/mmt8` (see [Behaviour specs](#behaviour-specs-panelspec)).

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

The GUI renders a 780x600 window using SDL2 + SDL2_ttf, laid out like the
real front panel: a 4x3 block of function keys (QUANT, LENGTH, PART / COPY,
NAME, EDIT / TRANS, MERGE, SONG / ERASE, TAPE, MIDI CHAN) with PAGE DOWN and
PAGE UP beneath, the LCD over the keypad (1-5, 6-0, - and +), a column of
LOOP, MIDI ECHO, MIDI FILTER, CLOCK, CLICK and TEMPO on the right, the eight
TRACK keys across the middle and the transport (<<, >>, green PLAY,
STOP/CONT, red RECORD) along the bottom.

- **LCD display**: Amber text (#FFAA00) on dark background (#332200) in a
  bordered rectangle. Monospace font, 2 lines x 16 characters.
- **Buttons**: Light membrane keys that darken on press, each showing its
  keyboard shortcut in the corner where there is room.
- **LEDs**: Placed as on the unit (right of PART/EDIT/SONG, left of LOOP and
  MIDI ECHO, above the tracks, PLAY and RECORD), driven by the two LED
  latches described above.
- **MIDI IN / OUT dots** in the header strip flash on message traffic.
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

## Behaviour specs (panelspec)

`specs/mmt8/` describes the MMT-8 as physical inputs and observable results,
one behaviour per file, in the [panelspec](https://github.com/audiodestrukt/hexatrack)
format (`docs/spec-format.md` there is the reference). Each spec cites the
manual chapter it comes from. The suite runs against the original firmware in
this simulator, so a passing spec is as good as a check on real hardware:

```
cargo install --git https://github.com/audiodestrukt/hexatrack panelspec
make spec                       # from the repo root: lint, then run everything
panelspec run -a "python3 sim/adapters/mmt8_sim.py" specs/mmt8 --tag buttons
```

What is covered:

- `button-*.yaml` pin every front-panel button's matrix position by an
  observable effect (a held page, an LED, a keypad digit); this is what makes
  the mapping in `mmt8_keys.c` verified rather than guessed.
- The rest follow the manual: part selection, PLAY / STOP / CONTINUE, next
  part, recording with count-down, LENGTH, ERASE, COPY, LOOP, MIDI ECHO,
  NAME, EDIT, MIDI CHANNEL, TRANSPOSE, QUANTIZE, TEMPO and MIDI clock, CLICK
  pages, MIDI FILTER, CLOCK pages and external MIDI start, autolocate, memory
  across a power cycle, the clear-memory combo and the diagnostic self-test.
- Song mode: selecting songs, step editing (insert, erase, blank step past
  the end), per-step track mutes, play / continue / loop / end, NAME, tempo
  stored per song, COPY and ERASE of songs, track time offsets, autolocate,
  and the mode being remembered across power.
- The SysEx memory dump (SEND ALL PARTS & SONGS OUT MIDI) as a round trip:
  dump, clear memory, replay the dump into MIDI IN, part restored.

Not covered: MERGE and the tape functions (no audio path in the simulator;
a recording of a real MMT-8 tape dump would make that possible).

`specs/mmt8/vocab.yaml` is the vocabulary: the buttons, a virtual MIDI
keyboard on MIDI IN (`midi_note60`, `midi_cc7`, `midi_program`, `midi_start`,
`midi_clock`, ...), a `power` switch, the LCD lines and cursor, the LEDs, MIDI
clock and message counters, and MIDI OUT messages as events.

`sim/adapters/mmt8_sim.py` is the adapter. It speaks the panelspec JSON
protocol on stdin/stdout and drives `mmt8sim --script`, a deterministic mode
with no window, no ALSA and no wall clock: one command per line (`wait`,
`press`, `release`, `midi`, `reset cold|warm`, `lcd`, `leds`, `midiout`,
`quit`), one reply per command. Time only moves on `wait`, so a full run takes
seconds and always gives the same answer. A pressed key is held for at least
50 ms before release so the firmware's keyboard scan sees it, as any physical
tap would be.

Writing a new spec: run the gesture once with a guessed expectation; the
runner prints what the firmware actually showed, which becomes the spec. If
the firmware disagrees with the manual, keep the manual's claim in the
description and record what the firmware does (see `quantize.yaml`).

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
