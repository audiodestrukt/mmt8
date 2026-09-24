#!/usr/bin/env python3
"""panelspec adapter for the MMT-8 hardware simulator.

Speaks the panelspec adapter protocol (JSON lines on stdin/stdout) and drives
the original v1.11 firmware running in ../mmt8sim --script. Standard library
only.

    panelspec run -a "python3 sim/adapters/mmt8_sim.py" specs/mmt8

Controls (see specs/mmt8/vocab.yaml): every front-panel button by name, plus a
virtual MIDI keyboard on MIDI IN (midi_note{0..127} as buttons, midi_cc / midi_program
as knobs, midi_start / midi_stop / midi_continue as buttons that send one
real-time message, midi_channel as a switch) and a power switch. Observables
are the LCD text, the LEDs and MIDI-out counters; events are the MIDI messages
the firmware sends.
"""

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SIM = os.path.join(HERE, "..", "mmt8sim")
DEFAULT_ROM = os.path.join(HERE, "..", "..", "firmware", "alesis_mmt8_v111.bin")

# Front-panel controls -> simulator key names (mmt8_keys.c)
BUTTONS = {
    "play": "PLAY", "stop": "STOP", "rec": "REC", "rew": "REW", "ff": "FF",
    "part": "PART", "song": "SONG", "edit": "EDIT", "name": "NAME",
    "pgup": "PGUP", "pgdn": "PGDN", "plus": "PLUS", "minus": "MINUS",
    "tempo": "TEMPO", "click": "CLICK", "copy": "COPY", "erase": "ERASE",
    "loop": "LOOP", "echo": "ECHO", "length": "LENGTH", "merge": "MERGE",
    "quant": "QUANT", "trans": "TRANS", "filter": "FILTER", "midich": "MIDICH",
    "clock": "CLOCK", "tape": "TAPE",
}
for _i in range(1, 9):
    BUTTONS[f"track{_i}"] = f"T{_i}"
for _i in range(10):
    BUTTONS[f"digit{_i}"] = str(_i)

# Both LED latches are active low (0 = lit). Status latch (0xFF04) bits:
STATUS_LEDS = {"play": 0, "rec": 1, "part": 2, "edit": 3, "song": 4, "echo": 5, "loop": 6}

# How long the boot splash is shown before the firmware is usable (emulated ms)
BOOT_MS = 1000

# A key must stay down long enough for the firmware's keyboard scan to see it
# (scan + debounce), as any physical tap does. `press` is down+up with no time
# in between, so `up` first lets the key be held for at least this long.
MIN_HOLD_MS = 50

# Most MIDI-out events kept between `events` calls (clocks alone are 48/s).
MAX_PENDING = 20000

REALTIME = {0xF8: "clock", 0xFA: "start", 0xFB: "continue", 0xFC: "stop", 0xFE: "active_sensing", 0xFF: "reset"}
CHANNEL_STATUS = {0x80: "note_off", 0x90: "note_on", 0xA0: "poly_pressure", 0xB0: "control_change",
                  0xC0: "program_change", 0xD0: "channel_pressure", 0xE0: "pitch_bend"}
DATA_LEN = {0x80: 2, 0x90: 2, 0xA0: 2, 0xB0: 2, 0xC0: 1, 0xD0: 1, 0xE0: 2}


class Sim:
    """A running mmt8sim --script process and the simple line protocol."""

    def __init__(self, binary, rom):
        self.proc = subprocess.Popen([binary, "--script", rom], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        self.lines = iter(self.proc.stdout)

    def cmd(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        for reply in self.lines:
            reply = reply.rstrip("\n")
            if reply.startswith(("ok", "err", "lcd ", "leds ", "midiout")):
                if reply.startswith("err"):
                    raise RuntimeError(f"simulator: {reply}")
                return reply
        raise RuntimeError("simulator exited")

    def wait(self, ms):
        self.cmd(f"wait {int(ms)}")

    def press(self, key):
        self.cmd(f"press {key}")

    def release(self, key):
        self.cmd(f"release {key}")

    def midi(self, data):
        self.cmd("midi " + " ".join(f"{b:02X}" for b in data))

    def reset(self, cold=True):
        self.cmd("reset cold" if cold else "reset warm")

    def lcd(self):
        parts = self.cmd("lcd").split()
        raw = bytes.fromhex(parts[1])
        self.cursor_addr, self.cursor_on = int(parts[2]), int(parts[3]) == 1
        return raw[:16].decode("latin1"), raw[16:].decode("latin1")

    def cursor(self):
        """Index of the underlined character: 0-15 line 1, 16-31 line 2, None if off."""
        self.lcd()
        if not self.cursor_on:
            return None
        a = self.cursor_addr
        if a < 16:
            return a
        if 0x40 <= a < 0x50:
            return 16 + a - 0x40
        return None

    def leds(self):
        _, data, status = self.cmd("leds").split()
        return int(data, 16), int(status, 16)

    def midiout(self):
        parts = self.cmd("midiout").split()[1:]
        return bytes(int(p, 16) for p in parts)

    def close(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
        self.proc.wait(timeout=5)


def parse_midi(data):
    """Split a raw MIDI byte stream into event dicts. Always advances, so a
    malformed stream can never loop; unexpected bytes are skipped."""
    events, i, running = [], 0, None
    n = len(data)
    while i < n:
        b = data[i]
        if b >= 0xF8:                                   # real-time, may interleave
            events.append({"type": "midi", "status": REALTIME.get(b, f"0x{b:02X}")})
            i += 1
            continue
        if b == 0xF0:                                   # sysex up to and including F7
            j = i + 1
            while j < n and data[j] != 0xF7:
                j += 1
            events.append({"type": "midi", "status": "sysex", "length": min(j, n - 1) - i + 1,
                           "bytes": list(data[i:min(j + 1, i + 8)])})
            i = j + 1
            running = None
            continue
        if b >= 0xF1:                                   # system common: no running status
            running = None
            i += 1
            continue
        if b >= 0x80:                                   # channel status
            running = b
            i += 1
        elif running is None:                           # stray data byte
            i += 1
            continue
        kind, ch = running & 0xF0, (running & 0x0F) + 1
        need = DATA_LEN[kind]
        d = list(data[i:i + need])
        i += max(len(d), 1) if len(d) < need else need  # never stall on a short tail
        if len(d) < need:
            break
        ev = {"type": "midi", "status": CHANNEL_STATUS[kind], "channel": ch}
        if kind in (0x80, 0x90, 0xA0):
            ev.update(note=d[0], velocity=d[1])
            if kind == 0x90 and d[1] == 0:
                ev["status"] = "note_off"
        elif kind == 0xB0:
            ev.update(controller=d[0], value=d[1])
        elif kind == 0xC0:
            ev.update(program=d[0])
        elif kind == 0xD0:
            ev.update(value=d[0])
        elif kind == 0xE0:
            ev.update(value=(d[1] << 7 | d[0]) - 8192)
        events.append(ev)
    return events


class Adapter:
    def __init__(self, binary, rom):
        self.binary, self.rom = binary, rom
        self.sim = None
        self.midi_channel = 1
        self.pending = []      # parsed events not yet reported
        self.now_ms = 0        # emulated time since the simulator started
        self.held = {}         # key -> time it went down
        self.released = {}     # key -> time it last went up
        self.note_count = 0    # note-on messages sent since reset
        self.clock_count = 0   # MIDI clocks sent since reset
        self.msg_count = 0     # non-realtime messages since reset

    # -- fixtures ------------------------------------------------------
    def reset(self, fixture, params):
        if self.sim:
            self.sim.close()
        self.sim = Sim(self.binary, self.rom)
        self.midi_channel = 1
        self.pending, self.clock_count, self.msg_count = [], 0, 0
        self.now_ms, self.held, self.released, self.note_count = 0, {}, {}, 0
        if fixture == "cold-boot":
            return
        if fixture == "ready":
            self.wait(BOOT_MS)
        elif fixture == "recorded-part":
            self.wait(BOOT_MS)
            self.record_part(int(params.get("note", 60)), int(params.get("beats", 2)))
        else:
            raise ValueError(f"unknown fixture `{fixture}`")
        self.drain()
        self.pending = []

    def record_part(self, note, beats):
        """Record one note at the start of beat 1 of part 00, `beats` long."""
        s = self.sim
        s.press("REC"); s.wait(50); s.release("REC"); s.wait(50)
        s.press("PLAY"); s.wait(50); s.release("PLAY")
        # default count-down is 4 beats at 120 BPM: wait for "BEAT 001"
        for _ in range(60):
            s.wait(50)
            if "BEAT 001" in s.lcd()[1]:
                break
        s.midi([0x90, note, 100]); s.wait(250); s.midi([0x80, note, 0])
        s.wait(beats * 500 - 250 - 20)
        s.press("STOP"); s.wait(50); s.release("STOP"); s.wait(100)

    # -- inputs --------------------------------------------------------
    def wait(self, ms):
        self.sim.wait(ms)
        self.now_ms += ms

    def down(self, control):
        if control in BUTTONS:
            # The same key pressed again with no gap is one press to the scan.
            since = self.now_ms - self.released.get(control, -MIN_HOLD_MS)
            if since < MIN_HOLD_MS:
                self.wait(MIN_HOLD_MS - since)
            self.sim.press(BUTTONS[control])
            self.held[control] = self.now_ms
        elif control.startswith("midi_note"):
            self.sim.midi([0x90 | (self.midi_channel - 1), int(control[9:]), 100])
        elif control == "midi_start":
            self.sim.midi([0xFA])
        elif control == "midi_stop":
            self.sim.midi([0xFC])
        elif control == "midi_continue":
            self.sim.midi([0xFB])
        elif control == "midi_clock":
            self.sim.midi([0xF8])
        else:
            raise ValueError(f"cannot press `{control}`")

    def up(self, control):
        if control in BUTTONS:
            since = self.now_ms - self.held.pop(control, self.now_ms)
            if since < MIN_HOLD_MS:
                self.wait(MIN_HOLD_MS - since)
            self.sim.release(BUTTONS[control])
            self.released[control] = self.now_ms
        elif control.startswith("midi_note"):
            self.sim.midi([0x80 | (self.midi_channel - 1), int(control[9:]), 0])
        elif control.startswith("midi_"):
            pass
        else:
            raise ValueError(f"cannot release `{control}`")

    def set(self, control, value):
        v = int(value)
        if control == "midi_channel":
            self.midi_channel = v
        elif control.startswith("midi_cc"):
            self.sim.midi([0xB0 | (self.midi_channel - 1), int(control[7:]), v])
        elif control == "midi_program":
            self.sim.midi([0xC0 | (self.midi_channel - 1), v])
        elif control == "power":
            if v == 0:
                pass               # nothing to do until it comes back on
            else:
                self.sim.reset(cold=False)
                self.wait(BOOT_MS)
        else:
            raise ValueError(f"`{control}` can't be set")

    def advance(self, by):
        if "ms" not in by:
            raise ValueError("the MMT-8 adapter only counts time in ms")
        self.wait(by["ms"])
        self.drain()

    # -- observables ---------------------------------------------------
    def drain(self):
        raw = self.sim.midiout()
        if raw:
            evs = parse_midi(raw)
            self.clock_count += sum(1 for e in evs if e["status"] == "clock")
            self.msg_count += sum(1 for e in evs if e["status"] != "clock")
            self.note_count += sum(1 for e in evs if e["status"] == "note_on")
            self.pending.extend(evs)
            if len(self.pending) > MAX_PENDING:     # safety net: never grow without bound
                del self.pending[:-MAX_PENDING]

    def observe(self, path):
        self.drain()
        if path == "lcd.cursor":
            return self.sim.cursor()
        if path.startswith("lcd."):
            l1, l2 = self.sim.lcd()
            return {"lcd.line1": l1.strip(), "lcd.line2": l2.strip(),
                    "lcd.raw1": l1, "lcd.raw2": l2}.get(path)
        if path.startswith("led."):
            data, status = self.sim.leds()
            name = path[4:]
            if name.startswith("track."):
                n = int(name[6:])
                return not (data >> (n - 1) & 1)      # active low
            if name in STATUS_LEDS:
                return not (status >> STATUS_LEDS[name] & 1)
            return None
        if path == "midi.clock_count":
            return self.clock_count
        if path == "midi.message_count":
            return self.msg_count
        if path == "midi.note_count":
            return self.note_count
        return None

    def events(self):
        self.drain()
        evs, self.pending = self.pending, []
        return evs


def handle(ad, req):
    op = req.get("op")
    if op == "hello":
        return {"device": "mmt8", "profile": "mmt8", "protocol": 1,
                "implementation": "Alesis MMT-8 firmware v1.11 in sim/mmt8sim (emu8051)"}
    if op == "reset":
        ad.reset(req["fixture"], req.get("params", {}))
        return {}
    if op == "input":
        i = req["input"]
        kind = i["kind"]
        if kind == "down":
            ad.down(i["control"])
        elif kind == "up":
            ad.up(i["control"])
        elif kind == "set":
            ad.set(i["control"], i["value"])
        else:
            raise ValueError(f"unsupported input {i}")
        return {}
    if op == "advance":
        ad.advance(req["by"])
        return {}
    if op == "observe":
        return {"values": {p: ad.observe(p) for p in req["paths"]}}
    if op == "events":
        return {"events": ad.events()}
    if op == "bye":
        return {}
    raise ValueError(f"unknown op `{op}`")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sim", default=os.environ.get("MMT8SIM", DEFAULT_SIM))
    ap.add_argument("--rom", default=os.environ.get("MMT8_ROM", DEFAULT_ROM))
    args = ap.parse_args()
    ad = Adapter(args.sim, args.rom)
    for line in sys.stdin:
        if not line.strip():
            continue
        req = {}
        try:
            req = json.loads(line)
            resp = {"ok": True, **handle(ad, req)}
        except Exception as e:
            resp = {"ok": False, "error": f"{type(e).__name__}: {e}"}
        if "id" in req:
            resp["id"] = req["id"]
        print(json.dumps(resp), flush=True)
        if req.get("op") == "bye":
            break
    if ad.sim:
        ad.sim.close()


if __name__ == "__main__":
    main()
