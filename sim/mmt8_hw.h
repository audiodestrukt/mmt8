#ifndef MMT8_HW_H
#define MMT8_HW_H

#include "emu8051.h"

/* HD44780 LCD state */
typedef struct {
    uint8_t ddram[2][40];   /* 2 lines x 40 chars */
    uint8_t cursor_addr;    /* current DDRAM address */
    int     display_on;
    int     cursor_on;
    int     blink_on;
    int     entry_increment; /* 1 = increment, 0 = decrement */
} lcd_state_t;

/* Initialize hardware emulation state */
void mmt8_hw_init(void);

/* Install callbacks on an em8051 instance */
void mmt8_hw_install(struct em8051 *cpu);

/* XDATA read/write callbacks */
uint8_t mmt8_xdata_read(struct em8051 *cpu, uint16_t addr);
void    mmt8_xdata_write(struct em8051 *cpu, uint16_t addr, uint8_t val);

/* P1 SFR read callback (keyboard row input) */
uint8_t mmt8_p1_read(struct em8051 *cpu, uint8_t reg);

/* Access hardware state from GUI */
lcd_state_t *mmt8_get_lcd(void);
uint8_t      mmt8_get_led_control(void);
uint8_t      mmt8_get_led_data(void);
uint8_t      mmt8_get_status_latch(void);
uint8_t      mmt8_get_transport_state(void);

/* Keyboard matrix: set/clear a key press (col 0-5, row 0-7) */
void mmt8_key_press(int col, int row);
void mmt8_key_release(int col, int row);

/* ---- UART (MIDI) emulation ----
 *
 * The 80C31 UART runs in mode 1 at 31.25 kbaud (Timer 1 auto-reload, TH1=0xFF).
 * One byte (start + 8 data + stop) therefore takes 320 machine cycles.
 * TX: a write to SBUF starts a 320-cycle transmission; when it completes the
 *     byte is queued for the host, TI is set and the serial interrupt raised.
 * RX: bytes queued by the host are shifted in one at a time (320 cycles each),
 *     then loaded into the receive SBUF with RI set, as long as REN is set and
 *     the previous byte has been read (RI clear).
 */
#define MMT8_UART_BYTE_CYCLES 320

/* Advance the UART by one machine cycle. Call once per tick(). */
void mmt8_hw_tick(struct em8051 *cpu);

/* Host -> firmware (MIDI IN). Returns 0 on success, -1 if the FIFO is full. */
int mmt8_uart_rx_push(uint8_t b);

/* Firmware -> host (MIDI OUT). Returns 1 and stores a byte, or 0 if none. */
int mmt8_uart_tx_pop(uint8_t *b);

typedef struct {
    unsigned long rx_bytes;      /* bytes delivered to the firmware's SBUF */
    unsigned long rx_dropped;    /* host bytes dropped because the FIFO was full */
    unsigned long tx_bytes;      /* bytes transmitted by the firmware */
    unsigned long sbuf_reads;    /* firmware reads of SBUF (ISR consumed a byte) */
} mmt8_uart_stats_t;

const mmt8_uart_stats_t *mmt8_uart_stats(void);

#endif /* MMT8_HW_H */
