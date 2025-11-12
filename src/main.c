/********************************************************************
 *  AVR-VM  –  Fully-featured AVR-8 Virtual Machine
 *
 *  Supports the full Microchip/Atmel 8-bit AVR ISA (AVR / AVRe /
 *  AVRe+ / AVRxm / AVRxt / AVRrc). Target device-size chosen here
 *  is ATmega1284-class (128 K words flash, 16 K SRAM, 4 K EEPROM).
 *
 *  Build:  gcc -std=c89 -Wall -O2 avr_vm.c -o avr_vm
 *  Run  :  ./avr_vm  <file.hex>   [options]
 *
 *  ------------------------------------------------------------------
 *  Copyright 2025 Isak Ruas
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  ------------------------------------------------------------------
 *  Author : Isak Ruas
 *  Project: AVR-VM Core Emulator
 *  License: Apache License 2.0
 *  Year   : 2025
 ********************************************************************/

#define _POSIX_C_SOURCE 200809L /* for strdup() */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <io.h>
#define fileno _fileno
#else
#include <unistd.h>
#include <fcntl.h>
#endif

/*
#define INTERACTIVE 1  <- set 0 to keep it ANSI-C only
#if INTERACTIVE
#include <unistd.h>
#include <fcntl.h>
#endif
*/

/*--------------------------------------------------------------------
  Global compile-time options
 --------------------------------------------------------------------*/
#define FLASH_BYTES (256 * 1024) /* 128 Ki *words* -> 256 KiB */
#define SRAM_BYTES (16 * 1024)
#define EEPROM_BYTES (4 * 1024)
#define IO_BYTES 0x100 /* 256 std + ext */
#define REG_COUNT 32

/* Print every instruction?   */
static int g_trace = 0;

/*--------------------------------------------------------------------
  Helper macros
 --------------------------------------------------------------------*/
#define BIT(n) (1u << (n))
#define GETBIT(v, n) (((v) >> (n)) & 1)
#define SETBIT(v, n) ((v) |= BIT(n))
#define CLRBIT(v, n) ((v) &= ~BIT(n))

/*--------------------------------------------------------------------
  CPU structure
 --------------------------------------------------------------------*/
typedef struct
{
    /* Core register file ------------------------------------------------*/
    uint8_t R[REG_COUNT]; /* R0..R31 */
    uint32_t pc;          /* byte address (22-bit capable)        */
    uint16_t sp;          /* stack pointer                        */
    uint8_t sreg;         /* status register                      */
    /* Extended addressing ----------------------------------------------*/
    uint8_t rampx, rampy, rampz, rampd, eind;
    /* Memories ----------------------------------------------------------*/
    uint8_t *flash;       /* FLASH_BYTES                        */
    uint8_t *sram;        /* SRAM_BYTES                         */
    uint8_t *eeprom;      /* EEPROM_BYTES                       */
    uint8_t io[IO_BYTES]; /* 0x00-0xFF                          */
    /* Run-time ----------------------------------------------------------*/
    uint64_t cycles;
    int running;
} avr_t;

/*--------------------------------------------------------------------
  Forward declarations
 --------------------------------------------------------------------*/
static uint16_t flash_word(avr_t *cpu, uint32_t byte_addr);
static uint8_t read_data(avr_t *cpu, uint32_t addr);
static void write_data(avr_t *cpu, uint32_t addr, uint8_t v);
static void push8(avr_t *, uint8_t);
static uint8_t pop8(avr_t *);
static void push16(avr_t *, uint16_t);
static uint16_t pop16(avr_t *);
// static void disasm(avr_t *c, uint32_t pc);
static void dump_regs(avr_t *c);
static int is_2_word_instruction(uint16_t op);

/* Flag bits in SREG */
enum
{
    F_C = 0,
    F_Z,
    F_N,
    F_V,
    F_S,
    F_H,
    F_T,
    F_I
};

/*--------------------------------------------------------------------
  Flag helpers –   Arithmetic / logical
 --------------------------------------------------------------------*/

/**
 * @brief Sets Z, N, V, S flags for logical operations.
 * V is cleared, S is N^V (so S=N).
 * @param c CPU state
 * @param r Result byte
 */
static void flags_logic(avr_t *c, uint8_t r)
{
    if (r)
    {
        CLRBIT(c->sreg, F_Z);
    }
    else
    {
        SETBIT(c->sreg, F_Z);
    }

    if (r & 0x80)
    {
        SETBIT(c->sreg, F_N);
    }
    else
    {
        CLRBIT(c->sreg, F_N);
    }

    CLRBIT(c->sreg, F_V);

    if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V))
    {
        SETBIT(c->sreg, F_S);
    }
    else
    {
        CLRBIT(c->sreg, F_S);
    }
}

/**
 * @brief Sets all flags (H,C,Z,N,V,S) for addition (ADD, ADC).
 * @param c CPU state
 * @param a Operand 1 (Rd)
 * @param b Operand 2 (Rr or K)
 * @param r 16-bit result (a+b+cin)
 * @param cin Carry in (0 or 1)
 */
static void flags_add(avr_t *c, uint8_t a, uint8_t b, uint16_t r, uint8_t cin)
{
    uint8_t res = (uint8_t)r;
    /* H */
    uint8_t h = ((a & 0x0f) + (b & 0x0f) + cin) & 0x10;
    if (h)
        SETBIT(c->sreg, F_H);
    else
        CLRBIT(c->sreg, F_H);
    /* C */
    if (r & 0x100)
        SETBIT(c->sreg, F_C);
    else
        CLRBIT(c->sreg, F_C);
    /* Z */
    if (res)
        CLRBIT(c->sreg, F_Z);
    else
        SETBIT(c->sreg, F_Z);
    /* N */
    if (res & 0x80)
        SETBIT(c->sreg, F_N);
    else
        CLRBIT(c->sreg, F_N);
    /* V */
    uint8_t v = (~(a ^ b) & (a ^ res) & 0x80);
    if (v)
        SETBIT(c->sreg, F_V);
    else
        CLRBIT(c->sreg, F_V);
    /* S */
    if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V))
        SETBIT(c->sreg, F_S);
    else
        CLRBIT(c->sreg, F_S);
}

/**
 * @brief Sets all flags (H,C,Z,N,V,S) for subtraction (SUB, SBC, CP, CPI, CPC).
 * @param c CPU state
 * @param a Operand 1 (Rd)
 * @param b Operand 2 (Rr or K)
 * @param cin Carry in (0 or 1) - used as borrow
 */
static void flags_sub(avr_t *c, uint8_t a, uint8_t b, uint8_t cin)
{
    uint16_t s_full = (uint16_t)a - (uint16_t)b - (uint16_t)cin;
    uint8_t res = (uint8_t)s_full;

    /* H: Half Carry (Borrow from bit 3) */
    uint8_t h_borrow = (~a & b) | (b & res) | (res & ~a);
    if (h_borrow & 0x08)
        SETBIT(c->sreg, F_H);
    else
        CLRBIT(c->sreg, F_H);

    /* C: Carry (Borrow from bit 7) */
    if (s_full & 0x100)
        SETBIT(c->sreg, F_C);
    else
        CLRBIT(c->sreg, F_C);

    /* Z: Zero Flag */
    /* For SBC/CPC, Z is unchanged if result is 0. */
    if (cin && (res == 0))
    {
        /* Z flag is unchanged (Z = Z) */
    }
    else
    {
        if (res == 0)
            SETBIT(c->sreg, F_Z);
        else
            CLRBIT(c->sreg, F_Z);
    }

    /* N: Negative Flag */
    if (res & 0x80)
        SETBIT(c->sreg, F_N);
    else
        CLRBIT(c->sreg, F_N);

    /* V: Overflow Flag */
    uint8_t v = (a & ~b & ~res) | (~a & b & res);
    if (v & 0x80)
        SETBIT(c->sreg, F_V);
    else
        CLRBIT(c->sreg, F_V);

    /* S: Sign Flag (N ^ V) */
    if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V))
        SETBIT(c->sreg, F_S);
    else
        CLRBIT(c->sreg, F_S);
}

/*--------------------------------------------------------------------
  Memory helpers
 --------------------------------------------------------------------*/
static uint16_t flash_word(avr_t *cpu, uint32_t byte_addr)
{
    return cpu->flash[byte_addr] | (cpu->flash[byte_addr + 1] << 8);
}
static uint8_t read_data(avr_t *cpu, uint32_t addr)
{
    if (addr < REG_COUNT)
        return cpu->R[addr];
    else if (addr < 0x100)
        return cpu->io[addr - 0x20];
    else if (addr - 0x100 < SRAM_BYTES)
        return cpu->sram[addr - 0x100];
    fprintf(stderr, "Data read OOB 0x%06lX\n", (unsigned long)addr);
    return 0;
}
static void write_data(avr_t *cpu, uint32_t addr, uint8_t v)
{
    if (addr < REG_COUNT)
        cpu->R[addr] = v;
    else if (addr < 0x100)
        cpu->io[addr - 0x20] = v;
    else if (addr - 0x100 < SRAM_BYTES)
        cpu->sram[addr - 0x100] = v;
    else
        fprintf(stderr, "Data write OOB 0x%06lX\n", (unsigned long)addr);
}
static void push8(avr_t *c, uint8_t v) { write_data(c, c->sp--, v); }
static uint8_t pop8(avr_t *c) { return read_data(c, ++c->sp); }
static void push16(avr_t *c, uint16_t v)
{
    push8(c, (v >> 8) & 0xFF); // Push high byte
    push8(c, v & 0xFF);        // Push low byte
}
static uint16_t pop16(avr_t *c)
{
    uint8_t l = pop8(c); // Pop low byte
    uint8_t h = pop8(c); // Pop high byte
    return (h << 8) | l;
}

/**
 * @brief Helper to check if an instruction is 2 words (4 bytes) long.
 * Used by skip instructions (CPSE, SBRC, SBRS).
 * @param op The 16-bit opcode of the instruction *to be skipped*.
 * @return 1 if 2-word, 0 if 1-word.
 */
static int is_2_word_instruction(uint16_t op)
{
    /* LDS - 1001 000d dddd 0000 */
    if ((op & 0xFE0F) == 0x9000)
        return 1;
    /* STS - 1001 001r rrrr 0000 */
    if ((op & 0xFE0F) == 0x9200)
        return 1;
    /* CALL - 1001 010k kkkk 111k */
    if ((op & 0xFE0E) == 0x940E)
        return 1;
    /* JMP - 1001 010k kkkk 110k */
    if ((op & 0xFE0E) == 0x940C)
        return 1;

    return 0;
}

/*--------------------------------------------------------------------
  The big single-step decoder / executor
 --------------------------------------------------------------------*/

#define TRACE(...)               \
    do                           \
    {                            \
        if (g_trace)             \
            printf(__VA_ARGS__); \
    } while (0)

static void avr_step(avr_t *c)
{
    uint32_t cur_pc = c->pc; /* PC before instruction fetch */
    uint16_t op = flash_word(c, cur_pc);
    c->pc += 2; /* PC points to next word    */

    // dump_regs(c);
    /* ==================== ARITHMETIC INSTRUCTIONS ==================== */

    /* ADD  – 0000 11rd dddd rrrr (mask 0xFC00 == 0x0C00) */
    if ((op & 0xFC00) == 0x0C00)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t a = c->R[d];
        uint8_t b = c->R[r];
        uint16_t s = a + b;
        c->R[d] = (uint8_t)s;
        flags_add(c, a, b, s, 0);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  ADD  R%u,R%u    -> R%u=0x%02X\n",
              (unsigned long)cur_pc, op, d, r, d, c->R[d]);
    }

    /* ADC  – 0001 11rd dddd rrrr (mask 0xFC00 == 0x1C00) */
    /* Also ROL (ADC Rd,Rd) */
    else if ((op & 0xFC00) == 0x1C00)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t a = c->R[d];
        uint8_t b = c->R[r];
        uint8_t c_in = GETBIT(c->sreg, F_C);
        uint16_t s = a + b + c_in;

        if (d == r)
        { /* This is ROL */
            uint8_t msb = (a & 0x80) ? 1 : 0;
            c->R[d] = (a << 1) | c_in;
            flags_add(c, a, a, s, c_in); /* Use ADD logic for flags */
            if (msb)
                SETBIT(c->sreg, F_C); /* But override C flag */
            else
                CLRBIT(c->sreg, F_C);
            TRACE("PC=%06lX  %04X  ROL  R%u -> 0x%02X\n",
                  (unsigned long)cur_pc, op, d, c->R[d]);
        }
        else
        { /* This is ADC */
            c->R[d] = (uint8_t)s;
            flags_add(c, a, b, s, c_in);
            TRACE("PC=%06lX  %04X  ADC  R%u,R%u +C=%u -> R%u=0x%02X\n",
                  (unsigned long)cur_pc, op, d, r, c_in, d, c->R[d]);
        }
        c->cycles += 1;
    }

    /* SUB  – 0001 10rd dddd rrrr (mask 0xFC00 == 0x1800) */
    else if ((op & 0xFC00) == 0x1800)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t a = c->R[d];
        uint8_t b = c->R[r];
        c->R[d] = a - b;
        flags_sub(c, a, b, 0);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  SUB  R%u,R%u    -> R%u=0x%02X\n",
              (unsigned long)cur_pc, op, d, r, d, c->R[d]);
    }

    /* SBC  – 0000 10rd dddd rrrr (mask 0xFC00 == 0x0800) */
    else if ((op & 0xFC00) == 0x0800)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t a = c->R[d];
        uint8_t b = c->R[r];
        uint8_t c_in = GETBIT(c->sreg, F_C);
        c->R[d] = a - b - c_in;
        flags_sub(c, a, b, c_in);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  SBC  R%u,R%u -C=%u -> R%u=0x%02X\n",
              (unsigned long)cur_pc, op, d, r, c_in, d, c->R[d]);
    }

    /* INC  – 1001 010d dddd 0011 (mask 0xFE0F == 0x9403) */
    else if ((op & 0xFE0F) == 0x9403)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t res = c->R[d] + 1;
        c->R[d] = res;
        if (res == 0)
            SETBIT(c->sreg, F_Z);
        else
            CLRBIT(c->sreg, F_Z);
        if (res & 0x80)
            SETBIT(c->sreg, F_N);
        else
            CLRBIT(c->sreg, F_N);
        if (res == 0x80)
            SETBIT(c->sreg, F_V);
        else
            CLRBIT(c->sreg, F_V);
        if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V))
            SETBIT(c->sreg, F_S);
        else
            CLRBIT(c->sreg, F_S);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  INC  R%u        -> 0x%02X\n",
              (unsigned long)cur_pc, op, d, res);
    }

    /* DEC  – 1001 010d dddd 1010 (mask 0xFE0F == 0x940A) */
    else if ((op & 0xFE0F) == 0x940A)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t res = c->R[d] - 1;
        c->R[d] = res;
        if (res == 0)
            SETBIT(c->sreg, F_Z);
        else
            CLRBIT(c->sreg, F_Z);
        if (res & 0x80)
            SETBIT(c->sreg, F_N);
        else
            CLRBIT(c->sreg, F_N);
        if (res == 0x7F)
            SETBIT(c->sreg, F_V);
        else
            CLRBIT(c->sreg, F_V);
        if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V))
            SETBIT(c->sreg, F_S);
        else
            CLRBIT(c->sreg, F_S);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  DEC  R%u        -> 0x%02X\n",
              (unsigned long)cur_pc, op, d, res);
    }

    /* NEG  – 1001 010d dddd 0001 (mask 0xFE0F == 0x9401) */
    else if ((op & 0xFE0F) == 0x9401)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t a = c->R[d];
        uint8_t res = -a;
        c->R[d] = res;

        if (res == 0x80)
            SETBIT(c->sreg, F_V);
        else
            CLRBIT(c->sreg, F_V);
        if (res != 0x00)
            SETBIT(c->sreg, F_C);
        else
            CLRBIT(c->sreg, F_C);

        /* H flag logic for NEG: R3 | Rd3 */
        uint8_t h = (res & 0x08) | (a & 0x08);
        if (h)
            SETBIT(c->sreg, F_H);
        else
            CLRBIT(c->sreg, F_H);

        if (res == 0)
            SETBIT(c->sreg, F_Z);
        else
            CLRBIT(c->sreg, F_Z);
        if (res & 0x80)
            SETBIT(c->sreg, F_N);
        else
            CLRBIT(c->sreg, F_N);
        if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V))
            SETBIT(c->sreg, F_S);
        else
            CLRBIT(c->sreg, F_S);

        c->cycles += 1;
        TRACE("PC=%06lX  %04X  NEG  R%u        -> 0x%02X\n",
              (unsigned long)cur_pc, op, d, res);
    }

    /* COM  – 1001 010d dddd 0000 (mask 0xFE0F == 0x9400) */
    else if ((op & 0xFE0F) == 0x9400)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t res = ~c->R[d];
        c->R[d] = res;
        SETBIT(c->sreg, F_C);
        flags_logic(c, res); /* Sets Z, N, V=0, S */
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  COM  R%u        -> 0x%02X\n",
              (unsigned long)cur_pc, op, d, res);
    }

    /* ==================== LOGIC AND BIT OPERATIONS ==================== */

    /* AND  – 0010 00rd dddd rrrr (mask 0xFC00 == 0x2000) */
    /* Also TST (AND Rd,Rd) */
    else if ((op & 0xFC00) == 0x2000)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t res = c->R[d] & c->R[r];
        c->R[d] = res;
        flags_logic(c, res);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  AND  R%u,R%u -> R%u=0x%02X\n",
              (unsigned long)cur_pc, op, d, r, d, res);
    }

    /* OR   – 0010 10rd dddd rrrr (mask 0xFC00 == 0x2800) */
    else if ((op & 0xFC00) == 0x2800)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t res = c->R[d] | c->R[r];
        c->R[d] = res;
        flags_logic(c, res);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  OR   R%u,R%u -> R%u=0x%02X\n",
              (unsigned long)cur_pc, op, d, r, d, res);
    }

    /* EOR  – 0010 01rd dddd rrrr (mask 0xFC00 == 0x2400) */
    /* Also CLR (EOR Rd,Rd) */
    else if ((op & 0xFC00) == 0x2400)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t res = c->R[d] ^ c->R[r];
        c->R[d] = res;
        flags_logic(c, res);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  EOR  R%u,R%u -> R%u=0x%02X\n",
              (unsigned long)cur_pc, op, d, r, d, res);
    }

    /* ANDI – 0111 KKKK dddd KKKK (mask 0xF000 == 0x7000) */
    /* Also CBR (ANDI Rd, 0xFF-K) */
    else if ((op & 0xF000) == 0x7000)
    {
        uint8_t d = 16 + ((op >> 4) & 0x0F);
        uint8_t K = ((op >> 4) & 0xF0) | (op & 0x0F);
        uint8_t res = c->R[d] & K;
        c->R[d] = res;
        flags_logic(c, res);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  ANDI R%u,0x%02X -> 0x%02X\n",
              (unsigned long)cur_pc, op, d, K, res);
    }

    /* ORI/SBR – 0110 KKKK dddd KKKK (mask 0xF000 == 0x6000) */
    else if ((op & 0xF000) == 0x6000)
    {
        uint8_t d = 16 + ((op >> 4) & 0x0F);
        uint8_t K = ((op >> 4) & 0xF0) | (op & 0x0F);
        uint8_t res = c->R[d] | K;
        c->R[d] = res;
        flags_logic(c, res);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  ORI  R%u,0x%02X -> 0x%02X\n",
              (unsigned long)cur_pc, op, d, K, res);
    }

    /* CPI – 0011 KKKK dddd KKKK (mask 0xF000 == 0x3000) */
    else if ((op & 0xF000) == 0x3000)
    {
        uint8_t d = 16 + ((op >> 4) & 0x0F);
        uint8_t K = ((op >> 4) & 0xF0) | (op & 0x0F);
        uint8_t a = c->R[d];
        flags_sub(c, a, K, 0);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  CPI  R%u,0x%02X (flags only)\n",
              (unsigned long)cur_pc, op, d, K);
    }

    /* SBI – 1001 1010 AAAA Abbb (mask 0xFF00 == 0x9A00) */
    else if ((op & 0xFF00) == 0x9A00)
    {
        uint8_t A = (op >> 3) & 0x1F;
        uint8_t b = op & 0x07;
        uint8_t val = read_data(c, 0x20 + A);
        write_data(c, 0x20 + A, val | (1 << b));
        c->cycles += 2; /* 1 on AVRxm/xt */
        TRACE("PC=%06lX  %04X  SBI  0x%02X,%u\n",
              (unsigned long)cur_pc, op, 0x20 + A, b);
    }

    /* CBI – 1001 1000 AAAA Abbb (mask 0xFF00 == 0x9800) */
    else if ((op & 0xFF00) == 0x9800)
    {
        uint8_t A = (op >> 3) & 0x1F;
        uint8_t b = op & 0x07;
        uint8_t val = read_data(c, 0x20 + A);
        write_data(c, 0x20 + A, val & ~(1 << b));
        c->cycles += 2; /* 1 on AVRxm/xt */
        TRACE("PC=%06lX  %04X  CBI  0x%02X,%u\n",
              (unsigned long)cur_pc, op, 0x20 + A, b);
    }

    /* BST – 1111 101d dddd 0bbb (mask 0xFE08 == 0xFA00) */
    else if ((op & 0xFE08) == 0xFA00)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t b = op & 0x07;
        if (c->R[d] & (1 << b))
            SETBIT(c->sreg, F_T);
        else
            CLRBIT(c->sreg, F_T);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  BST  R%u,%u -> T=%u\n",
              (unsigned long)cur_pc, op, d, b, GETBIT(c->sreg, F_T));
    }

    /* BLD – 1111 100d dddd 0bbb (mask 0xFE08 == 0xF800) */
    else if ((op & 0xFE08) == 0xF800)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t b = op & 0x07;
        if (GETBIT(c->sreg, F_T))
            c->R[d] |= (1 << b);
        else
            c->R[d] &= ~(1 << b);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  BLD  R%u,%u <- T=%u\n",
              (unsigned long)cur_pc, op, d, b, GETBIT(c->sreg, F_T));
    }

    /* ==================== CONTROL FLOW ==================== */

    /* RJMP – 1100 kkkk kkkk kkkk (mask 0xF000 == 0xC000) */
    else if ((op & 0xF000) == 0xC000)
    {
        int16_t k = op & 0x0FFF;
        if (k & 0x0800)
            k |= 0xF000;
        c->pc = cur_pc + 2 + (k << 1);
        c->cycles += 2;
        TRACE("PC=%06lX  %04X  RJMP %+d -> PC=0x%06lX\n",
              (unsigned long)cur_pc, op, k, (unsigned long)c->pc);
    }

    /* JMP – 1001 010k kkkk 110k (mask 0xFE0E == 0x940C) */
    else if ((op & 0xFE0E) == 0x940C)
    {
        uint16_t op2 = flash_word(c, c->pc); /* c->pc is already cur_pc+2 */
        c->pc += 2;                          // Consume the second word

        uint32_t k = op2;                   /* k15:0 */
        k |= (uint32_t)(op & 0x0001) << 16; /* k16 */
        k |= (uint32_t)(op & 0x01E0) << 12; /* k20:17 */
        k |= (uint32_t)(op & 0x0200) << 12; /* k21 */

        c->pc = k << 1; /* Address is in words */
        c->cycles += 3;
        TRACE("PC=%06lX  %04X %04X  JMP  0x%06lX\n",
              (unsigned long)cur_pc, op, op2, (unsigned long)c->pc);
    }

    /* RCALL – 1101 kkkk kkkk kkkk (mask 0xF000 == 0xD000) */
    else if ((op & 0xF000) == 0xD000)
    {
        int16_t k = op & 0x0FFF;
        if (k & 0x0800)
            k |= 0xF000;
        push16(c, cur_pc + 2); /* Return address is next instruction */
        c->pc = cur_pc + 2 + (k << 1);
        c->cycles += 3; /* 3 for 16-bit PC */
        TRACE("PC=%06lX  %04X  RCALL %+d -> PC=0x%06lX\n",
              (unsigned long)cur_pc, op, k, (unsigned long)c->pc);
    }

    /* CALL – 1001 010k kkkk 111k (mask 0xFE0E == 0x940E) */
    else if ((op & 0xFE0E) == 0x940E)
    {
        uint16_t op2 = flash_word(c, c->pc);
        c->pc += 2; /* Consume the second word */

        uint32_t k = op2;                   /* k15:0 */
        k |= (uint32_t)(op & 0x0001) << 16; /* k16 */
        k |= (uint32_t)(op & 0x01E0) << 12; /* k20:17 */
        k |= (uint32_t)(op & 0x0200) << 12; /* k21 */

        push16(c, cur_pc + 4); /* Return address (after 2-word instruction) */
        c->pc = k << 1;        /* Address is in words */
        c->cycles += 4;        /* 4 for 16-bit PC */
        TRACE("PC=%06lX  %04X %04X  CALL 0x%06lX\n",
              (unsigned long)cur_pc, op, op2, (unsigned long)c->pc);
    }

    /* RET – 1001 0101 0000 1000 (opcode 0x9508) */
    else if (op == 0x9508)
    {
        c->pc = pop16(c);
        c->cycles += 4; /* 4 for 16-bit PC */
        TRACE("PC=%06lX  %04X  RET  -> PC=0x%06lX\n",
              (unsigned long)cur_pc, op, (unsigned long)c->pc);
    }

    /* RETI – 1001 0101 0001 1000 (opcode 0x9518) */
    else if (op == 0x9518)
    {
        c->pc = pop16(c);
        SETBIT(c->sreg, F_I);
        c->cycles += 4; /* 4 for 16-bit PC */
        TRACE("PC=%06lX  %04X  RETI -> PC=0x%06lX  I=1\n",
              (unsigned long)cur_pc, op, (unsigned long)c->pc);
    }

    /* BRBS/BRBC – 1111 00ss kkkk kkkk / 1111 01ss kkkk kkkk */
    else if ((op & 0xFC00) == 0xF000)
    { /* BRBS - 1111 00.. */
        uint8_t s = op & 0x07;
        int8_t k = (op >> 3) & 0x7F;
        if (k & 0x40)
            k |= 0x80;
        int take = GETBIT(c->sreg, s);
        if (take)
        {
            c->pc = cur_pc + 2 + (k << 1);
            c->cycles += 2;
        }
        else
        {
            c->cycles += 1;
        }
        TRACE("PC=%06lX  %04X  BRBS %u,%+d %s\n",
              (unsigned long)cur_pc, op, s, k, take ? "-> taken" : "");
    }
    else if ((op & 0xFC00) == 0xF400)
    { /* BRBC - 1111 01.. */
        uint8_t s = op & 0x07;
        int8_t k = (op >> 3) & 0x7F;
        if (k & 0x40)
            k |= 0x80;
        int take = !GETBIT(c->sreg, s);
        if (take)
        {
            c->pc = cur_pc + 2 + (k << 1);
            c->cycles += 2;
        }
        else
        {
            c->cycles += 1;
        }
        TRACE("PC=%06lX  %04X  BRBC %u,%+d %s\n",
              (unsigned long)cur_pc, op, s, k, take ? "-> taken" : "");
    }

    /* ==================== REGISTER MANIPULATION ==================== */

    /* MOV – 0010 11rd dddd rrrr (mask 0xFC00 == 0x2C00) */
    else if ((op & 0xFC00) == 0x2C00)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t r = ((op & 0x200) >> 5) | (op & 0x0F);
        c->R[d] = c->R[r];
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  MOV  R%u,R%u -> 0x%02X\n",
              (unsigned long)cur_pc, op, d, r, c->R[d]);
    }

    /* MOVW – 0000 0001 dddd rrrr (mask 0xFF00 == 0x0100) */
    else if ((op & 0xFF00) == 0x0100)
    {
        uint8_t d = ((op >> 3) & 0x1E);
        uint8_t r = ((op & 0x0F) << 1);
        c->R[d] = c->R[r];
        c->R[d + 1] = c->R[r + 1];
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  MOVW R%u:R%u <- R%u:R%u\n",
              (unsigned long)cur_pc, op, d + 1, d, r + 1, r);
    }

    /* LDS – 1001 000d dddd 0000 (mask 0xFE0F == 0x9000) */
    else if ((op & 0xFE0F) == 0x9000)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint16_t ea = flash_word(c, c->pc);
        c->pc += 2;
        c->R[d] = read_data(c, ea);
        c->cycles += 2; /* 2 on AVRe */
        TRACE("PC=%06lX  %04X %04X  LDS  R%u <- [0x%04X]=0x%02X\n",
              (unsigned long)cur_pc, op, ea, d, ea, c->R[d]);
    }

    /* STS – 1001 001r rrrr 0000 (mask 0xFE0F == 0x9200) */
    else if ((op & 0xFE0F) == 0x9200)
    {
        uint8_t r = (op >> 4) & 0x1F;
        uint16_t ea = flash_word(c, c->pc);
        c->pc += 2;
        write_data(c, ea, c->R[r]);
        c->cycles += 2; /* 2 on AVRe */
        TRACE("PC=%06lX  %04X %04X  STS  [0x%04X] <- R%u=0x%02X\n",
              (unsigned long)cur_pc, op, ea, ea, r, c->R[r]);
    }

    /* LDI – 1110 KKKK dddd KKKK (mask 0xF000 == 0xE000) */
    /* Also SER (LDI Rd, 0xFF) */
    else if ((op & 0xF000) == 0xE000)
    {
        uint8_t d = 16 + ((op >> 4) & 0x0F);
        uint8_t K = ((op >> 4) & 0xF0) | (op & 0x0F);
        c->R[d] = K;
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  LDI  R%u,0x%02X\n",
              (unsigned long)cur_pc, op, d, K);
    }

    /* PUSH – 1001 001r rrrr 1111 (mask 0xFE0F == 0x920F) */
    else if ((op & 0xFE0F) == 0x920F)
    {
        uint8_t r = (op >> 4) & 0x1F;
        push8(c, c->R[r]);
        c->cycles += 2; /* 2 on AVRe */
        TRACE("PC=%06lX  %04X  PUSH R%u=0x%02X\n",
              (unsigned long)cur_pc, op, r, c->R[r]);
    }

    /* POP – 1001 000d dddd 1111 (mask 0xFE0F == 0x900F) */
    else if ((op & 0xFE0F) == 0x900F)
    {
        uint8_t d = (op >> 4) & 0x1F;
        c->R[d] = pop8(c);
        c->cycles += 2; /* 2 on AVRe */
        TRACE("PC=%06lX  %04X  POP  R%u <- 0x%02X\n",
              (unsigned long)cur_pc, op, d, c->R[d]);
    }

    /* IN – 1011 0AAd dddd AAAA (mask 0xF800 == 0xB000) */
    else if ((op & 0xF800) == 0xB000)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t A = ((op >> 5) & 0x30) | (op & 0x0F);
        c->R[d] = read_data(c, 0x20 + A);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  IN   R%u <- I/O[0x%02X]=0x%02X\n",
              (unsigned long)cur_pc, op, d, 0x20 + A, c->R[d]);
    }

    /* OUT – 1011 1AAr rrrr AAAA (mask 0xF800 == 0xB800) */
    else if ((op & 0xF800) == 0xB800)
    {
        uint8_t r = (op >> 4) & 0x1F;
        uint8_t A = ((op >> 5) & 0x30) | (op & 0x0F);
        write_data(c, 0x20 + A, c->R[r]);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  OUT  I/O[0x%02X] <- R%u=0x%02X\n",
              (unsigned long)cur_pc, op, 0x20 + A, r, c->R[r]);
    }

    /* ==================== COMPARE AND TEST ==================== */

    /* CP – 0001 01rd dddd rrrr (mask 0xFC00 == 0x1400) */
    else if ((op & 0xFC00) == 0x1400)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t a = c->R[d], b = c->R[r];
        flags_sub(c, a, b, 0);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  CP   R%u,R%u (flags)\n",
              (unsigned long)cur_pc, op, d, r);
    }

    /* CPC – 0000 01rd dddd rrrr (mask 0xFC00 == 0x0400) */
    else if ((op & 0xFC00) == 0x0400)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t a = c->R[d], b = c->R[r], c_in = GETBIT(c->sreg, F_C);
        flags_sub(c, a, b, c_in);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  CPC  R%u,R%u (flags)\n",
              (unsigned long)cur_pc, op, d, r);
    }

    /* CPSE – 0001 00rd dddd rrrr (mask 0xFC00 == 0x1000) */
    else if ((op & 0xFC00) == 0x1000)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t r = ((op & 0x200) >> 5) | (op & 0x0F);
        int skip = (c->R[d] == c->R[r]);
        c->cycles += 1; /* Base 1 cycle */
        if (skip)
        {
            uint16_t nxt = flash_word(c, c->pc); /* c->pc is already cur_pc+2 */
            if (is_2_word_instruction(nxt))
            {
                c->pc += 4;     /* Skip 2-word instruction */
                c->cycles += 2; /* Total 3 cycles */
            }
            else
            {
                c->pc += 2;     /* Skip 1-word instruction */
                c->cycles += 1; /* Total 2 cycles */
            }
        }
        TRACE("PC=%06lX  %04X  CPSE R%u,R%u %s\n",
              (unsigned long)cur_pc, op, d, r, skip ? "--> skip" : "");
    }

    /* SBRC – 1111 110r rrrr 0bbb (mask 0xFE08 == 0xFC00) */
    else if ((op & 0xFE08) == 0xFC00)
    {
        uint8_t r = (op >> 4) & 0x1F;
        uint8_t b = op & 0x07;
        int skip = !(c->R[r] & (1 << b));
        c->cycles += 1; /* Base 1 cycle */
        if (skip)
        {
            uint16_t nxt = flash_word(c, c->pc);
            if (is_2_word_instruction(nxt))
            {
                c->pc += 4;
                c->cycles += 2; /* Total 3 */
            }
            else
            {
                c->pc += 2;
                c->cycles += 1; /* Total 2 */
            }
        }
        TRACE("PC=%06lX  %04X  SBRC R%u,%u %s\n",
              (unsigned long)cur_pc, op, r, b, skip ? "--> skip" : "");
    }

    /* SBRS – 1111 111r rrrr 0bbb (mask 0xFE08 == 0xFE00) */
    else if ((op & 0xFE08) == 0xFE00)
    {
        uint8_t r = (op >> 4) & 0x1F;
        uint8_t b = op & 0x07;
        int skip = (c->R[r] & (1 << b));
        c->cycles += 1; /* Base 1 cycle */
        if (skip)
        {
            uint16_t nxt = flash_word(c, c->pc);
            if (is_2_word_instruction(nxt))
            {
                c->pc += 4;
                c->cycles += 2; /* Total 3 */
            }
            else
            {
                c->pc += 2;
                c->cycles += 1; /* Total 2 */
            }
        }
        TRACE("PC=%06lX  %04X  SBRS R%u,%u %s\n",
              (unsigned long)cur_pc, op, r, b, skip ? "--> skip" : "");
    }

    /* ==================== SHIFT AND ROTATE ==================== */

    /* LSR – 1001 010d dddd 0110 (mask 0xFE0F == 0x9406) */
    else if ((op & 0xFE0F) == 0x9406)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t lsb = c->R[d] & 1;
        uint8_t res = c->R[d] >> 1;
        c->R[d] = res;

        (lsb) ? SETBIT(c->sreg, F_C) : CLRBIT(c->sreg, F_C);
        (res == 0) ? SETBIT(c->sreg, F_Z) : CLRBIT(c->sreg, F_Z);
        CLRBIT(c->sreg, F_N);

        /* V = N ^ C (N is 0) */
        if (GETBIT(c->sreg, F_C))
            SETBIT(c->sreg, F_V);
        else
            CLRBIT(c->sreg, F_V);

        /* S = N ^ V */
        (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V)) ? SETBIT(c->sreg, F_S) : CLRBIT(c->sreg, F_S);

        c->cycles += 1;
        TRACE("PC=%06lX  %04X  LSR  R%u -> 0x%02X\n",
              (unsigned long)cur_pc, op, d, res);
    }

    /* ROR – 1001 010d dddd 0111 (mask 0xFE0F == 0x9407) */
    else if ((op & 0xFE0F) == 0x9407)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t cin = GETBIT(c->sreg, F_C);
        uint8_t lsb = c->R[d] & 1;
        uint8_t res = (c->R[d] >> 1) | (cin << 7);
        c->R[d] = res;

        (lsb) ? SETBIT(c->sreg, F_C) : CLRBIT(c->sreg, F_C);
        (res == 0) ? SETBIT(c->sreg, F_Z) : CLRBIT(c->sreg, F_Z);
        (res & 0x80) ? SETBIT(c->sreg, F_N) : CLRBIT(c->sreg, F_N);

        /* V = N ^ C */
        if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_C))
            SETBIT(c->sreg, F_V);
        else
            CLRBIT(c->sreg, F_V);

        /* S = N ^ V */
        (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V)) ? SETBIT(c->sreg, F_S) : CLRBIT(c->sreg, F_S);

        c->cycles += 1;
        TRACE("PC=%06lX  %04X  ROR  R%u -> 0x%02X\n",
              (unsigned long)cur_pc, op, d, res);
    }

    /* ASR – 1001 010d dddd 0101 (mask 0xFE0F == 0x9405) */
    else if ((op & 0xFE0F) == 0x9405)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t msb = c->R[d] & 0x80;
        uint8_t lsb = c->R[d] & 1;
        uint8_t res = (c->R[d] >> 1) | msb;
        c->R[d] = res;

        (lsb) ? SETBIT(c->sreg, F_C) : CLRBIT(c->sreg, F_C);
        (res == 0) ? SETBIT(c->sreg, F_Z) : CLRBIT(c->sreg, F_Z);
        (res & 0x80) ? SETBIT(c->sreg, F_N) : CLRBIT(c->sreg, F_N);

        /* V = N ^ C */
        if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_C))
            SETBIT(c->sreg, F_V);
        else
            CLRBIT(c->sreg, F_V);

        /* S = N ^ V */
        (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V)) ? SETBIT(c->sreg, F_S) : CLRBIT(c->sreg, F_S);

        c->cycles += 1;
        TRACE("PC=%06lX  %04X  ASR  R%u -> 0x%02X\n",
              (unsigned long)cur_pc, op, d, res);
    }

    /* SWAP – 1001 010d dddd 0010 (mask 0xFE0F == 0x9402) */
    else if ((op & 0xFE0F) == 0x9402)
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t val = c->R[d];
        c->R[d] = (val << 4) | (val >> 4);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  SWAP R%u -> 0x%02X\n",
              (unsigned long)cur_pc, op, d, c->R[d]);
    }

    /* ==================== MCU CONTROL ==================== */

    /* NOP – 0000 0000 0000 0000 */
    else if (op == 0x0000)
    {
        c->cycles += 1;
        TRACE("PC=%06lX  0000  NOP\n", (unsigned long)cur_pc);
    }

    /* SLEEP – 1001 0101 1000 1000 (opcode 0x9588) */
    else if (op == 0x9588)
    {
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  SLEEP (ignored)\n",
              (unsigned long)cur_pc, op);
    }

    /* WDR – 1001 0101 1010 1000 (opcode 0x95A8) */
    else if (op == 0x95A8)
    {
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  WDR (ignored)\n",
              (unsigned long)cur_pc, op);
    }

    /* ==================== SREG MANIPULATION ==================== */

    /* SEI – 1001 0100 0111 1000 (opcode 0x9478) */
    else if (op == 0x9478)
    {
        SETBIT(c->sreg, F_I);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  SEI  (I=1)\n",
              (unsigned long)cur_pc, op);
    }

    /* CLI – 1001 0100 1111 1000 (opcode 0x94F8) */
    else if (op == 0x94F8)
    {
        CLRBIT(c->sreg, F_I);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  CLI  (I=0)\n",
              (unsigned long)cur_pc, op);
    }

    /* SEC – 1001 0100 0000 1000 (opcode 0x9408) */
    else if (op == 0x9408)
    {
        SETBIT(c->sreg, F_C);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  SEC  (C=1)\n",
              (unsigned long)cur_pc, op);
    }

    /* CLC – 1001 0100 1000 1000 (opcode 0x9488) */
    else if (op == 0x9488)
    {
        CLRBIT(c->sreg, F_C);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  CLC  (C=0)\n",
              (unsigned long)cur_pc, op);
    }

    /* SEN – 1001 0100 0010 1000 (opcode 0x9428) */
    else if (op == 0x9428)
    {
        SETBIT(c->sreg, F_N);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  SEN  (N=1)\n",
              (unsigned long)cur_pc, op);
    }

    /* CLN – 1001 0100 1010 1000 (opcode 0x94A8) */
    else if (op == 0x94A8)
    {
        CLRBIT(c->sreg, F_N);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  CLN  (N=0)\n",
              (unsigned long)cur_pc, op);
    }

    /* SEZ – 1001 0100 0001 1000 (opcode 0x9418) */
    else if (op == 0x9418)
    {
        SETBIT(c->sreg, F_Z);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  SEZ  (Z=1)\n",
              (unsigned long)cur_pc, op);
    }

    /* CLZ – 1001 0100 1001 1000 (opcode 0x9498) */
    else if (op == 0x9498)
    {
        CLRBIT(c->sreg, F_Z);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  CLZ  (Z=0)\n",
              (unsigned long)cur_pc, op);
    }

    /* SEV – 1001 0100 0011 1000 (opcode 0x9438) */
    else if (op == 0x9438)
    {
        SETBIT(c->sreg, F_V);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  SEV  (V=1)\n",
              (unsigned long)cur_pc, op);
    }

    /* CLV – 1001 0100 1011 1000 (opcode 0x94B8) */
    else if (op == 0x94B8)
    {
        CLRBIT(c->sreg, F_V);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  CLV  (V=0)\n",
              (unsigned long)cur_pc, op);
    }

    /* SES – 1001 0100 0100 1000 (opcode 0x9448) */
    else if (op == 0x9448)
    {
        SETBIT(c->sreg, F_S);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  SES  (S=1)\n",
              (unsigned long)cur_pc, op);
    }

    /* CLS – 1001 0100 1100 1000 (opcode 0x94C8) */
    else if (op == 0x94C8)
    {
        CLRBIT(c->sreg, F_S);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  CLS  (S=0)\n",
              (unsigned long)cur_pc, op);
    }

    /* SEH – 1001 0100 0101 1000 (opcode 0x9458) */
    else if (op == 0x9458)
    {
        SETBIT(c->sreg, F_H);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  SEH  (H=1)\n",
              (unsigned long)cur_pc, op);
    }

    /* CLH – 1001 0100 1101 1000 (opcode 0x94D8) */
    else if (op == 0x94D8)
    {
        CLRBIT(c->sreg, F_H);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  CLH  (H=0)\n",
              (unsigned long)cur_pc, op);
    }

    /* SET – 1001 0100 0110 1000 (opcode 0x9468) */
    else if (op == 0x9468)
    {
        SETBIT(c->sreg, F_T);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  SET  (T=1)\n",
              (unsigned long)cur_pc, op);
    }

    /* CLT – 1001 0100 1110 1000 (opcode 0x94E8) */
    else if (op == 0x94E8)
    {
        CLRBIT(c->sreg, F_T);
        c->cycles += 1;
        TRACE("PC=%06lX  %04X  CLT  (T=0)\n",
              (unsigned long)cur_pc, op);
    }

    /* ==================== UNKNOWN ==================== */

    else
    {
        fprintf(stderr, "Unknown opcode %04X at PC=%06lX\n",
                op, (unsigned long)cur_pc);
        c->running = 0;
    }
}

/*--------------------------------------------------------------------
  Intel-HEX loader
 --------------------------------------------------------------------*/
static int load_hex(avr_t *c, const char *fn)
{
    FILE *f = fopen(fn, "r");
    if (!f)
    {
        perror(fn);
        return -1;
    }
    char line[600];
    uint32_t base = 0;
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] != ':')
            continue;
        unsigned cnt, addr, type, chk = 0, i;
        sscanf(line + 1, "%2x%4x%2x", &cnt, &addr, &type);
        for (i = 0; i < cnt + 4; i++)
        {
            unsigned v;
            sscanf(line + 1 + i * 2, "%2x", &v);
            chk += v;
        }
        unsigned byte;
        sscanf(line + 1 + (cnt + 4) * 2, "%2x", &byte);
        chk += byte;
        if ((chk & 0xFF) != 0)
        {
            fprintf(stderr, "HEX checksum error\n");
            return -1;
        }
        if (type == 0)
        {
            for (i = 0; i < cnt; i++)
            {
                unsigned v;
                sscanf(line + 9 + i * 2, "%2x", &v);
                uint32_t a = base + addr + i;
                if (a >= FLASH_BYTES)
                {
                    fprintf(stderr, "hex overflows flash\n");
                    return -1;
                }
                c->flash[a] = v;
            }
        }
        else if (type == 4)
        {
            unsigned hi;
            sscanf(line + 9, "%4x", &hi);
            base = ((uint32_t)hi) << 16;
        }
        else if (type == 1)
            break;
    }
    fclose(f);
    return 0;
}

/*--------------------------------------------------------------------
  Monitor helpers
 --------------------------------------------------------------------*/
static void dump_regs(avr_t *c)
{
    // 1. General-Purpose Registers (R0-R31) - 8-bit values
    printf("Registers:\n");
    for (int i = 0; i < 32; i++)
    {
        printf("R%-2d = 0x%02X", i, c->R[i]);
        if (i % 4 == 3)
            printf("\n"); // 4 registers per line
        else
            printf(" | ");
    }

    // 2. Special Registers (PC, SP, SREG)
    printf("\nProcessor State:\n");
    printf("PC   = 0x%06lX  (Next instruction)\n", (unsigned long)c->pc);
    printf("SP   = 0x%04X  (Stack Pointer)\n", c->sp);
    printf("SREG = 0x%02X  [", c->sreg);

    // 3. SREG Flags (bit-by-bit breakdown)
    printf("%c%c%c%c%c%c%c%c]  ",
           (c->sreg & 0x80) ? 'I' : '-',  // Global Interrupt
           (c->sreg & 0x40) ? 'T' : '-',  // Bit Copy Storage
           (c->sreg & 0x20) ? 'H' : '-',  // Half Carry
           (c->sreg & 0x10) ? 'S' : '-',  // Sign
           (c->sreg & 0x08) ? 'V' : '-',  // Overflow
           (c->sreg & 0x04) ? 'N' : '-',  // Negative
           (c->sreg & 0x02) ? 'Z' : '-',  // Zero
           (c->sreg & 0x01) ? 'C' : '-'); // Carry

    printf("\nCycles = %llu\n", (unsigned long long)c->cycles);
}

/*--------------------------------------------------------------------
  Init
 --------------------------------------------------------------------*/
static void avr_init(avr_t *c)
{
    memset(c, 0, sizeof(*c));
    c->flash = (uint8_t *)calloc(FLASH_BYTES, 1);
    c->sram = (uint8_t *)calloc(SRAM_BYTES, 1);
    c->eeprom = (uint8_t *)calloc(EEPROM_BYTES, 1);
    c->sp = 0x100 + SRAM_BYTES - 1; /* stack at top of SRAM       */
    c->running = 1;
}

/*--------------------------------------------------------------------
  Main
 --------------------------------------------------------------------*/
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        puts("usage: avr_vm <file.hex> [-t]");
        return 0;
    }
    if (argc > 2 && strcmp(argv[2], "-t") == 0)
        g_trace = 1;

    avr_t cpu;
    avr_init(&cpu);
    if (load_hex(&cpu, argv[1]))
        return 1;

    puts("Starting …  (enter 'q' to quit, 'd' to dump regs)");
    while (cpu.running)
    {
        /* very small interactive console */
        if (fileno(stdin) >= 0 && fcntl(fileno(stdin), F_GETFL) != -1)
        {
            int ch = getchar();
            if (ch == 'q')
            {
                puts("quit");
                break;
            }
            else if (ch == 'd')
            {
                dump_regs(&cpu);
            }
        }

        avr_step(&cpu);
    }
    dump_regs(&cpu);

    return 0;
}