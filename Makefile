# Top-level convenience wrapper around sim/Makefile.
#   make            build the simulator
#   make run        build and launch the GUI simulator
#   make headless   run without a window, logging LCD changes and MIDI bytes
#   make test       run the emu8051 core differential test
#   make spec       run the panelspec behaviour suite (specs/mmt8) against the firmware
#   make clean
# Extra simulator flags: make run ARGS="--midi-out 128:0"

.PHONY: all run headless test spec clean

all:
	$(MAKE) -C sim

run headless test clean:
	$(MAKE) -C sim $@ ARGS="$(ARGS)"

# Behaviour specs from the manual, run against the firmware in the simulator.
# Needs the panelspec CLI: cargo install --git https://github.com/audiodestrukt/hexatrack panelspec
spec: all
	panelspec lint specs/mmt8
	panelspec run -a "python3 sim/adapters/mmt8_sim.py" specs/mmt8 $(SPECARGS)

# Rebuild the firmware from its assembly source and check it matches the ROM.
#   make firmware        assemble firmware/mmt8.asm -> build/mmt8.hex + build/mmt8.bin
#   make roundtrip       regenerate the source from the dis51 listing, assemble, byte-compare
AS31 = tools/as31/as31

$(AS31):
	scripts/get-as31.sh

firmware: $(AS31)
	mkdir -p build
	$(AS31) -Fhex firmware/mmt8 >/dev/null
	mv firmware/mmt8.obj build/mmt8.hex
	python3 -c "import sys; m=bytearray(32768); \
	  [m.__setitem__(slice(int(l[3:7],16), int(l[3:7],16)+int(l[1:3],16)), bytes.fromhex(l[9:9+2*int(l[1:3],16)])) \
	   for l in open('build/mmt8.hex') if l.startswith(':') and l[7:9]=='00']; open('build/mmt8.bin','wb').write(m)"
	cmp build/mmt8.bin firmware/alesis_mmt8_v111.bin && echo "build/mmt8.bin is byte-identical to the original ROM"

roundtrip: $(AS31)
	python3 scripts/listing2asm.py alesis_mmt8_v111.asm > firmware/mmt8.asm
	$(MAKE) firmware

.PHONY: firmware roundtrip
