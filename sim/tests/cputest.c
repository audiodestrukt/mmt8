/*
 * Differential test of the patched emu8051 core against a reference model of
 * the 8051 instruction semantics (flags, BCD, MOV/XCH addressing modes, ...).
 *
 * Build & run:  make test        (from sim/)
 *
 * It exists because the upstream core had several silent bugs that only
 * showed up as bizarre firmware behaviour (corrupted part numbers, phantom
 * MIDI events): wrong auxiliary-carry, XCHD not writing back, DA A carry,
 * and MOV direct,@Ri with source and destination swapped.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "emu8051.h"
#define C_ 0x80
#define AC_ 0x40
#define OV_ 0x04
static struct em8051 cpu;
static unsigned char code[32768], xdata[65536], upper[128];
static void run(const uint8_t *ins, int len, uint8_t a, uint8_t b, uint8_t psw, uint8_t r0val, uint8_t iram_r0)
{
    reset(&cpu, 0);
    memset(code, 0, sizeof code);
    memcpy(code, ins, len);
    code[len] = 0x80; code[len+1] = 0xFE;  /* SJMP $ */
    code[len+2] = 0x80; code[len+3] = 0xFE;  /* SJMP $ at a +2 branch target */
    cpu.mSFR[REG_ACC] = a; cpu.mSFR[REG_B] = b; cpu.mSFR[REG_PSW] = psw;
    cpu.mLowerData[0] = r0val;          /* R0 (bank 0) */
    cpu.mLowerData[r0val] = iram_r0;    /* @R0 target */
    for (int i = 0; i < 12; i++) tick(&cpu);
}
static int fails, tests;
static void check(const char *name, int cond, const char *fmt, ...)
{
    tests++;
    if (!cond) { fails++; printf("FAIL %s: ", name); va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n"); }
}
#include <stdarg.h>
int main(void)
{
    cpu.mCodeMem = code; cpu.mCodeMemMaxIdx = 32767;
    cpu.mExtData = xdata; cpu.mExtDataMaxIdx = 65535;
    cpu.mUpperData = upper;
    reset(&cpu, 1);
    srand(1);
    for (int n = 0; n < 20000; n++) {
        uint8_t a = rand(), b = rand(), cin = rand() & 1;
        uint8_t psw = (cin ? C_ : 0) | ((rand() & 1) ? AC_ : 0);
        uint8_t ins[4];
        int r, exp_c, exp_ac, exp_ov;
        /* ADD A,#b */
        ins[0] = 0x24; ins[1] = b; run(ins, 2, a, 0, psw, 1, 0);
        r = a + b; exp_c = r > 0xFF; exp_ac = ((a & 0xF) + (b & 0xF)) > 0xF;
        exp_ov = (((a ^ r) & (b ^ r)) & 0x80) != 0;
        check("ADD", cpu.mSFR[REG_ACC] == (r & 0xFF) && !!(cpu.mSFR[REG_PSW] & C_) == exp_c && !!(cpu.mSFR[REG_PSW] & AC_) == exp_ac && !!(cpu.mSFR[REG_PSW] & OV_) == exp_ov,
              "a=%02X b=%02X -> acc=%02X psw=%02X (exp acc=%02X C=%d AC=%d OV=%d)", a, b, cpu.mSFR[REG_ACC], cpu.mSFR[REG_PSW], r & 0xFF, exp_c, exp_ac, exp_ov);
        /* ADDC A,#b */
        ins[0] = 0x34; ins[1] = b; run(ins, 2, a, 0, psw, 1, 0);
        r = a + b + cin; exp_c = r > 0xFF; exp_ac = ((a & 0xF) + (b & 0xF) + cin) > 0xF;
        exp_ov = (((a ^ r) & (b ^ r)) & 0x80) != 0;
        check("ADDC", cpu.mSFR[REG_ACC] == (r & 0xFF) && !!(cpu.mSFR[REG_PSW] & C_) == exp_c && !!(cpu.mSFR[REG_PSW] & AC_) == exp_ac && !!(cpu.mSFR[REG_PSW] & OV_) == exp_ov,
              "a=%02X b=%02X cin=%d -> acc=%02X psw=%02X (exp acc=%02X C=%d AC=%d OV=%d)", a, b, cin, cpu.mSFR[REG_ACC], cpu.mSFR[REG_PSW], r & 0xFF, exp_c, exp_ac, exp_ov);
        /* SUBB A,#b */
        ins[0] = 0x94; ins[1] = b; run(ins, 2, a, 0, psw, 1, 0);
        r = (int)a - b - cin; exp_c = r < 0; exp_ac = ((int)(a & 0xF) - (b & 0xF) - cin) < 0;
        exp_ov = (((a ^ b) & (a ^ r)) & 0x80) != 0;
        check("SUBB", cpu.mSFR[REG_ACC] == (r & 0xFF) && !!(cpu.mSFR[REG_PSW] & C_) == exp_c && !!(cpu.mSFR[REG_PSW] & AC_) == exp_ac && !!(cpu.mSFR[REG_PSW] & OV_) == exp_ov,
              "a=%02X b=%02X cin=%d -> acc=%02X psw=%02X (exp acc=%02X C=%d AC=%d OV=%d)", a, b, cin, cpu.mSFR[REG_ACC], cpu.mSFR[REG_PSW], r & 0xFF, exp_c, exp_ac, exp_ov);
        /* SUBB A,@R0 (indirect path) */
        ins[0] = 0x96; run(ins, 1, a, 0, psw, 0x30, b);
        check("SUBB@R0", cpu.mSFR[REG_ACC] == (r & 0xFF) && !!(cpu.mSFR[REG_PSW] & C_) == exp_c,
              "a=%02X b=%02X cin=%d -> acc=%02X psw=%02X", a, b, cin, cpu.mSFR[REG_ACC], cpu.mSFR[REG_PSW]);
        /* MUL AB */
        ins[0] = 0xA4; run(ins, 1, a, b, psw, 1, 0);
        r = a * b;
        check("MUL", cpu.mSFR[REG_ACC] == (r & 0xFF) && cpu.mSFR[REG_B] == (r >> 8) && !(cpu.mSFR[REG_PSW] & C_) && !!(cpu.mSFR[REG_PSW] & OV_) == (r > 0xFF),
              "a=%02X b=%02X -> acc=%02X b=%02X psw=%02X", a, b, cpu.mSFR[REG_ACC], cpu.mSFR[REG_B], cpu.mSFR[REG_PSW]);
        /* DIV AB */
        ins[0] = 0x84; run(ins, 1, a, b, psw, 1, 0);
        if (b) check("DIV", cpu.mSFR[REG_ACC] == a / b && cpu.mSFR[REG_B] == a % b && !(cpu.mSFR[REG_PSW] & (C_ | OV_)),
              "a=%02X b=%02X -> acc=%02X b=%02X psw=%02X", a, b, cpu.mSFR[REG_ACC], cpu.mSFR[REG_B], cpu.mSFR[REG_PSW]);
        else check("DIV0", (cpu.mSFR[REG_PSW] & OV_) && !(cpu.mSFR[REG_PSW] & C_), "a=%02X b=00 -> psw=%02X", a, cpu.mSFR[REG_PSW]);
        /* DA A (after an ADD-like state: use psw C/AC as given) */
        ins[0] = 0xD4; run(ins, 1, a, 0, psw, 1, 0);
        { int v = a, c = cin; int ac = !!(psw & AC_);
          if ((v & 0xF) > 9 || ac) v += 6;
          if (((v >> 4) & 0xF) > 9 || c || v > 0x9F) { v += 0x60; c = 1; }
          /* reference: C set if result of second step overflowed or C was already set; DA never clears C */
          check("DA", cpu.mSFR[REG_ACC] == (v & 0xFF) && !!(cpu.mSFR[REG_PSW] & C_) == (c || ((v & 0x100) != 0)),
              "a=%02X C=%d AC=%d -> acc=%02X psw=%02X (exp acc=%02X)", a, cin, ac, cpu.mSFR[REG_ACC], cpu.mSFR[REG_PSW], v & 0xFF); }
        /* CJNE A,#b,rel : C = A < b, jump if != */
        ins[0] = 0xB4; ins[1] = b; ins[2] = 0x02; run(ins, 3, a, 0, psw, 1, 0);
        check("CJNE", !!(cpu.mSFR[REG_PSW] & C_) == (a < b) && cpu.mPC == (a != b ? 5 : 3),
              "a=%02X b=%02X -> psw=%02X pc=%04X", a, b, cpu.mSFR[REG_PSW], cpu.mPC);
        /* CJNE @R0,#b,rel */
        ins[0] = 0xB6; ins[1] = b; ins[2] = 0x02; run(ins, 3, 0x55, 0, psw, 0x30, a);
        check("CJNE@R0", !!(cpu.mSFR[REG_PSW] & C_) == (a < b) && cpu.mPC == (a != b ? 5 : 3),
              "m=%02X b=%02X -> psw=%02X pc=%04X", a, b, cpu.mSFR[REG_PSW], cpu.mPC);
        /* XCHD A,@R0 */
        ins[0] = 0xD6; run(ins, 1, a, 0, psw, 0x30, b);
        check("XCHD", cpu.mSFR[REG_ACC] == ((a & 0xF0) | (b & 0x0F)) && cpu.mLowerData[0x30] == ((b & 0xF0) | (a & 0x0F)),
              "a=%02X m=%02X -> acc=%02X m=%02X", a, b, cpu.mSFR[REG_ACC], cpu.mLowerData[0x30]);
        /* RLC A / RRC A */
        ins[0] = 0x33; run(ins, 1, a, 0, psw, 1, 0);
        check("RLC", cpu.mSFR[REG_ACC] == (uint8_t)((a << 1) | cin) && !!(cpu.mSFR[REG_PSW] & C_) == !!(a & 0x80), "a=%02X cin=%d -> acc=%02X psw=%02X", a, cin, cpu.mSFR[REG_ACC], cpu.mSFR[REG_PSW]);
        ins[0] = 0x13; run(ins, 1, a, 0, psw, 1, 0);
        check("RRC", cpu.mSFR[REG_ACC] == (uint8_t)((a >> 1) | (cin << 7)) && !!(cpu.mSFR[REG_PSW] & C_) == (a & 1), "a=%02X cin=%d -> acc=%02X psw=%02X", a, cin, cpu.mSFR[REG_ACC], cpu.mSFR[REG_PSW]);
        /* SWAP, RL, RR */
        ins[0] = 0xC4; run(ins, 1, a, 0, psw, 1, 0);
        check("SWAP", cpu.mSFR[REG_ACC] == (uint8_t)((a << 4) | (a >> 4)), "a=%02X -> %02X", a, cpu.mSFR[REG_ACC]);
        /* DJNZ direct */
        ins[0] = 0xD5; ins[1] = 0x30; ins[2] = 0x02; run(ins, 3, 0, 0, psw, 1, 0); /* mem[0x30] = 0 from run() r0val=1.. set explicitly */
        /* MOVC A,@A+PC : code[ (pc+1)+a ] */
        ins[0] = 0x83; run(ins, 1, a, 0, psw, 1, 0);
        check("MOVC_PC", cpu.mSFR[REG_ACC] == code[1 + a], "a=%02X -> acc=%02X exp=%02X", a, cpu.mSFR[REG_ACC], code[1 + a]);
        /* INC/DEC leave flags alone */
        ins[0] = 0x04; run(ins, 1, a, 0, psw, 1, 0);
        check("INC", cpu.mSFR[REG_ACC] == (uint8_t)(a + 1) && (cpu.mSFR[REG_PSW] & (C_|AC_|OV_)) == (psw & (C_|AC_|OV_)), "a=%02X psw=%02X -> acc=%02X psw=%02X", a, psw, cpu.mSFR[REG_ACC], cpu.mSFR[REG_PSW]);
        ins[0] = 0x14; run(ins, 1, a, 0, psw, 1, 0);
        check("DEC", cpu.mSFR[REG_ACC] == (uint8_t)(a - 1) && (cpu.mSFR[REG_PSW] & (C_|AC_|OV_)) == (psw & (C_|AC_|OV_)), "a=%02X psw=%02X -> acc=%02X psw=%02X", a, psw, cpu.mSFR[REG_ACC], cpu.mSFR[REG_PSW]);
        /* CPL C / ANL C,bit / ORL C,/bit on a bit-addressable byte 0x20 (bit 0x00..) */
        ins[0] = 0xB3; run(ins, 1, a, 0, psw, 1, 0);
        check("CPLC", !!(cpu.mSFR[REG_PSW] & C_) == !cin, "cin=%d -> psw=%02X", cin, cpu.mSFR[REG_PSW]);
        /* MOV direct,@R0 : IRAM[0x40] <- IRAM[0x30] (R0=0x30 holds b) */
        ins[0] = 0x86; ins[1] = 0x40; run(ins, 2, a, 0, psw, 0x30, b);
        check("MOVdir,@R0", cpu.mLowerData[0x40] == b && cpu.mLowerData[0x30] == b, "b=%02X -> [40]=%02X [30]=%02X", b, cpu.mLowerData[0x40], cpu.mLowerData[0x30]);
        /* MOV @R0,direct : IRAM[0x30] <- ACC (direct 0xE0) */
        ins[0] = 0xA6; ins[1] = 0xE0; run(ins, 2, a, 0, psw, 0x30, b);
        check("MOV@R0,dir", cpu.mLowerData[0x30] == a, "a=%02X -> [30]=%02X", a, cpu.mLowerData[0x30]);
        /* MOV direct,direct : 85 src dst : IRAM[0x41] <- IRAM[0x30] */
        ins[0] = 0x85; ins[1] = 0x30; ins[2] = 0x41; run(ins, 3, a, 0, psw, 0x30, b);
        check("MOVdir,dir", cpu.mLowerData[0x41] == b && cpu.mLowerData[0x30] == b, "b=%02X -> [41]=%02X", b, cpu.mLowerData[0x41]);
        /* MOV direct,R0 (0x88) and MOV R1,direct (0xA9) */
        ins[0] = 0x88; ins[1] = 0x42; run(ins, 2, a, 0, psw, 0x30, b);
        check("MOVdir,R0", cpu.mLowerData[0x42] == 0x30, "-> [42]=%02X", cpu.mLowerData[0x42]);
        ins[0] = 0xA9; ins[1] = 0x30; run(ins, 2, a, 0, psw, 0x30, b);
        check("MOVR1,dir", cpu.mLowerData[1] == b, "b=%02X -> R1=%02X", b, cpu.mLowerData[1]);
        /* XCH A,@R0 / XCH A,direct / XCH A,R0 */
        ins[0] = 0xC6; run(ins, 1, a, 0, psw, 0x30, b);
        check("XCH@R0", cpu.mSFR[REG_ACC] == b && cpu.mLowerData[0x30] == a, "a=%02X b=%02X -> acc=%02X [30]=%02X", a, b, cpu.mSFR[REG_ACC], cpu.mLowerData[0x30]);
        ins[0] = 0xC5; ins[1] = 0x30; run(ins, 2, a, 0, psw, 0x30, b);
        check("XCHdir", cpu.mSFR[REG_ACC] == b && cpu.mLowerData[0x30] == a, "a=%02X b=%02X -> acc=%02X [30]=%02X", a, b, cpu.mSFR[REG_ACC], cpu.mLowerData[0x30]);
        ins[0] = 0xC8; run(ins, 1, a, 0, psw, 0x30, b);
        check("XCHR0", cpu.mSFR[REG_ACC] == 0x30 && cpu.mLowerData[0] == a, "a=%02X -> acc=%02X R0=%02X", a, cpu.mSFR[REG_ACC], cpu.mLowerData[0]);
        /* INC/DEC @R0, direct */
        ins[0] = 0x06; run(ins, 1, a, 0, psw, 0x30, b);
        check("INC@R0", cpu.mLowerData[0x30] == (uint8_t)(b + 1), "b=%02X -> %02X", b, cpu.mLowerData[0x30]);
        ins[0] = 0x15; ins[1] = 0x30; run(ins, 2, a, 0, psw, 0x30, b);
        check("DECdir", cpu.mLowerData[0x30] == (uint8_t)(b - 1), "b=%02X -> %02X", b, cpu.mLowerData[0x30]);
        /* ANL/ORL/XRL direct,#imm and direct,A */
        ins[0] = 0x53; ins[1] = 0x30; ins[2] = a; run(ins, 3, 0x55, 0, psw, 0x30, b);
        check("ANLdir,#", cpu.mLowerData[0x30] == (b & a), "-> %02X", cpu.mLowerData[0x30]);
        ins[0] = 0x43; ins[1] = 0x30; ins[2] = a; run(ins, 3, 0x55, 0, psw, 0x30, b);
        check("ORLdir,#", cpu.mLowerData[0x30] == (b | a), "-> %02X", cpu.mLowerData[0x30]);
        ins[0] = 0x63; ins[1] = 0x30; ins[2] = a; run(ins, 3, 0x55, 0, psw, 0x30, b);
        check("XRLdir,#", cpu.mLowerData[0x30] == (b ^ a), "-> %02X", cpu.mLowerData[0x30]);
        ins[0] = 0x52; ins[1] = 0x30; run(ins, 2, a, 0, psw, 0x30, b);
        check("ANLdir,A", cpu.mLowerData[0x30] == (b & a), "-> %02X", cpu.mLowerData[0x30]);
        ins[0] = 0x42; ins[1] = 0x30; run(ins, 2, a, 0, psw, 0x30, b);
        check("ORLdir,A", cpu.mLowerData[0x30] == (b | a), "-> %02X", cpu.mLowerData[0x30]);
        ins[0] = 0x62; ins[1] = 0x30; run(ins, 2, a, 0, psw, 0x30, b);
        check("XRLdir,A", cpu.mLowerData[0x30] == (b ^ a), "-> %02X", cpu.mLowerData[0x30]);
        /* ANL/ORL/XRL A,@R0 and A,direct */
        ins[0] = 0x56; run(ins, 1, a, 0, psw, 0x30, b);
        check("ANLA,@R0", cpu.mSFR[REG_ACC] == (a & b), "-> %02X", cpu.mSFR[REG_ACC]);
        ins[0] = 0x65; ins[1] = 0x30; run(ins, 2, a, 0, psw, 0x30, b);
        check("XRLA,dir", cpu.mSFR[REG_ACC] == (a ^ b), "-> %02X", cpu.mSFR[REG_ACC]);
        /* CJNE R0,#imm,rel (0xB8): R0=0x30 vs imm b */
        ins[0] = 0xB8; ins[1] = b; ins[2] = 0x02; run(ins, 3, a, 0, psw, 0x30, 0);
        check("CJNER0", !!(cpu.mSFR[REG_PSW] & C_) == (0x30 < b) && cpu.mPC == (0x30 != b ? 5 : 3), "b=%02X -> psw=%02X pc=%04X", b, cpu.mSFR[REG_PSW], cpu.mPC);
        /* DJNZ direct,rel : [0x30]=b */
        ins[0] = 0xD5; ins[1] = 0x30; ins[2] = 0x02; run(ins, 3, a, 0, psw, 0x30, b);
        check("DJNZdir", cpu.mLowerData[0x30] == (uint8_t)(b - 1) && cpu.mPC == ((uint8_t)(b - 1) ? 5 : 3), "b=%02X -> [30]=%02X pc=%04X", b, cpu.mLowerData[0x30], cpu.mPC);
        /* MOV bit,C and MOV C,bit on bit 0x00 (IRAM 0x20.0) */
        cpu.mLowerData[0x20] = 0;
        ins[0] = 0x92; ins[1] = 0x03; run(ins, 2, a, 0, psw, 0x30, b);
        check("MOVbit,C", !!(cpu.mLowerData[0x20] & 0x08) == cin, "cin=%d -> [20]=%02X", cin, cpu.mLowerData[0x20]);
        if (fails > 40) break;
    }
    printf("%d tests, %d failures\n", tests, fails);
    return fails != 0;
}
