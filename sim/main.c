#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>
#include <SDL2/SDL.h>
#include "emu8051.h"
#include "mmt8_hw.h"
#include "mmt8_gui.h"
#include "mmt8_midi.h"
#include "mmt8_keys.h"

#define ROM_SIZE 32768
#define XDATA_SIZE 65536

/* Machine cycles per second: 12 MHz crystal / 12 = 1 MHz, so 1 cycle = 1 us */
#define CYCLES_PER_SEC 1000000ULL
#define MAX_CYCLES_PER_FRAME 50000

/* Scripted key presses (for headless testing): press i starts at
 * PRESS_FIRST_MS + i*PRESS_SPACING_MS unless given as C,R@MS, and is held
 * for --hold ms. */
#define PRESS_FIRST_MS   1500
#define PRESS_SPACING_MS 400
#define MAX_PRESSES      16

static struct em8051 cpu;
static unsigned char rom_image[ROM_SIZE];   /* pristine copy, restored on power-on */
static volatile int running = 1;

static int   opt_headless;
static int   opt_no_midi;
static int   opt_trace;
static int   opt_lcd_log;
static int   opt_loopback;
static int   opt_script;
static long  opt_exit_after_ms = -1;
static long  opt_hold_ms = 100;
static const char *opt_midi_in;
static const char *opt_midi_out;
static const char *opt_screenshot;
static struct { int col, row; long at_ms; } presses[MAX_PRESSES];
static int num_presses;

static void on_sigint(int sig) { (void)sig; running = 0; }
static void log_lcd_changes(uint64_t emu_ms);

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options] [firmware.bin]\n"
        "  -H, --headless        run without the SDL window\n"
        "  -S, --script          headless, no wall clock: read commands from stdin (see README)\n"
        "  -n, --no-midi         do not create ALSA sequencer MIDI ports\n"
        "  -t, --midi-trace      print MIDI bytes in/out to stderr\n"
        "  -l, --lcd-log         print the LCD contents whenever they change\n"
        "  -L, --loopback        wire MIDI OUT straight back to MIDI IN (raw bytes, for the\n"
        "                        firmware self-test: hold LOOP+QUANT at power-on)\n"
        "  -i, --midi-in ADDR    connect ALSA port ADDR (e.g. 20:0) to the MMT-8 MIDI IN\n"
        "  -o, --midi-out ADDR   connect the MMT-8 MIDI OUT to ALSA port ADDR\n"
        "  -p, --press C,R[@MS]  press key matrix column C row R (scripted, may repeat;\n"
        "                        optional @MS = emulated start time in ms)\n"
        "  -d, --hold MS         how long each scripted press is held (default 100)\n"
        "  -x, --exit-after MS   exit after MS ms of emulated time and dump the LCD\n"
        "  -h, --help            this help\n",
        prog);
}

static int load_firmware(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open firmware: %s\n", path);
        return -1;
    }
    memset(rom_image, 0, ROM_SIZE);
    size_t n = fread(rom_image, 1, ROM_SIZE, f);
    fclose(f);
    if (n != ROM_SIZE) {
        fprintf(stderr, "Warning: firmware is %zu bytes (expected %d)\n", n, ROM_SIZE);
    }
    printf("Loaded %zu bytes of firmware from %s\n", n, path);
    return 0;
}

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

static void dump_lcd(uint64_t total_cycles)
{
    lcd_state_t *lcd = mmt8_get_lcd();
    char line0[17], line1[17];
    for (int i = 0; i < 16; i++) {
        line0[i] = (lcd->ddram[0][i] >= 0x20 && lcd->ddram[0][i] <= 0x7E)
            ? lcd->ddram[0][i] : ' ';
        line1[i] = (lcd->ddram[1][i] >= 0x20 && lcd->ddram[1][i] <= 0x7E)
            ? lcd->ddram[1][i] : ' ';
    }
    line0[16] = line1[16] = '\0';
    printf("LCD after %llu cycles:\n", (unsigned long long)total_cycles);
    printf("  Line 0: [%s]\n", line0);
    printf("  Line 1: [%s]\n", line1);
    printf("  PC=0x%04X LED_CTRL=0x%02X LED_DATA=0x%02X STATUS=0x%02X TRANSPORT=0x%02X\n",
           cpu.mPC, mmt8_get_led_control(), mmt8_get_led_data(),
           mmt8_get_status_latch(), mmt8_get_transport_state());
}

/* Cold (wipe RAM) or warm (keep RAM, like the memory battery) power-on.
 * Pressed keys survive, so power-on button combinations can be scripted. */
static void power_on(int cold)
{
    uint8_t keys[6];
    mmt8_get_key_matrix(keys);
    if (cold) {
        memset(cpu.mExtData, 0, XDATA_SIZE);
        memset(cpu.mUpperData, 0, 128);
    }
    reset(&cpu, cold);              /* a wipe also clears code memory */
    memcpy(cpu.mCodeMem, rom_image, ROM_SIZE);
    mmt8_hw_init();
    mmt8_hw_install(&cpu);
    mmt8_set_key_matrix(keys);
}

/* ---- Script mode -------------------------------------------------------
 *
 * Deterministic, faster than real time, no window and no ALSA. One command
 * per line on stdin, exactly one response line on stdout per command:
 *
 *   wait MS            run MS milliseconds of emulated time      -> ok
 *   press NAME|C,R     press a front-panel key (see mmt8_keys.c) or matrix position -> ok
 *   release NAME       release it                               -> ok
 *   midi HH HH ..      feed bytes into MIDI IN                   -> ok
 *   reset [cold|warm]  power-cycle; warm keeps RAM (default cold)-> ok
 *   lcd                -> lcd <32 hex bytes: line 1 then line 2> <cursor addr> <cursor on> <display on>
 *   leds               -> leds <led data hex> <status latch hex>
 *   midiout            -> midiout <hex bytes sent since last call>
 *   quit               -> ok, then exit
 *
 * Unknown commands or keys answer "err <message>".
 */
#define TXCAP_SIZE (1 << 20)
static uint8_t txcap[TXCAP_SIZE];
static size_t  txcap_len;
static uint64_t script_cycles;

static void run_cycles(uint64_t n)
{
    uint8_t b;
    for (uint64_t i = 0; i < n; i++) {
        tick(&cpu);
        mmt8_hw_tick(&cpu);
        while (mmt8_uart_tx_pop(&b)) {
            if (opt_trace) fprintf(stderr, "MIDI OUT %02X\n", b);
            if (opt_loopback) mmt8_uart_rx_push(b);
            if (txcap_len < TXCAP_SIZE) txcap[txcap_len++] = b;
        }
    }
    script_cycles += n;
}

static int script_mode(void)
{
    char line[4096];
    fflush(stdout);
    while (fflush(stdout), fgets(line, sizeof(line), stdin)) {
        char cmd[32] = "", arg[64] = "";
        char *rest = line;
        int n = sscanf(line, "%31s %63s", cmd, arg);
        if (n < 1) continue;
        if (strcmp(cmd, "wait") == 0) {
            long ms = atol(arg);
            if (n < 2 || ms < 0) { printf("err wait needs a non-negative ms count\n"); continue; }
            run_cycles((uint64_t)ms * 1000ULL);
            if (opt_lcd_log) log_lcd_changes(script_cycles / 1000);
            printf("ok\n");
        } else if (strcmp(cmd, "press") == 0 || strcmp(cmd, "release") == 0) {
            int col, row;
            const mmt8_key_t *k = n >= 2 ? mmt8_key_find(arg) : NULL;
            if (k) { col = k->col; row = k->row; }
            else if (n < 2 || sscanf(arg, "%d,%d", &col, &row) != 2 ||
                     col < 0 || col > 5 || row < 0 || row > 7) {
                printf("err unknown key '%s'\n", arg); continue;
            }
            if (cmd[0] == 'p') mmt8_key_press(col, row);
            else               mmt8_key_release(col, row);
            printf("ok\n");
        } else if (strcmp(cmd, "midi") == 0) {
            rest = line + 4;
            unsigned v; int used, count = 0;
            while (sscanf(rest, " %x%n", &v, &used) == 1) {
                mmt8_uart_rx_push((uint8_t)v);
                rest += used; count++;
            }
            printf("ok %d\n", count);
        } else if (strcmp(cmd, "reset") == 0) {
            power_on(!(n >= 2 && strcmp(arg, "warm") == 0));
            txcap_len = 0;
            printf("ok\n");
        } else if (strcmp(cmd, "lcd") == 0) {
            lcd_state_t *lcd = mmt8_get_lcd();
            printf("lcd ");
            for (int l = 0; l < 2; l++)
                for (int i = 0; i < 16; i++) printf("%02X", lcd->ddram[l][i]);
            printf(" %d %d %d\n", lcd->cursor_addr, lcd->cursor_on, lcd->display_on);
        } else if (strcmp(cmd, "leds") == 0) {
            printf("leds %02X %02X\n", mmt8_get_led_data(), mmt8_get_status_latch());
        } else if (strcmp(cmd, "midiout") == 0) {
            printf("midiout");
            for (size_t i = 0; i < txcap_len; i++) printf(" %02X", txcap[i]);
            printf("\n");
            txcap_len = 0;
        } else if (strcmp(cmd, "quit") == 0) {
            printf("ok\n");
            break;
        } else {
            printf("err unknown command '%s'\n", cmd);
        }
    }
    return 0;
}

/* Print the LCD whenever its visible contents change (--lcd-log). */
static void log_lcd_changes(uint64_t emu_ms)
{
    static char prev[2][17];
    lcd_state_t *lcd = mmt8_get_lcd();
    char cur[2][17];
    for (int line = 0; line < 2; line++) {
        for (int i = 0; i < 16; i++) {
            uint8_t ch = lcd->ddram[line][i];
            cur[line][i] = (ch >= 0x20 && ch <= 0x7E) ? ch : ' ';
        }
        cur[line][16] = '\0';
    }
    if (memcmp(prev, cur, sizeof(cur)) != 0) {
        fprintf(stderr, "LCD %6llu ms [%s] [%s] LED=%02X STATUS=%02X\n",
                (unsigned long long)emu_ms, cur[0], cur[1],
                mmt8_get_led_data(), mmt8_get_status_latch());
        memcpy(prev, cur, sizeof(cur));
    }
}

/* Move MIDI bytes between the emulated UART and the host ports. */
static void pump_midi(void)
{
    uint8_t b;
    while (mmt8_uart_tx_pop(&b)) {
        if (opt_trace) fprintf(stderr, "MIDI OUT %02X\n", b);
        if (opt_loopback) mmt8_uart_rx_push(b);
        if (!opt_no_midi) midi_send_byte(b);
    }
    if (!opt_no_midi) {
        while (midi_recv_byte(&b)) {
            if (opt_trace) fprintf(stderr, "MIDI IN  %02X\n", b);
            mmt8_uart_rx_push(b);
        }
    }
}

/* Apply scripted key presses according to emulated time. */
static void update_scripted_keys(uint64_t emu_ms)
{
    static int state[MAX_PRESSES];   /* 0 = not yet, 1 = down, 2 = done */
    for (int i = 0; i < num_presses; i++) {
        uint64_t t0 = presses[i].at_ms >= 0 ? (uint64_t)presses[i].at_ms
                      : PRESS_FIRST_MS + (uint64_t)i * PRESS_SPACING_MS;
        if (state[i] == 0 && emu_ms >= t0) {
            mmt8_key_press(presses[i].col, presses[i].row);
            state[i] = 1;
        } else if (state[i] == 1 && emu_ms >= t0 + (uint64_t)opt_hold_ms) {
            mmt8_key_release(presses[i].col, presses[i].row);
            state[i] = 2;
        }
    }
}

static int parse_args(int argc, char *argv[], const char **rom_path)
{
    static const struct option longopts[] = {
        {"headless",   no_argument,       0, 'H'},
        {"script",     no_argument,       0, 'S'},
        {"no-midi",    no_argument,       0, 'n'},
        {"midi-trace", no_argument,       0, 't'},
        {"lcd-log",    no_argument,       0, 'l'},
        {"loopback",   no_argument,       0, 'L'},
        {"midi-in",    required_argument, 0, 'i'},
        {"midi-out",   required_argument, 0, 'o'},
        {"press",      required_argument, 0, 'p'},
        {"exit-after", required_argument, 0, 'x'},
        {"hold",       required_argument, 0, 'd'},
        {"screenshot", required_argument, 0, 's'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "HSntlLi:o:p:x:d:s:h", longopts, NULL)) != -1) {
        switch (c) {
        case 'H': opt_headless = 1; break;
        case 'S': opt_script = 1; opt_headless = 1; opt_no_midi = 1; break;
        case 'n': opt_no_midi = 1; break;
        case 't': opt_trace = 1; break;
        case 'l': opt_lcd_log = 1; break;
        case 'L': opt_loopback = 1; break;
        case 'i': opt_midi_in = optarg; break;
        case 'o': opt_midi_out = optarg; break;
        case 'p': {
            int col, row;
            long at = -1;
            int n = sscanf(optarg, "%d,%d@%ld", &col, &row, &at);
            if (n < 2 || col < 0 || col > 5 || row < 0 || row > 7) {
                fprintf(stderr, "Bad --press '%s' (want COL,ROW[@MS] with col 0-5, row 0-7)\n", optarg);
                return -1;
            }
            if (num_presses < MAX_PRESSES) {
                presses[num_presses].col = col;
                presses[num_presses].row = row;
                presses[num_presses].at_ms = at;
                num_presses++;
            }
            break;
        }
        case 'x': opt_exit_after_ms = atol(optarg); break;
        case 'd': opt_hold_ms = atol(optarg); break;
        case 's': opt_screenshot = optarg; break;
        case 'h': usage(argv[0]); exit(0);
        default:  usage(argv[0]); return -1;
        }
    }
    if (optind < argc)
        *rom_path = argv[optind];
    return 0;
}

int main(int argc, char *argv[])
{
    const char *rom_path = "../firmware/alesis_mmt8_v111.bin";
    if (parse_args(argc, argv, &rom_path) < 0)
        return 1;

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    /* Initialize GUI */
    if (!opt_headless && gui_init() < 0) {
        fprintf(stderr, "GUI init failed\n");
        return 1;
    }

    /* Initialize hardware emulation */
    mmt8_hw_init();

    /* Host MIDI ports */
    if (!opt_no_midi) {
        if (midi_init() < 0) {
            fprintf(stderr, "MIDI disabled (ALSA sequencer unavailable)\n");
            opt_no_midi = 1;
        } else {
            printf("ALSA MIDI ports: IN=%s OUT=%s (client \"MMT-8 Simulator\")\n",
                   midi_in_addr(), midi_out_addr());
            if (opt_midi_in && midi_connect_in(opt_midi_in) == 0)
                printf("Connected %s -> MIDI IN\n", opt_midi_in);
            if (opt_midi_out && midi_connect_out(opt_midi_out) == 0)
                printf("Connected MIDI OUT -> %s\n", opt_midi_out);
        }
    }

    /* Initialize emu8051 */
    memset(&cpu, 0, sizeof(cpu));
    cpu.mCodeMem = malloc(ROM_SIZE);
    cpu.mCodeMemMaxIdx = ROM_SIZE - 1;
    cpu.mExtData = malloc(XDATA_SIZE);
    cpu.mExtDataMaxIdx = XDATA_SIZE - 1;
    cpu.mUpperData = malloc(128);

    if (!cpu.mCodeMem || !cpu.mExtData || !cpu.mUpperData) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    /* Load firmware */
    if (load_firmware(rom_path) < 0)
        return 1;

    power_on(1);

    if (opt_script)
        return script_mode();

    printf("MMT-8 Simulator starting...%s\n", opt_headless ? " (headless)" : "");
    printf("CPU reset, PC=0x%04X\n", cpu.mPC);

    uint64_t last_time = now_us();
    uint64_t total_cycles = 0;
    int lcd_dumped = 0;

    while (running) {
        uint64_t now = now_us();
        uint64_t elapsed_us = now - last_time;
        last_time = now;

        /* 1 machine cycle per microsecond */
        int cycles = (int)elapsed_us;
        if (cycles > MAX_CYCLES_PER_FRAME) cycles = MAX_CYCLES_PER_FRAME;

        for (int i = 0; i < cycles; i++) {
            tick(&cpu);
            mmt8_hw_tick(&cpu);
            total_cycles++;
        }

        uint64_t emu_ms = total_cycles / 1000;
        update_scripted_keys(emu_ms);
        pump_midi();
        if (opt_lcd_log)
            log_lcd_changes(emu_ms);

        /* Debug: dump LCD contents once after enough cycles */
        if (!lcd_dumped && total_cycles > 500000) {
            dump_lcd(total_cycles);
            lcd_dumped = 1;
        }

        if (opt_exit_after_ms >= 0 && emu_ms >= (uint64_t)opt_exit_after_ms)
            running = 0;

        if (!opt_headless) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) {
                    running = 0;
                } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                    running = 0;
                } else {
                    gui_handle_event(&ev);
                }
            }
            gui_render(&cpu);
            SDL_Delay(1);
        } else {
            struct timespec ts = {0, 1000000};   /* 1 ms */
            nanosleep(&ts, NULL);
        }
    }

    if (opt_headless || opt_exit_after_ms >= 0)
        dump_lcd(total_cycles);

    const mmt8_uart_stats_t *st = mmt8_uart_stats();
    printf("UART: rx=%lu (dropped %lu, SBUF reads %lu) tx=%lu\n",
           st->rx_bytes, st->rx_dropped, st->sbuf_reads, st->tx_bytes);

    if (opt_screenshot && !opt_headless && gui_screenshot(opt_screenshot) == 0)
        printf("Saved screenshot to %s\n", opt_screenshot);

    /* Cleanup */
    if (!opt_no_midi) midi_shutdown();
    if (!opt_headless) gui_shutdown();
    free(cpu.mCodeMem);
    free(cpu.mExtData);
    free(cpu.mUpperData);

    return 0;
}
