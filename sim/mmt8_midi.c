#include <stdio.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include "mmt8_midi.h"

#define IN_QUEUE_SIZE 8192
#define EVENT_BUF_SIZE 1024   /* max SysEx chunk assembled per ALSA event */

static snd_seq_t        *seq;
static int               port_in  = -1;   /* writable by others: MIDI IN  */
static int               port_out = -1;   /* readable by others: MIDI OUT */
static snd_midi_event_t *encoder;         /* raw bytes -> seq events (OUT) */
static snd_midi_event_t *decoder;         /* seq events -> raw bytes (IN)  */
static char              in_addr_str[32], out_addr_str[32];

/* Bytes decoded from input events, waiting for the UART */
static uint8_t  in_queue[IN_QUEUE_SIZE];
static unsigned in_head, in_tail;

static void in_queue_push(uint8_t b)
{
    unsigned next = (in_head + 1) % IN_QUEUE_SIZE;
    if (next == in_tail)
        return;                     /* overflow: drop */
    in_queue[in_head] = b;
    in_head = next;
}

int midi_init(void)
{
    int err = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (err < 0) {
        fprintf(stderr, "ALSA: snd_seq_open failed: %s\n", snd_strerror(err));
        seq = NULL;
        return -1;
    }
    snd_seq_set_client_name(seq, "MMT-8 Simulator");

    port_in = snd_seq_create_simple_port(seq, "MMT-8 MIDI IN",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    port_out = snd_seq_create_simple_port(seq, "MMT-8 MIDI OUT",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port_in < 0 || port_out < 0) {
        fprintf(stderr, "ALSA: could not create sequencer ports\n");
        midi_shutdown();
        return -1;
    }

    if (snd_midi_event_new(EVENT_BUF_SIZE, &encoder) < 0 ||
        snd_midi_event_new(EVENT_BUF_SIZE, &decoder) < 0) {
        fprintf(stderr, "ALSA: snd_midi_event_new failed\n");
        midi_shutdown();
        return -1;
    }
    /* Always emit explicit status bytes toward the firmware; the 8051 code
     * copes with running status but there is no reason to exercise it. */
    snd_midi_event_no_status(decoder, 1);

    int client = snd_seq_client_id(seq);
    snprintf(in_addr_str,  sizeof(in_addr_str),  "%d:%d", client, port_in);
    snprintf(out_addr_str, sizeof(out_addr_str), "%d:%d", client, port_out);
    return 0;
}

void midi_shutdown(void)
{
    if (encoder) { snd_midi_event_free(encoder); encoder = NULL; }
    if (decoder) { snd_midi_event_free(decoder); decoder = NULL; }
    if (seq)     { snd_seq_close(seq); seq = NULL; }
    port_in = port_out = -1;
}

static int parse_addr(const char *addr, snd_seq_addr_t *out)
{
    int err = snd_seq_parse_address(seq, out, addr);
    if (err < 0)
        fprintf(stderr, "ALSA: cannot resolve port '%s': %s\n", addr, snd_strerror(err));
    return err;
}

int midi_connect_in(const char *addr)
{
    snd_seq_addr_t a;
    if (!seq || parse_addr(addr, &a) < 0) return -1;
    int err = snd_seq_connect_from(seq, port_in, a.client, a.port);
    if (err < 0) {
        fprintf(stderr, "ALSA: connect %d:%d -> MIDI IN failed: %s\n",
                a.client, a.port, snd_strerror(err));
        return -1;
    }
    return 0;
}

int midi_connect_out(const char *addr)
{
    snd_seq_addr_t a;
    if (!seq || parse_addr(addr, &a) < 0) return -1;
    int err = snd_seq_connect_to(seq, port_out, a.client, a.port);
    if (err < 0) {
        fprintf(stderr, "ALSA: connect MIDI OUT -> %d:%d failed: %s\n",
                a.client, a.port, snd_strerror(err));
        return -1;
    }
    return 0;
}

void midi_send_byte(uint8_t b)
{
    if (!seq) return;
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    long r = snd_midi_event_encode_byte(encoder, b, &ev);
    if (r != 1)
        return;                         /* message not complete yet */
    snd_seq_ev_set_source(&ev, port_out);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_event_output_direct(seq, &ev);
}

static void pump_input(void)
{
    snd_seq_event_t *ev;
    while (snd_seq_event_input(seq, &ev) >= 0) {
        if (ev->type == SND_SEQ_EVENT_SYSEX) {
            const uint8_t *p = ev->data.ext.ptr;
            for (unsigned i = 0; i < ev->data.ext.len; i++)
                in_queue_push(p[i]);
        } else {
            uint8_t buf[16];
            long n = snd_midi_event_decode(decoder, buf, sizeof(buf), ev);
            for (long i = 0; i < n; i++)
                in_queue_push(buf[i]);
            /* n < 0: not a MIDI event (port subscription etc.) — ignore */
        }
    }
}

int midi_recv_byte(uint8_t *b)
{
    if (!seq) return 0;
    if (in_head == in_tail)
        pump_input();
    if (in_head == in_tail)
        return 0;
    *b = in_queue[in_tail];
    in_tail = (in_tail + 1) % IN_QUEUE_SIZE;
    return 1;
}

const char *midi_in_addr(void)  { return in_addr_str; }
const char *midi_out_addr(void) { return out_addr_str; }
