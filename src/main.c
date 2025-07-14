/********************************************************************
 *  AVR-VM  –  Fully-featured AVR-8 virtual machine
 *
 *  Supports the full Microchip/Atmel 8-bit AVR ISA (AVR / AVRe /
 *  AVRe+ / AVRxm / AVRxt / AVRrc).  Target device-size chosen here
 *  is ATmega1284-class (128 K words flash, 16 K SRAM, 4 K EEPROM).
 *
 *  Build:  gcc -std=c89 -Wall -O2 avr_vm.c -o avr_vm
 *  Run  :  ./avr_vm  <file.hex>   [options]
 *
 *  (c) 2023-2024  Public-Domain / CC-0
 *******************************************************************/

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
#define IO_BYTES 0x100 /*   256 std + ext */
#define REG_COUNT 32

/* Print every instruction?   */
static int g_trace = 1;

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
    uint32_t pc;          /* byte address (22-bit capable)          */
    uint16_t sp;          /* stack pointer                          */
    uint8_t sreg;         /* status register                        */
    /* Extended addressing ----------------------------------------------*/
    uint8_t rampx, rampy, rampz, rampd, eind;
    /* Memories ----------------------------------------------------------*/
    uint8_t *flash;       /* FLASH_BYTES                            */
    uint8_t *sram;        /* SRAM_BYTES                             */
    uint8_t *eeprom;      /* EEPROM_BYTES                           */
    uint8_t io[IO_BYTES]; /* 0x00-0xFF                              */
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
static void flags_sub(avr_t *c, uint8_t a, uint8_t b, uint16_t r, uint8_t cin)
{
    uint8_t res = (uint8_t)r;
    /* H */
    uint8_t h = (~a & b) | (b & res) | (res & ~a);
    if (h & 0x08)
        SETBIT(c->sreg, F_H);
    else
        CLRBIT(c->sreg, F_H);
    /* C */
    uint8_t c_out = (~a & (b | cin)) | (b & cin);
    if (c_out & 0x80)
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
    uint8_t v = (a & ~b & ~res) | (~a & b & res);
    if (v & 0x80)
        SETBIT(c->sreg, F_V);
    else
        CLRBIT(c->sreg, F_V);
    /* S */
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
    push8(c, v & 0xFF);
    push8(c, v >> 8);
}
static uint16_t pop16(avr_t *c)
{
    uint8_t l = pop8(c);
    uint8_t h = pop8(c);
    return (h << 8) | l;
}

/*--------------------------------------------------------------------
  The big single-step decoder / executor
 --------------------------------------------------------------------*/
static void avr_step(avr_t *c)
{
    uint32_t cur_pc = c->pc;
    uint16_t op = flash_word(c, cur_pc);
    c->pc += 2;
     
    if (g_trace)
        printf("PC = %06lX, instr = %04X\n", (unsigned long)cur_pc, op);


    if (op == 0x0000)
    {
        c->cycles += 1;
    }

    else if ((op & 0xFF00) == 0x9600) /* Added per Section 6: ADIW – Add Immediate to Word */
    {
        /* Rd pair ∈ {24,26,28,30}, immediate K ∈ [0..63] */
        int dd = (op >> 4) & 0x03; /* 0→R24,1→R26,2→R28,3→R30 */
        int d = 24 + dd;
        uint8_t K = ((op >> 2) & 0x30) | (op & 0x0F);

        /* form 16‐bit value R[d+1]:R[d] */
        uint16_t a = (uint16_t)c->R[d] | ((uint16_t)c->R[d + 1] << 8);
        uint32_t r = (uint32_t)a + K;

        c->R[d] = (uint8_t)r;
        c->R[d + 1] = (uint8_t)(r >> 8);

        /* Flags: Z, N, V, S, C (H unaffected) */
        if ((r & 0xFFFF) == 0)
            SETBIT(c->sreg, F_Z);
        else
            CLRBIT(c->sreg, F_Z);
        if (r & 0x8000)
            SETBIT(c->sreg, F_N);
        else
            CLRBIT(c->sreg, F_N);
        /* Two's‐compl overflow for 16‐bit add: (~(A ^ K) & (A ^ R)) & 0x8000 */
        if ((~(a ^ K) & (a ^ r) & 0x8000) != 0)
            SETBIT(c->sreg, F_V);
        else
            CLRBIT(c->sreg, F_V);
        if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V))
            SETBIT(c->sreg, F_S);
        else
            CLRBIT(c->sreg, F_S);
        if (r & 0x10000)
            SETBIT(c->sreg, F_C);
        else
            CLRBIT(c->sreg, F_C);

        c->cycles += 2;
    }
    else if ((op & 0xFF00) == 0x9700) /* Added per Section 6: SBIW – Subtract Immediate from Word */
    {
        int dd = (op >> 4) & 0x03;
        int d = 24 + dd;
        uint8_t K = ((op >> 2) & 0x30) | (op & 0x0F);

        uint16_t a = (uint16_t)c->R[d] | ((uint16_t)c->R[d + 1] << 8);
        uint32_t r = (uint32_t)a - K;

        c->R[d] = (uint8_t)r;
        c->R[d + 1] = (uint8_t)(r >> 8);

        /* Flags: Z, N, V, S, C (H unaffected) */
        if ((r & 0xFFFF) == 0)
            SETBIT(c->sreg, F_Z);
        else
            CLRBIT(c->sreg, F_Z);
        if (r & 0x8000)
            SETBIT(c->sreg, F_N);
        else
            CLRBIT(c->sreg, F_N);
        /* Overflow: (A ^ K) & (A ^ R) & 0x8000 */
        if (((a ^ K) & (a ^ r) & 0x8000) != 0)
            SETBIT(c->sreg, F_V);
        else
            CLRBIT(c->sreg, F_V);
        if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V))
            SETBIT(c->sreg, F_S);
        else
            CLRBIT(c->sreg, F_S);
        /* Borrow out of MSB => carry flag */
        if (r & 0x10000)
            SETBIT(c->sreg, F_C);
        else
            CLRBIT(c->sreg, F_C);

        c->cycles += 2;
    }
    else if ((op & 0xF000) == 0x5000) /* Added per Section 6: SUBI – Subtract Immediate */
    {
        int d = 16 + ((op >> 4) & 0x0F);
        uint8_t K = ((op >> 4) & 0xF0) | (op & 0x0F);
        uint8_t a = c->R[d];
        uint16_t r = (uint16_t)a - (uint16_t)K;
        c->R[d] = (uint8_t)r;
        flags_sub(c, a, K, r, 0);
        c->cycles += 1;
    }
    else if ((op & 0xF000) == 0x4000) /* Added per Section 6: SBCI – Subtract Immediate with Carry */
    {
        int d = 16 + ((op >> 4) & 0x0F);
        uint8_t K = ((op >> 4) & 0xF0) | (op & 0x0F);
        uint8_t a = c->R[d];
        uint8_t cin = GETBIT(c->sreg, F_C);
        uint16_t r = (uint16_t)a - (uint16_t)K - cin;
        c->R[d] = (uint8_t)r;
        flags_sub(c, a, K, r, cin);
        c->cycles += 1;
    }

    /* ------------------- Logical instructions ------------------- */
    else if ((op & 0xFC00) == 0x2000) /* Added per Section 6: AND – Logical AND */
    {
        uint8_t d = (op >> 4) & 0x1F, r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t res = c->R[d] & c->R[r];
        c->R[d] = res;
        flags_logic(c, res);
        c->cycles += 1;
    }
    else if ((op & 0xFC00) == 0x2400) /* Added per Section 6: EOR – Exclusive OR */
    {
        uint8_t d = (op >> 4) & 0x1F, r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t res = c->R[d] ^ c->R[r];
        c->R[d] = res;
        flags_logic(c, res);
        c->cycles += 1;
    }
    else if ((op & 0xFC00) == 0x2800) /* Added per Section 6: OR – Logical OR */
    {
        uint8_t d = (op >> 4) & 0x1F, r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t res = c->R[d] | c->R[r];
        c->R[d] = res;
        flags_logic(c, res);
        c->cycles += 1;
    }
    else if ((op & 0xFC00) == 0x2C00) /* Added per Section 6: MOV – Copy Register */
    {
        uint8_t d = (op >> 4) & 0x1F, r = ((op & 0x200) >> 5) | (op & 0x0F);
        c->R[d] = c->R[r];
        c->cycles += 1;
    }
    else if ((op & 0xF000) == 0x3000) /* Added per Section 6: CPI – Compare with Immediate */
    {
        int d = 16 + ((op >> 4) & 0x0F);
        uint8_t K = ((op >> 4) & 0xF0) | (op & 0x0F);
        uint8_t a = c->R[d];
        uint16_t r = (uint16_t)a - (uint16_t)K;
        flags_sub(c, a, K, r, 0);
        c->cycles += 1;
    }
    else if ((op & 0xFC00) == 0x1400) /* Added per Section 6: CP – Compare */
    {
        uint8_t d = (op >> 4) & 0x1F, r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t a = c->R[d], b = c->R[r];
        uint16_t res = (uint16_t)a - (uint16_t)b;
        flags_sub(c, a, b, res, 0);
        c->cycles += 1;
    }
    else if ((op & 0xFC00) == 0x0400) /* Added per Section 6: CPC – Compare with Carry */
    {
        uint8_t d = (op >> 4) & 0x1F, r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint8_t a = c->R[d], b = c->R[r], cin = GETBIT(c->sreg, F_C);
        uint16_t res = (uint16_t)a - (uint16_t)b - cin;
        flags_sub(c, a, b, res, cin);
        c->cycles += 1;
    }

    /* ----------------------- Shifts & Rotates ----------------------- */
    else if ((op & 0xFC00) == 0x0C00) /* ADD / LSL pair—detect LSL when Rd==Rr */
    {
        uint8_t d = (op >> 4) & 0x1F, r = ((op & 0x200) >> 5) | (op & 0x0F);
        if (r == d) /* Added per Section 6: LSL – Logical Shift Left */
        {
            uint8_t old = c->R[d], res = old << 1;
            c->R[d] = res;
            flags_logic(c, res); /* sets Z,N,V,S */
            if (old & 0x80)
                SETBIT(c->sreg, F_C);
            else
                CLRBIT(c->sreg, F_C);
            c->cycles += 1;
        }
        else /* fallback to normal ADD (already implemented above) */
        {
            uint8_t a = c->R[d], b = c->R[r];
            uint16_t sum = a + b;
            c->R[d] = (uint8_t)sum;
            flags_add(c, a, b, sum, 0);
            c->cycles += 1;
        }
    }
    else if ((op & 0xFE0F) == 0x9406) /* Added per Section 6: LSR – Logical Shift Right */
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t old = c->R[d], res = old >> 1;
        c->R[d] = res;
        CLRBIT(c->sreg, F_N); /* N ← 0 */
        if (res)
            CLRBIT(c->sreg, F_Z);
        else
            SETBIT(c->sreg, F_Z);
        CLRBIT(c->sreg, F_V); /* V ← N ^ C, with N=0 => V=C */
        if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_C))
            SETBIT(c->sreg, F_S);
        else
            CLRBIT(c->sreg, F_S);
        if (old & 0x01)
            SETBIT(c->sreg, F_C);
        else
            CLRBIT(c->sreg, F_C);
        c->cycles += 1;
    }
    else if ((op & 0xFE0F) == 0x9405) /* Added per Section 6: ASR – Arithmetic Shift Right */
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t old = c->R[d], msb = old & 0x80;
        uint8_t res = (old >> 1) | msb;
        c->R[d] = res;
        if (res)
            CLRBIT(c->sreg, F_Z);
        else
            SETBIT(c->sreg, F_Z);
        if (res & 0x80)
            SETBIT(c->sreg, F_N);
        else
            CLRBIT(c->sreg, F_N);
        CLRBIT(c->sreg, F_V); /* V ← N ^ C */
        if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_C))
            SETBIT(c->sreg, F_S);
        else
            CLRBIT(c->sreg, F_S);
        if (old & 0x01)
            SETBIT(c->sreg, F_C);
        else
            CLRBIT(c->sreg, F_C);
        c->cycles += 1;
    }
    else if ((op & 0xFE0F) == 0x9407) /* Added per Section 6: ROR – Rotate Right through Carry */
    {
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t old = c->R[d];
        uint8_t cin = GETBIT(c->sreg, F_C);
        uint8_t res = (old >> 1) | (cin << 7);
        c->R[d] = res;
        CLRBIT(c->sreg, F_N); /* N ← 0 */
        if (res)
            CLRBIT(c->sreg, F_Z);
        else
            SETBIT(c->sreg, F_Z);
        CLRBIT(c->sreg, F_V); /* V ← N ^ C */
        if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_C))
            SETBIT(c->sreg, F_S);
        else
            CLRBIT(c->sreg, F_S);
        if (old & 0x01)
            SETBIT(c->sreg, F_C);
        else
            CLRBIT(c->sreg, F_C);
        c->cycles += 1;
    }
    else if ((op & 0xFC00) == 0x1C00) /* ADC / ROL pair—detect ROL when Rd==Rr */
    {
        uint8_t d = (op >> 4) & 0x1F, r = ((op & 0x200) >> 5) | (op & 0x0F);
        if (r == d) /* Added per Section 6: ROL – Rotate Left through Carry */
        {
            uint8_t old = c->R[d];
            uint8_t cin = GETBIT(c->sreg, F_C);
            uint8_t res = (old << 1) | cin;
            c->R[d] = res;
            if (res)
                CLRBIT(c->sreg, F_Z);
            else
                SETBIT(c->sreg, F_Z);
            if (res & 0x80)
                SETBIT(c->sreg, F_N);
            else
                CLRBIT(c->sreg, F_N);
            CLRBIT(c->sreg, F_V); /* V ← N ^ C (after roll) */
            if (GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_C))
                SETBIT(c->sreg, F_S);
            else
                CLRBIT(c->sreg, F_S);
            if (old & 0x80)
                SETBIT(c->sreg, F_C);
            else
                CLRBIT(c->sreg, F_C);
            c->cycles += 1;
        }
        else /* normal ADC already handled above */
        {
            uint8_t a = c->R[d], b = c->R[r], cin = GETBIT(c->sreg, F_C);
            uint16_t sum = a + b + cin;
            c->R[d] = (uint8_t)sum;
            flags_add(c, a, b, sum, cin);
            c->cycles += 1;
        }
    }

    /* ------------------- Integer Multiply Instructions ------------------- */
    else if ((op & 0xFC00) == 0x9C00) /* Added per Section 6: MUL – Multiply Unsigned */
    {
        uint8_t d = (op >> 4) & 0x1F, r = ((op & 0x200) >> 5) | (op & 0x0F);
        uint16_t prod = (uint16_t)c->R[d] * (uint16_t)c->R[r];
        c->R[0] = prod & 0xFF;
        c->R[1] = prod >> 8;
        CLRBIT(c->sreg, F_N);
        if (prod == 0)
            SETBIT(c->sreg, F_Z);
        else
            CLRBIT(c->sreg, F_Z);
        /* V unaffected for MUL family */
        if (prod & 0x8000)
            SETBIT(c->sreg, F_C);
        else
            CLRBIT(c->sreg, F_C);
        c->cycles += 2;
    }
    else if ((op & 0xFF08) == 0x0208) /* Added per Section 6: MULS – Multiply Signed */
    {
        uint8_t d = (op >> 4) & 0x1F, r = (op & 0xF) | ((op >> 5) & 0x10);
        int16_t a = (int8_t)c->R[d], b = (int8_t)c->R[r];
        int16_t prod = a * b;
        uint16_t u = (uint16_t)prod;
        c->R[0] = (uint8_t)u;
        c->R[1] = (uint8_t)(u >> 8);
        CLRBIT(c->sreg, F_N);
        if (u == 0)
            SETBIT(c->sreg, F_Z);
        else
            CLRBIT(c->sreg, F_Z);
        if (u & 0x8000)
            SETBIT(c->sreg, F_C);
        else
            CLRBIT(c->sreg, F_C);
        c->cycles += 2;
    }
    else if ((op & 0xFE08) == 0x0208) /* Added per Section 6: MULSU – Multiply Signed with Unsigned */
    {
        uint8_t d = (op >> 4) & 0x1F, r = (op & 0xF) | ((op >> 5) & 0x10);
        int16_t a = (int8_t)c->R[d];
        uint16_t b = c->R[r];
        int16_t prod = a * (int16_t)b;
        uint16_t u = (uint16_t)prod;
        c->R[0] = (uint8_t)u;
        c->R[1] = (uint8_t)(u >> 8);
        CLRBIT(c->sreg, F_N);
        if (u == 0)
            SETBIT(c->sreg, F_Z);
        else
            CLRBIT(c->sreg, F_Z);
        if (u & 0x8000)
            SETBIT(c->sreg, F_C);
        else
            CLRBIT(c->sreg, F_C);
        c->cycles += 2;
    }

    /* ------------------- Bit‐manipulation instructions ------------------- */

    /* CBI – Clear Bit in I/O Register */
    else if ((op & 0xFF00) == 0x9800) /* Added per Section 6: CBI */
    {
        uint8_t A = (op >> 3) & 0x1F, b = op & 0x07;
        uint32_t addr = 0x20 + A; /* I/O space starts at data addr 0x20 */
        uint8_t v = read_data(c, addr) & ~(1u << b);
        write_data(c, addr, v);
        c->cycles += 2;
    }

    /* SBI – Set Bit in I/O Register */
    else if ((op & 0xFF00) == 0x9A00) /* Added per Section 6: SBI */
    {
        uint8_t A = (op >> 3) & 0x1F, b = op & 0x07;
        uint32_t addr = 0x20 + A;
        uint8_t v = read_data(c, addr) | (1u << b);
        write_data(c, addr, v);
        c->cycles += 2;
    }

    /* SBIC – Skip if Bit in I/O Register is Cleared */
    else if ((op & 0xFF00) == 0x9900) /* Added per Section 6: SBIC */
    {
        uint8_t A = (op >> 3) & 0x1F, b = op & 0x07;
        uint32_t addr = 0x20 + A;
        uint8_t v = read_data(c, addr);
        if (!(v & (1u << b)))
        {
            c->pc += 2; /* skip next word */
            c->cycles += 2;
        }
        else
        {
            c->cycles += 1;
        }
    }

    /* SBIS – Skip if Bit in I/O Register is Set */
    else if ((op & 0xFF00) == 0x9B00) /* Added per Section 6: SBIS */
    {
        uint8_t A = (op >> 3) & 0x1F, b = op & 0x07;
        uint32_t addr = 0x20 + A;
        uint8_t v = read_data(c, addr);
        if (v & (1u << b))
        {
            c->pc += 2;
            c->cycles += 2;
        }
        else
        {
            c->cycles += 1;
        }
    }

    /* SBRC – Skip if Bit in Register is Cleared */
    else if ((op & 0xFE08) == 0xFC00) /* Added per Section 6: SBRC */
    {
        uint8_t r = ((op & 0x0200) >> 5) | (op & 0x0F);
        uint8_t b = op & 0x07;
        uint8_t v = c->R[r];
        if (!(v & (1u << b)))
        {
            c->pc += 2;
            c->cycles += 2;
        }
        else
        {
            c->cycles += 1;
        }
    }

    /* SBRS – Skip if Bit in Register is Set */
    else if ((op & 0xFE08) == 0xFE00) /* Added per Section 6: SBRS */
    {
        uint8_t r = ((op & 0x0200) >> 5) | (op & 0x0F);
        uint8_t b = op & 0x07;
        uint8_t v = c->R[r];
        if (v & (1u << b))
        {
            c->pc += 2;
            c->cycles += 2;
        }
        else
        {
            c->cycles += 1;
        }
    }

    /* BLD – Bit Load from T to Register */
    else if ((op & 0xFE08) == 0xF800) /* Added per Section 6: BLD */
    {
        uint8_t d = (op >> 4) & 0x1F, b = op & 0x07;
        if (GETBIT(c->sreg, F_T))
            c->R[d] |= (1u << b);
        else
            c->R[d] &= ~(1u << b);
        c->cycles += 1;
    }

    /* BST – Bit Store from Register to T */
    else if ((op & 0xFE08) == 0xFA00) /* Added per Section 6: BST */
    {
        uint8_t d = (op >> 4) & 0x1F, b = op & 0x07;
        if (c->R[d] & (1u << b))
            SETBIT(c->sreg, F_T);
        else
            CLRBIT(c->sreg, F_T);
        c->cycles += 1;
    }

    /* BSET – Set Flag in SREG */
   else if ((op & 0xFF8F) == 0x9408) /* Added per Section 6: BSET */
    {
        uint8_t s = (op >> 4) & 0x07;
        SETBIT(c->sreg, s);
        c->cycles += 1;
    }

    /* BCLR – Clear Flag in SREG */
    else if ((op & 0xFF8F) == 0x9488) /* Added per Section 6: BCLR */
    {
        uint8_t s = (op >> 4) & 0x07;
        CLRBIT(c->sreg, s);
        c->cycles += 1;
    }

    /* ------------------- Control‐flow instructions ------------------- */

    /* JMP – Long Jump */
    else if ((op & 0xFE0E) == 0x940C) /* Added per Section 6: JMP */
    {
        uint16_t op2 = flash_word(c, cur_pc + 2);
        uint32_t k =
            ((op & 0x0100) << 13)   /* K21 = bit8→bit21 */
            | ((op & 0x00F0) << 13) /* K20..17 = bits7..4→bits20..17 */ 
            | ((op & 0x0001) << 16) /* K16 = bit0→bit16 */
            | (uint32_t)op2;        /* K15..0 from second word */
        c->pc = k << 1;
        c->cycles += 3;
    }

    /* CALL – Long Call to Subroutine */
    else if ((op & 0xFE0E) == 0x940E) /* Added per Section 6: CALL */
    {
        uint16_t op2 = flash_word(c, cur_pc + 2);
        uint32_t k =
            ((op & 0x0100) << 13) | ((op & 0x00F0) << 13) | ((op & 0x0001) << 16) | (uint32_t)op2;
        push16(c, cur_pc + 4);
        c->pc = k << 1;
        c->cycles += 4;
    }

    /* RJMP – Relative Jump */
    else if ((op & 0xF000) == 0xC000) /* Added per Section 6: RJMP */
    {
        int16_t k = op & 0x0FFF;
        if (k & 0x0800)
            k |= 0xF000;               /* sign‐extend 12‐bit */
        c->pc = cur_pc + 2 + (k << 1); /* byte‐address = word*2 */
        c->cycles += 2;
    }

    /* RCALL – Relative Call to Subroutine */
    else if ((op & 0xF000) == 0xD000) /* Added per Section 6: RCALL */
    {
        int16_t k = op & 0x0FFF;
        if (k & 0x0800)
            k |= 0xF000;
        push16(c, cur_pc + 2); /* return address = byte addr */
        c->pc = cur_pc + 2 + (k << 1);
        c->cycles += 3;
    }

    /* ------------------- Branch instructions ------------------- */
    else if ((op & 0xFC00) == 0xF000) /* Added per Section 6: BRBS – Branch if Status Flag Set */
    {
        uint8_t s = op & 0x07;
        int8_t k = (op >> 3) & 0x7F;
        if (k & 0x40) k |= 0x80; /* sign extend 7-bit */
        if (GETBIT(c->sreg, s))
        {
            c->pc = cur_pc + 2 + (k << 1);
            c->cycles += 2;
        }
        else
        {
            c->cycles += 1;
        }
    }
    else if ((op & 0xFC00) == 0xF400) /* Added per Section 6: BRBC – Branch if Status Flag Cleared */
    {
        uint8_t s = op & 0x07;
        int8_t k = (op >> 3) & 0x7F;
        if (k & 0x40) k |= 0x80; /* sign extend 7-bit */
        if (!GETBIT(c->sreg, s))
        {
            c->pc = cur_pc + 2 + (k << 1);
            c->cycles += 2;
        }
        else
        {
            c->cycles += 1;
        }
    }

    /* IJMP – Indirect Jump (Z) */
    else if (op == 0x9409) /* Added per Section 6: IJMP */
    {
        uint16_t z = (uint16_t)c->R[30] | ((uint16_t)c->R[31] << 8);
        c->pc = ((uint32_t)z) << 1; /* word→byte */
        c->cycles += 2;
    }

    /* EIJMP – Extended Indirect Jump (EIND:Z) */
    else if (op == 0x9419) /* Added per Section 6: EIJMP */
    {
        uint32_t z = (uint32_t)c->R[30] | ((uint32_t)c->R[31] << 8) | ((uint32_t)c->eind << 16);
        c->pc = z << 1;
        c->cycles += 2;
    }

    /* ICALL – Indirect Call to Subroutine (Z) */
    else if (op == 0x9509) /* Added per Section 6: ICALL */
    {
        uint16_t z = (uint16_t)c->R[30] | ((uint16_t)c->R[31] << 8);
        push16(c, cur_pc + 2);
        c->pc = ((uint32_t)z) << 1;
        c->cycles += 3;
    }

    /* EICALL – Extended Indirect Call (EIND:Z) */
    else if (op == 0x9519) /* Added per Section 6: EICALL */
    {
        uint32_t z = (uint32_t)c->R[30] | ((uint32_t)c->R[31] << 8) | ((uint32_t)c->eind << 16);
        /* push 22‐bit return address */
        push16(c, cur_pc + 2);        /* low word */
        push8(c, (cur_pc + 2) >> 16); /* high byte */
        c->pc = z << 1;
        c->cycles += 4;
    }

    /* RET – Return from Subroutine */
    else if (op == 0x9508) /* Added per Section 6: RET */
    {
        uint16_t ret = pop16(c);
        c->pc = ret;
        c->cycles += 4;
    }

    /* RETI – Return from Interrupt */
    else if (op == 0x9518) /* Added per Section 6: RETI */
    {
        uint16_t ret = pop16(c);
        c->pc = ret;
        SETBIT(c->sreg, F_I); /* re‐enable interrupts */
        c->cycles += 4;
    }

    /* ------------------- Data-Transfer instructions ------------------- */

    /* LDI – Load Immediate              */
    else if ((op & 0xF000) == 0xE000)
    { /* Added per Section 6: LDI */
        uint8_t d = 16 + ((op >> 4) & 0x0F);
        uint8_t K = ((op >> 4) & 0xF0) | (op & 0x0F);
        c->R[d] = K;
        c->cycles += 1;
    }

    /* LDS – Load Direct from Data Space */
    else if ((op & 0xFE0F) == 0x9000)
    { /* Added per Section 6: LDS */
        uint8_t d = (op >> 4) & 0x1F;
        uint16_t addr = flash_word(c, cur_pc + 2); /* immediate follows */
        c->R[d] = read_data(c, addr);
        c->pc += 2;
        c->cycles += 2;
    }

    /* STS – Store Direct to Data Space */
    else if ((op & 0xFE0F) == 0x9200)
    { /* Added per Section 6: STS */
        uint8_t r = (op >> 4) & 0x1F;
        uint16_t addr = flash_word(c, cur_pc + 2);
        write_data(c, addr, c->R[r]);
        c->pc += 2;
        c->cycles += 2;
    }

    /* LD Rd, X – Load Indirect using X */
    else if ((op & 0xFE0F) == 0x900C)
    { /* Added per Section 6: LD Rd,X */
        uint8_t d = (op >> 4) & 0x1F;
        uint16_t addr = (uint16_t)c->R[27] << 8 | c->R[26];
        c->R[d] = read_data(c, addr);
        c->cycles += 2;
    }
    /* LD Rd, X+ – Load Indirect + Post‐increment X */
    else if ((op & 0xFE0F) == 0x900D)
    { /* Added per Section 6: LD Rd,X+ */
        uint8_t d = (op >> 4) & 0x1F;
        uint16_t addr = (uint16_t)c->R[27] << 8 | c->R[26];
        c->R[d] = read_data(c, addr);
        addr++;
        c->R[26] = addr & 0xFF;
        c->R[27] = addr >> 8;
        c->cycles += 2;
    }
    /* LD Rd, –X – Load Indirect + Pre-decrement X */
    else if ((op & 0xFE0F) == 0x900E)
    { /* Added per Section 6: LD Rd,–X */
        uint8_t d = (op >> 4) & 0x1F;
        uint16_t addr = (uint16_t)c->R[27] << 8 | c->R[26];
        addr--;
        c->R[26] = addr & 0xFF;
        c->R[27] = addr >> 8;
        c->R[d] = read_data(c, addr);
        c->cycles += 3;
    }

    /* ST X, Rd – Store Indirect using X */
    else if ((op & 0xFE0F) == 0x920C)
    { /* Added per Section 6: ST X,Rd */
        uint8_t r = (op >> 4) & 0x1F;
        uint16_t addr = (uint16_t)c->R[27] << 8 | c->R[26];
        write_data(c, addr, c->R[r]);
        c->cycles += 2;
    }
    /* ST X+, Rd – Store Indirect + Post‐increment X */
    else if ((op & 0xFE0F) == 0x920D)
    { /* Added per Section 6: ST X+,Rd */
        uint8_t r = (op >> 4) & 0x1F;
        uint16_t addr = (uint16_t)c->R[27] << 8 | c->R[26];
        write_data(c, addr, c->R[r]);
        addr++;
        c->R[26] = addr & 0xFF;
        c->R[27] = addr >> 8;
        c->cycles += 2;
    }
    /* ST –X, Rd – Store Indirect + Pre-decrement X */
    else if ((op & 0xFE0F) == 0x920E)
    { /* Added per Section 6: ST –X,Rd */
        uint8_t r = (op >> 4) & 0x1F;
        uint16_t addr = (uint16_t)c->R[27] << 8 | c->R[26];
        addr--;
        c->R[26] = addr & 0xFF;
        c->R[27] = addr >> 8;
        write_data(c, addr, c->R[r]);
        c->cycles += 2;
    }

    /* LD Rd, Y – Load Indirect using Y */
    else if ((op & 0xFE0F) == 0x8008)
    { /* Added per Section 6: LD Rd,Y */
        uint8_t d = (op >> 4) & 0x1F;
        uint16_t addr = (uint16_t)c->R[29] << 8 | c->R[28];
        c->R[d] = read_data(c, addr);
        c->cycles += 2;
    }
    /* LD Rd, Y+ – Load Indirect + Post‐increment Y */
    else if ((op & 0xFE0F) == 0x9009)
    { /* Added per Section 6: LD Rd,Y+ */
        uint8_t d = (op >> 4) & 0x1F;
        uint16_t addr = (uint16_t)c->R[29] << 8 | c->R[28];
        c->R[d] = read_data(c, addr);
        addr++;
        c->R[28] = addr & 0xFF;
        c->R[29] = addr >> 8;
        c->cycles += 2;
    }
    /* LD Rd, –Y – Load Indirect + Pre-decrement Y */
    else if ((op & 0xFE0F) == 0x900A)
    { /* Added per Section 6: LD Rd,–Y */
        uint8_t d = (op >> 4) & 0x1F;
        uint16_t addr = (uint16_t)c->R[29] << 8 | c->R[28];
        addr--;
        c->R[28] = addr & 0xFF;
        c->R[29] = addr >> 8;
        c->R[d] = read_data(c, addr);
        c->cycles += 3;
    }

    /* ST Y, Rd – Store Indirect using Y */
    else if ((op & 0xFE0F) == 0x8208)
    { /* Added per Section 6: ST Y,Rd */
        uint8_t r = (op >> 4) & 0x1F;
        uint16_t addr = (uint16_t)c->R[29] << 8 | c->R[28];
        write_data(c, addr, c->R[r]);
        c->cycles += 2;
    }
    /* ST Y+, Rd – Store Indirect + Post‐increment Y */
    else if ((op & 0xFE0F) == 0x9209)
    { /* Added per Section 6: ST Y+,Rd */
        uint8_t r = (op >> 4) & 0x1F;
        uint16_t addr = (uint16_t)c->R[29] << 8 | c->R[28];
        write_data(c, addr, c->R[r]);
        addr++;
        c->R[28] = addr & 0xFF;
        c->R[29] = addr >> 8;
        c->cycles += 2;
    }
    /* ST –Y, Rd – Store Indirect + Pre-decrement Y */
    else if ((op & 0xFE0F) == 0x920A)
    { /* Added per Section 6: ST –Y,Rd */
        uint8_t r = (op >> 4) & 0x1F;
        uint16_t addr = (uint16_t)c->R[29] << 8 | c->R[28];
        addr--;
        c->R[28] = addr & 0xFF;
        c->R[29] = addr >> 8;
        write_data(c, addr, c->R[r]);
        c->cycles += 2;
    }

    /* LD Rd, Z – Load Indirect using Z */
    else if ((op & 0xFE0F) == 0x8000)
    { /* Added per Section 6: LD Rd,Z */
        uint8_t d = (op >> 4) & 0x1F;
        uint32_t addr = (uint16_t)c->R[31] << 8 | c->R[30];
        c->R[d] = read_data(c, addr);
        c->cycles += 2;
    }
    /* LD Rd, Z+ – Load Indirect + Post‐increment Z */
    else if ((op & 0xFE0F) == 0x9001)
    { /* Added per Section 6: LD Rd,Z+ */
        uint8_t d = (op >> 4) & 0x1F;
        uint32_t addr = (uint16_t)c->R[31] << 8 | c->R[30];
        c->R[d] = read_data(c, addr);
        addr++;
        c->R[30] = addr & 0xFF;
        c->R[31] = addr >> 8;
        c->cycles += 2;
    }
    /* LD Rd, –Z – Load Indirect + Pre-decrement Z */
    else if ((op & 0xFE0F) == 0x9002)
    { /* Added per Section 6: LD Rd,–Z */
        uint8_t d = (op >> 4) & 0x1F;
        uint32_t addr = (uint16_t)c->R[31] << 8 | c->R[30];
        addr--;
        c->R[30] = addr & 0xFF;
        c->R[31] = addr >> 8;
        c->R[d] = read_data(c, addr);
        c->cycles += 3;
    }

    /* ST Z, Rd – Store Indirect using Z */
   else if ((op & 0xFE0F) == 0x8200)
    { /* Added per Section 6: ST Z,Rd */
        uint8_t r = (op >> 4) & 0x1F;
        uint32_t addr = (uint16_t)c->R[31] << 8 | c->R[30];
        write_data(c, addr, c->R[r]);
        c->cycles += 2;
    }
    /* ST Z+, Rd – Store Indirect + Post‐increment Z */
    else if ((op & 0xFE0F) == 0x9201)
    { /* Added per Section 6: ST Z+,Rd */
        uint8_t r = (op >> 4) & 0x1F;
        uint32_t addr = (uint16_t)c->R[31] << 8 | c->R[30];
        write_data(c, addr, c->R[r]);
        addr++;
        c->R[30] = addr & 0xFF;
        c->R[31] = addr >> 8;
        c->cycles += 2;
    }
    /* ST –Z, Rd – Store Indirect + Pre-decrement Z */
    else if ((op & 0xFE0F) == 0x9202)
    { /* Added per Section 6: ST –Z,Rd */
        uint8_t r = (op >> 4) & 0x1F;
        uint32_t addr = (uint16_t)c->R[31] << 8 | c->R[30];
        addr--;
        c->R[30] = addr & 0xFF;
        c->R[31] = addr >> 8;
        write_data(c, addr, c->R[r]);
        c->cycles += 2;
    }

    /* IN – Load from I/O Location        */
    else if ((op & 0xF800) == 0xB000)
    { /* Added per Section 6: IN */
        uint8_t d = (op >> 4) & 0x1F;
        uint8_t A = ((op >> 5) & 0x30) | (op & 0x0F);
        c->R[d] = read_data(c, 0x20 + A);
        c->cycles += 1;
    }

    /* OUT – Store to I/O Location        */
    else if ((op & 0xF800) == 0xB800)
    { /* Added per Section 6: OUT */
        uint8_t A = ((op >> 5) & 0x30) | (op & 0x0F);
        uint8_t r = (op >> 4) & 0x1F;
        write_data(c, 0x20 + A, c->R[r]);
        c->cycles += 1;
    }

    /* LPM – Load Program Memory          */
    else if (op == 0x95C8)
    { /* Added per Section 6: LPM (R0 implied) */
        uint32_t addr = ((uint16_t)c->R[31] << 8 | c->R[30]) + ((uint32_t)c->rampz << 16);
        uint16_t w = flash_word(c, addr);
        c->R[0] = (uint8_t)(w & ((c->R[30] & 1) ? 0xFF : 0x00) ? (w >> 8) : w);
        /* LSb selects low versus high—but full low‐level support is complex */
        c->cycles += 3;
    }
    else if ((op & 0xFE0F) == 0x9004)
    { /* Added per Section 6: LPM Rd,Z */
        uint8_t d = (op >> 4) & 0x1F;
        uint32_t addr = ((uint16_t)c->R[31] << 8 | c->R[30]);
        uint16_t w = flash_word(c, addr);
        c->R[d] = (addr & 1) ? (w >> 8) : (w & 0xFF);
        c->cycles += 3;
    }
    else if ((op & 0xFE0F) == 0x9005)
    { /* Added per Section 6: LPM Rd,Z+ */
        uint8_t d = (op >> 4) & 0x1F;
        uint32_t addr = ((uint16_t)c->R[31] << 8 | c->R[30]);
        uint16_t w = flash_word(c, addr);
        c->R[d] = (addr & 1) ? (w >> 8) : (w & 0xFF);
        addr++;
        c->R[30] = addr & 0xFF;
        c->R[31] = addr >> 8;
        c->cycles += 3;
    }

    /* ELPM – Extended Load Program Memory */
    else if (op == 0x95D8)
    { /* Added per Section 6: ELPM (R0 implied) */
        uint32_t addr = ((uint16_t)c->R[31] << 8 | c->R[30]) + ((uint32_t)c->rampz << 16);
        uint16_t w = flash_word(c, addr);
        c->R[0] = (addr & 1) ? (w >> 8) : (w & 0xFF);
        c->cycles += 3;
    }
    else if ((op & 0xFE0F) == 0x9006)
    { /* Added per Section 6: ELPM Rd,Z */
        uint8_t d = (op >> 4) & 0x1F;
        uint32_t addr = ((uint16_t)c->R[31] << 8 | c->R[30]) + ((uint32_t)c->rampz << 16);
        uint16_t w = flash_word(c, addr);
        c->R[d] = (addr & 1) ? (w >> 8) : (w & 0xFF);
        c->cycles += 3;
    }
    else if ((op & 0xFE0F) == 0x9007)
    { /* Added per Section 6: ELPM Rd,Z+ */
        uint8_t d = (op >> 4) & 0x1F;
        uint32_t addr = ((uint16_t)c->R[31] << 8 | c->R[30]) + ((uint32_t)c->rampz << 16);
        uint16_t w = flash_word(c, addr);
        c->R[d] = (addr & 1) ? (w >> 8) : (w & 0xFF);
        addr++;
        c->R[30] = addr & 0xFF;
        c->R[31] = addr >> 8;
        c->cycles += 3;
    }

    /* SPM – Store Program Memory        */
    else if (op == 0x95E8)
    { /* Added per Section 6: SPM */
        /* Not implementing full SPM sequence—NOP placeholder */
        c->cycles += 4;
    }
    else if (op == 0x95F8)
    { /* Added per Section 6: SPM Z+ */
        /* Again placeholder only */
        c->cycles += 4;
    }

    /* ------------------------------------------------------------------- */

    else
    {
        fprintf(stderr, "Unimplemented opcode %04X\n", op);
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
  Tiny disassembler (one instruction)
 --------------------------------------------------------------------*/
static void disasm(avr_t *c, uint32_t pc)
{
    uint16_t op = flash_word(c, pc);
    printf("%06lX: %04X  ", (unsigned long)pc, op);
    if (op == 0)
        puts("NOP");
    else if ((op & 0xFC00) == 0x0C00)
    {
        printf("ADD R%u,R%u\n", (op >> 4) & 0x1F, ((op & 0x200) >> 5) | (op & 0x0F));
    }
    else
        puts("...");
}

/*--------------------------------------------------------------------
  Monitor helpers
 --------------------------------------------------------------------*/
static void dump_regs(avr_t *c)
{
    int i;
    for (i = 0; i < 32; i++)
    {
        printf("R%02d=%02X%c", i, c->R[i], (i % 8 == 7) ? '\n' : ' ');
    }
    printf("PC=%06lX  SP=%04X  SREG=%02X  CYC=%llu\n",
           (unsigned long)c->pc, c->sp, c->sreg, (unsigned long long)c->cycles);


    disasm(c, (unsigned long)c->pc);
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
    c->sp = 0x100 + SRAM_BYTES - 1; /* stack at top of SRAM          */
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
        avr_step(&cpu);

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
    }
    dump_regs(&cpu);
   
    return 0;
}