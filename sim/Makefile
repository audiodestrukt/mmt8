CC = gcc
CFLAGS = -Wall -Wextra -O2 -g $(shell sdl2-config --cflags) $(shell pkg-config --cflags alsa)
LDFLAGS = $(shell sdl2-config --libs) -lSDL2_ttf $(shell pkg-config --libs alsa) -lm
SRCS = main.c core.c opcodes.c disasm.c mmt8_hw.c mmt8_gui.c mmt8_midi.c mmt8_keys.c
OBJS = $(SRCS:.c=.o)
TARGET = mmt8sim

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c emu8051.h mmt8_hw.h mmt8_gui.h mmt8_midi.h mmt8_keys.h
	$(CC) $(CFLAGS) -c -o $@ $<

ROM ?= ../firmware/alesis_mmt8_v111.bin
ARGS ?=

run: $(TARGET)
	./$(TARGET) $(ARGS) $(ROM)

headless: $(TARGET)
	./$(TARGET) --headless --lcd-log --midi-trace $(ARGS) $(ROM)

CORE_OBJS = core.o opcodes.o disasm.o

tests/cputest: tests/cputest.c $(CORE_OBJS) emu8051.h
	$(CC) -Wall -O1 -I. -o $@ tests/cputest.c $(CORE_OBJS)

test: tests/cputest
	./tests/cputest

clean:
	rm -f $(OBJS) $(TARGET) tests/cputest

.PHONY: clean test run headless
