#ifndef MMT8_MIDI_H
#define MMT8_MIDI_H

#include <stdint.h>

/*
 * Host MIDI bridge: exposes the simulated MMT-8's MIDI IN and MIDI OUT as
 * ALSA sequencer ports (client "MMT-8 Simulator"), so any ALSA/JACK/PipeWire
 * MIDI device or application can be connected with aconnect, qjackctl, etc.
 */

/* Open the sequencer client and create the ports. Returns 0 or -1. */
int  midi_init(void);
void midi_shutdown(void);

/* Connect an external port ("client:port", or a client name) to our MIDI IN,
 * or our MIDI OUT to an external port. Return 0 or -1. */
int  midi_connect_in(const char *addr);
int  midi_connect_out(const char *addr);

/* Feed one raw byte from the firmware's UART to MIDI OUT. Bytes are assembled
 * into complete messages before being sent. */
void midi_send_byte(uint8_t b);

/* Read pending input events and return the next raw byte for the firmware's
 * UART. Returns 1 and stores a byte, or 0 if nothing is pending. */
int  midi_recv_byte(uint8_t *b);

/* "client:port" of our own ports, for display. */
const char *midi_in_addr(void);
const char *midi_out_addr(void);

#endif /* MMT8_MIDI_H */
