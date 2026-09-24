#include <string.h>
#include <stdio.h>
#include "emu8051.h"
#include "mmt8_hw.h"

/* ---- Hardware latch state ---- */
static uint8_t led_control;      /* 0xFF00 */
static uint8_t led_data;         /* 0xFF02 */
static uint8_t status_latch;     /* 0xFF04 */
static uint8_t key_column_sel;   /* 0xFF06 */
static uint8_t transport_state;  /* 0xFF0E */
static uint8_t beat_divider;     /* 0xFF0F */
static uint8_t click_enable;     /* 0xFF1A */

/* ---- LCD ---- */
static lcd_state_t lcd;

/* ---- Keyboard matrix (6 columns x 8 rows) ---- */
/* Each element is a bitmask of pressed rows for that column */
static uint8_t key_matrix[6];

/* ---- UART ---- */
#define UART_FIFO_SIZE 4096
typedef struct {
    uint8_t  buf[UART_FIFO_SIZE];
    unsigned head, tail;           /* push at head, pop at tail */
} fifo_t;

static fifo_t   uart_rx_fifo;     /* host -> firmware */
static fifo_t   uart_tx_fifo;     /* firmware -> host */
static int      uart_tx_cycles;   /* cycles left in the byte being sent, 0 = idle */
static uint8_t  uart_tx_shift;    /* byte being sent */
static int      uart_rx_cycles;   /* cycles left in the byte being received */
static uint8_t  uart_rx_shift;    /* byte being received */
static int      uart_rx_holding;  /* byte fully received, waiting for RI to clear */
static uint8_t  uart_rx_sbuf;     /* receive-side SBUF (separate from TX SBUF) */
static mmt8_uart_stats_t uart_stats;

static int fifo_push(fifo_t *f, uint8_t b)
{
    unsigned next = (f->head + 1) % UART_FIFO_SIZE;
    if (next == f->tail)
        return -1;
    f->buf[f->head] = b;
    f->head = next;
    return 0;
}

static int fifo_pop(fifo_t *f, uint8_t *b)
{
    if (f->head == f->tail)
        return 0;
    *b = f->buf[f->tail];
    f->tail = (f->tail + 1) % UART_FIFO_SIZE;
    return 1;
}

/* SBUF read: the firmware reads the receive register (never its own TX byte). */
static uint8_t uart_sbuf_read(struct em8051 *cpu, uint8_t reg)
{
    (void)cpu; (void)reg;
    uart_stats.sbuf_reads++;
    return uart_rx_sbuf;
}

/* SBUF write: start transmitting. emu8051 has already stored the value. */
static void uart_sbuf_write(struct em8051 *cpu, uint8_t reg)
{
    (void)reg;
    uart_tx_shift  = cpu->mSFR[REG_SBUF];
    uart_tx_cycles = MMT8_UART_BYTE_CYCLES;
}

/* SCON write (including SETB/CLR on TI/RI): the 8051 serial interrupt is
 * level-sensitive on TI|RI, but emu8051 models it as an edge flag. Re-arm the
 * flag whenever either bit is left set, so e.g. the firmware's software
 * "SETB TI" kick-start, and an ISR that clears RI while TI is pending, both
 * behave as on real hardware. */
static void uart_scon_write(struct em8051 *cpu, uint8_t reg)
{
    (void)reg;
    if (cpu->mSFR[REG_SCON] & (SCONMASK_TI | SCONMASK_RI))
        cpu->serial_interrupt_trigger = 1;
}

/* ---- LCD helpers ---- */

static void lcd_write_command(uint8_t cmd)
{
    if (cmd == 0x01) {
        /* Clear display */
        memset(lcd.ddram, ' ', sizeof(lcd.ddram));
        lcd.cursor_addr = 0;
    } else if (cmd == 0x02) {
        /* Return home */
        lcd.cursor_addr = 0;
    } else if ((cmd & 0xFC) == 0x04) {
        /* Entry mode set */
        lcd.entry_increment = (cmd & 0x02) ? 1 : 0;
    } else if ((cmd & 0xF8) == 0x08) {
        /* Display on/off control */
        lcd.display_on = (cmd & 0x04) ? 1 : 0;
        lcd.cursor_on  = (cmd & 0x02) ? 1 : 0;
        lcd.blink_on   = (cmd & 0x01) ? 1 : 0;
    } else if ((cmd & 0xE0) == 0x20) {
        /* Function set (0x38 = 8-bit, 2 lines) — just accept it */
    } else if (cmd & 0x80) {
        /* Set DDRAM address */
        lcd.cursor_addr = cmd & 0x7F;
    }
    /* Other commands (shift, CGRAM) ignored for now */
}

static void lcd_write_data(uint8_t data)
{
    int line, pos;

    if (lcd.cursor_addr >= 0x40) {
        line = 1;
        pos = lcd.cursor_addr - 0x40;
    } else {
        line = 0;
        pos = lcd.cursor_addr;
    }

    if (pos >= 0 && pos < 40) {
        lcd.ddram[line][pos] = data;
    }

    if (lcd.entry_increment)
        lcd.cursor_addr++;
    else
        lcd.cursor_addr--;

    lcd.cursor_addr &= 0x7F;
}

/* ---- Public API ---- */

void mmt8_hw_init(void)
{
    led_control = 0;
    led_data = 0;
    status_latch = 0;
    key_column_sel = 0xFF;
    transport_state = 0;
    beat_divider = 0;
    click_enable = 0;

    memset(&lcd, 0, sizeof(lcd));
    memset(lcd.ddram, ' ', sizeof(lcd.ddram));
    lcd.entry_increment = 1;

    memset(key_matrix, 0, sizeof(key_matrix));

    memset(&uart_rx_fifo, 0, sizeof(uart_rx_fifo));
    memset(&uart_tx_fifo, 0, sizeof(uart_tx_fifo));
    uart_tx_cycles = 0;
    uart_rx_cycles = 0;
    uart_rx_holding = 0;
    uart_rx_sbuf = 0;
    memset(&uart_stats, 0, sizeof(uart_stats));
}

void mmt8_hw_install(struct em8051 *cpu)
{
    cpu->xread = mmt8_xdata_read;
    cpu->xwrite = mmt8_xdata_write;
    cpu->sfrread[REG_P1]    = mmt8_p1_read;
    cpu->sfrread[REG_SBUF]  = uart_sbuf_read;
    cpu->sfrwrite[REG_SBUF] = uart_sbuf_write;
    cpu->sfrwrite[REG_SCON] = uart_scon_write;
}

void mmt8_hw_tick(struct em8051 *cpu)
{
    /* Transmit side */
    if (uart_tx_cycles > 0 && --uart_tx_cycles == 0) {
        fifo_push(&uart_tx_fifo, uart_tx_shift);
        uart_stats.tx_bytes++;
        if (uart_tx_shift < 0xF8) uart_stats.tx_msg_bytes++;
        cpu->mSFR[REG_SCON] |= SCONMASK_TI;
        cpu->serial_interrupt_trigger = 1;
    }

    /* Receive side: shift in the next byte if the line is idle */
    if (uart_rx_cycles > 0) {
        if (--uart_rx_cycles == 0)
            uart_rx_holding = 1;
    } else if (!uart_rx_holding && fifo_pop(&uart_rx_fifo, &uart_rx_shift)) {
        uart_rx_cycles = MMT8_UART_BYTE_CYCLES;
    }

    /* Deliver a completed byte once the firmware has consumed the previous one.
     * (Real hardware would drop it if RI were still set; holding it is a little
     * more forgiving and never matters when the ISR keeps up.) */
    if (uart_rx_holding &&
        (cpu->mSFR[REG_SCON] & SCONMASK_REN) &&
        !(cpu->mSFR[REG_SCON] & SCONMASK_RI)) {
        uart_rx_sbuf = uart_rx_shift;
        uart_rx_holding = 0;
        uart_stats.rx_bytes++;
        if (uart_rx_sbuf < 0xF8) uart_stats.rx_msg_bytes++;
        cpu->mSFR[REG_SCON] |= SCONMASK_RI;
        cpu->serial_interrupt_trigger = 1;
    }
}

int mmt8_uart_rx_push(uint8_t b)
{
    if (fifo_push(&uart_rx_fifo, b) < 0) {
        uart_stats.rx_dropped++;
        return -1;
    }
    return 0;
}

int mmt8_uart_tx_pop(uint8_t *b)
{
    return fifo_pop(&uart_tx_fifo, b);
}

const mmt8_uart_stats_t *mmt8_uart_stats(void)
{
    return &uart_stats;
}

void mmt8_xdata_write(struct em8051 *cpu, uint16_t addr, uint8_t val)
{
    if (addr < 0xFF00) {
        cpu->mExtData[addr] = val;
        return;
    }
    switch (addr) {
        case 0xFF00: led_control = val; break;
        case 0xFF02: led_data = val; break;
        case 0xFF04: status_latch = val; break;
        case 0xFF06: key_column_sel = val; break;
        case 0xFF08: lcd_write_command(val); break;
        case 0xFF09: lcd_write_data(val); break;
        case 0xFF0E: transport_state = val; break;
        case 0xFF0F: beat_divider = val; break;
        case 0xFF1A: click_enable = val; break;
        default:     cpu->mExtData[addr] = val; break;
    }
}

uint8_t mmt8_xdata_read(struct em8051 *cpu, uint16_t addr)
{
    if (addr < 0xFF00)
        return cpu->mExtData[addr];
    switch (addr) {
        case 0xFF04: return status_latch;
        case 0xFF0E: return transport_state;
        case 0xFF0F: return beat_divider;
        case 0xFF1A: return click_enable;
        default:     return cpu->mExtData[addr];
    }
}

uint8_t mmt8_p1_read(struct em8051 *cpu, uint8_t reg)
{
    (void)cpu;
    (void)reg;
    uint8_t result = 0xFF;
    for (int col = 0; col < 6; col++) {
        if (!(key_column_sel & (1 << col))) {
            result &= ~key_matrix[col];
        }
    }
    return result;
}

lcd_state_t *mmt8_get_lcd(void) { return &lcd; }
uint8_t mmt8_get_led_control(void) { return led_control; }
uint8_t mmt8_get_led_data(void)    { return led_data; }
uint8_t mmt8_get_status_latch(void) { return status_latch; }
uint8_t mmt8_get_transport_state(void) { return transport_state; }

void mmt8_key_press(int col, int row)
{
    if (col >= 0 && col < 6 && row >= 0 && row < 8)
        key_matrix[col] |= (1 << row);
}

void mmt8_key_release(int col, int row)
{
    if (col >= 0 && col < 6 && row >= 0 && row < 8)
        key_matrix[col] &= ~(1 << row);
}

void mmt8_get_key_matrix(uint8_t out[6])     { memcpy(out, key_matrix, 6); }
void mmt8_set_key_matrix(const uint8_t in[6]) { memcpy(key_matrix, in, 6); }
