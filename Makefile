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
