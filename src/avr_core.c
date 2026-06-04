/*
 * Copyright 2025-present Isak Ruas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * avr_core.c -- AVR 8-bit CPU core.
 *
 * The decoder matches AVR opcodes by mnemonic and bit pattern.
 */

#include "avr_core.h"

#include "avr_devices.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIT(n) (1u << (n))
#define GETBIT(v, n) (((v) >> (n)) & 1)
#define SETBIT(v, n) ((v) |= BIT(n))
#define CLRBIT(v, n) ((v) &= ~BIT(n))

/* Trace prints the disassembled instruction when cpu->trace is non-zero. */
#define TRACE(c, ...)                                                          \
  do {                                                                         \
    if ((c)->trace)                                                            \
      printf(__VA_ARGS__);                                                     \
  } while (0)

/* Classic AVR EEPROM I/O registers (data-space absolute addresses:
 * 0x3F=EECR, 0x40=EEDR, 0x41=EEARL, 0x42=EEARH). These are represented in the
 * io[] window with a 0x20 offset removed. */
#define IO_EECR 0x1F
#define IO_EEDR 0x20
#define IO_EEARL 0x21
#define IO_EEARH 0x22

/* -------------------------------------------------------------------------
 * Memory and pointer helpers
 * ------------------------------------------------------------------------- */

static void set_flag(avr_t *c, int bit, int val) {
  if (val)
    SETBIT(c->sreg, bit);
  else
    CLRBIT(c->sreg, bit);
}

/* Per-core base cycle count used by the VM. The AVRe column also represents
 * AVRe+; the generic core (AVR_CORE_UNKNOWN) uses it too. Counts are the
 * nominal internal-SRAM, no-wait-state, 16-bit-PC values; external-memory
 * wait states and the >128 KB PC penalty are not modeled. */
static int cyc(const avr_t *c, int e, int xm, int xt, int rc) {
  switch (c->core) {
  case AVR_CORE_XM:
    return xm;
  case AVR_CORE_XT:
    return xt;
  case AVR_CORE_RC:
    return rc;
  default:
    return e; /* AVRe, AVRe+, generic */
  }
}

/* Cycles for an LD/ST via pointer: is_store and mode (0=none,1=+,2=-). */
static int ld_st_cyc(const avr_t *c, int is_store, int mode) {
  if (is_store) {
    if (mode == 2)
      return cyc(c, 2, 2, 1, 2); /* ST -X/-Y/-Z */
    return cyc(c, 2, 1, 1, 1);   /* ST X/X+ */
  }
  if (mode == 0)
    return cyc(c, 2, 2, 2, 1); /* LD X/Y/Z   */
  if (mode == 1)
    return cyc(c, 2, 2, 2, 2); /* LD X+/Y+/Z+ */
  return cyc(c, 2, 3, 2, 2);   /* LD -X/-Y/-Z */
}

/* Flash is stored little-endian (low byte first), matching .hex layout. */
uint16_t avr_flash_word(const avr_t *c, uint32_t byte_addr) {
  return (uint16_t)(c->flash[byte_addr] | (c->flash[byte_addr + 1] << 8));
}

void avr_put_op(avr_t *c, uint32_t byte_addr, uint16_t op) {
  c->flash[byte_addr] = (uint8_t)(op & 0xFF);
  c->flash[byte_addr + 1] = (uint8_t)((op >> 8) & 0xFF);
}

#include "eeprom_addrs.h"
#include "spi_addrs.h"
#include "twi_addrs.h"
#include "uart_addrs.h"

static const eeprom_hw_t* get_ee_hw(const char* name) {
    for (size_t i = 0; i < sizeof(eeprom_hw_table)/sizeof(eeprom_hw_table[0]); i++) {
        if (strcmp(eeprom_hw_table[i].name, name) == 0) return &eeprom_hw_table[i];
    }
    return NULL;
}

static const spi_hw_t* get_spi_hw(const char* name) {
    for (size_t i = 0; i < sizeof(spi_hw_table)/sizeof(spi_hw_table[0]); i++) {
        if (strcmp(spi_hw_table[i].name, name) == 0) return &spi_hw_table[i];
    }
    return NULL;
}

static const twi_hw_t* get_twi_hw(const char* name) {
    for (size_t i = 0; i < sizeof(twi_hw_table)/sizeof(twi_hw_table[0]); i++) {
        if (strcmp(twi_hw_table[i].name, name) == 0) return &twi_hw_table[i];
    }
    return NULL;
}

static const uart_hw_t* get_uart_hw(const char* name) {
    for (size_t i = 0; i < sizeof(uart_hw_table)/sizeof(uart_hw_table[0]); i++) {
        if (strcmp(uart_hw_table[i].name, name) == 0) return &uart_hw_table[i];
    }
    return NULL;
}

static void eeprom_handle_eecr_write(avr_t *c, uint8_t old_eecr, uint8_t new_eecr, const eeprom_hw_t* ee) {
  uint32_t eear = (uint32_t)c->io[ee->addr_l] | ((uint32_t)(c->io[ee->addr_h] & 0x0F) << 8);

  // EEMPE (bit 2) write handling: set the hardware 4-cycle timer
  if (new_eecr & BIT(2)) {
    c->eempe_timer = 4;
  } else {
    c->eempe_timer = 0;
  }

  // EERE (bit 0) read handling
  if ((new_eecr & BIT(0)) && !(old_eecr & BIT(0))) {
    if (eear < c->eeprom_bytes) {
      c->io[ee->data] = c->eeprom[eear];
    }
    CLRBIT(c->io[ee->ctrl], 0); // Clear EERE immediately
  }

  // EEPE (bit 1) write handling: must be set while EEMPE timer is active
  if ((new_eecr & BIT(1)) && !(old_eecr & BIT(1))) {
    if (c->eempe_timer > 0) {
      // Buffer the address and data, start programming cycle
      c->eeprom_write_addr = eear;
      c->eeprom_write_val = c->io[ee->data];
      c->eeprom_write_cycles_left = 64; // 64 clock cycles delay
      
      c->eempe_timer = 0;
      CLRBIT(c->io[ee->ctrl], 2); // Clear EEMPE immediately
    } else {
      // If EEMPE is not active, EEPE write has no effect
      CLRBIT(c->io[ee->ctrl], 1);
    }
  }
}

static void eeprom_handle_modern_write(avr_t *c, uint8_t new_ctrl, const eeprom_hw_t* ee) {
    uint32_t eear = (uint32_t)c->io[ee->addr_l] | ((uint32_t)(c->io[ee->addr_h] & 0x0F) << 8);
    // In Modern AVRs, CMD 0x04 is Erase&Write.
    if (new_ctrl == 0x04) {
      c->eeprom_write_addr = eear;
      c->eeprom_write_val = c->io[ee->data];
      c->eeprom_write_cycles_left = 64;
      c->io[ee->status] |= 0x01; // Set EEBUSY
    }
}

uint8_t avr_read_data(avr_t *c, uint32_t addr) {
  if (addr < AVR_REG_COUNT)
    return c->R[addr];
  if (c->core != AVR_CORE_RC) {
    if (addr == 0x5F) return c->sreg;
    if (addr == 0x5D) return (uint8_t)(c->sp & 0xFF);
    if (addr == 0x5E) return (uint8_t)((c->sp >> 8) & 0xFF);
  }
  if (addr < c->sram_start) {
    uint32_t io_addr = addr - 0x20;
    
    // Support Modern AVR EEPROM reading
    const eeprom_hw_t* ee = get_ee_hw(c->device);
    if (ee && ee->is_modern && io_addr == ee->data) {
       uint32_t eear = (uint32_t)c->io[ee->addr_l] | ((uint32_t)(c->io[ee->addr_h] & 0x0F) << 8);
       if (eear < c->eeprom_bytes) return c->eeprom[eear];
    }

    const spi_hw_t* spi = get_spi_hw(c->device);
    if (spi && io_addr == spi->status) {
       return c->io[io_addr] | 0x80; // SPIF
    }

    const twi_hw_t* twi = get_twi_hw(c->device);
    if (twi && io_addr == twi->ctrl) {
       return c->io[io_addr] | 0x80; // TWINT
    }

    const uart_hw_t* uart = get_uart_hw(c->device);
    if (uart && io_addr == uart->status) {
       return c->io[io_addr] | 0x60; // UDRE | TXC
    }

    return c->io[io_addr];
  }
  if (addr - c->sram_start < c->sram_bytes)
    return c->sram[addr - c->sram_start];
  fprintf(stderr, "Data read OOB 0x%06lX\n", (unsigned long)addr);
  return 0;
}

void avr_write_data(avr_t *c, uint32_t addr, uint8_t v) {
  if (addr < AVR_REG_COUNT) {
    c->R[addr] = v;
    return;
  }
  if (c->core != AVR_CORE_RC) {
    if (addr == 0x5F) { c->sreg = v; return; }
    if (addr == 0x5D) { c->sp = (uint16_t)((c->sp & 0xFF00) | v); return; }
    if (addr == 0x5E) { c->sp = (uint16_t)((c->sp & 0x00FF) | ((uint16_t)v << 8)); return; }
  }
  if (addr < c->sram_start) {
    uint32_t io_addr = addr - 0x20;
    const eeprom_hw_t* ee = get_ee_hw(c->device);
    
    if (ee) {
        if (c->eeprom_write_cycles_left > 0 && 
            (io_addr == ee->ctrl || io_addr == ee->addr_l || io_addr == ee->addr_h || io_addr == ee->data)) {
          return;
        }
    }
    
    uint8_t old = c->io[io_addr];
    c->io[io_addr] = v;
    
    if (ee) {
        if (ee->is_modern == 0 && io_addr == ee->ctrl) {
            eeprom_handle_eecr_write(c, old, v, ee);
        } else if (ee->is_modern == 1 && io_addr == ee->ctrl) {
            eeprom_handle_modern_write(c, v, ee);
        }
    }
    return;
  }
  if (addr - c->sram_start < c->sram_bytes) {
    c->sram[addr - c->sram_start] = v;
    return;
  }
  fprintf(stderr, "Data write OOB 0x%06lX\n", (unsigned long)addr);
}

uint16_t avr_get_x(const avr_t *c) {
  return (uint16_t)((c->R[27] << 8) | c->R[26]);
}
uint16_t avr_get_y(const avr_t *c) {
  return (uint16_t)((c->R[29] << 8) | c->R[28]);
}
uint16_t avr_get_z(const avr_t *c) {
  return (uint16_t)((c->R[31] << 8) | c->R[30]);
}

void avr_set_x(avr_t *c, uint16_t v) {
  c->R[26] = (uint8_t)(v & 0xFF);
  c->R[27] = (uint8_t)(v >> 8);
}
void avr_set_y(avr_t *c, uint16_t v) {
  c->R[28] = (uint8_t)(v & 0xFF);
  c->R[29] = (uint8_t)(v >> 8);
}
void avr_set_z(avr_t *c, uint16_t v) {
  c->R[30] = (uint8_t)(v & 0xFF);
  c->R[31] = (uint8_t)(v >> 8);
}

/* Stack uses post-decrement on push, pre-increment on pop. For 16-bit
 * values (CALL/RCALL return address) the high byte is pushed first, so it
 * ends up at the higher address. */
static void push8(avr_t *c, uint8_t v) { avr_write_data(c, c->sp--, v); }
static uint8_t pop8(avr_t *c) { return avr_read_data(c, ++c->sp); }

static void push16(avr_t *c, uint16_t v) {
  push8(c, (uint8_t)(v >> 8));
  push8(c, (uint8_t)(v & 0xFF));
}

static uint16_t pop16(avr_t *c) {
  uint8_t lo = pop8(c);
  uint8_t hi = pop8(c);
  return (uint16_t)((hi << 8) | lo);
}

/* -------------------------------------------------------------------------
 * Flag helpers
 * ------------------------------------------------------------------------- */

/* AND/OR/EOR/ANDI/ORI/COM: V is forced to 0, S = N ^ V. */
static void flags_logic(avr_t *c, uint8_t r) {
  set_flag(c, F_Z, r == 0);
  set_flag(c, F_N, (r & 0x80) != 0);
  CLRBIT(c->sreg, F_V);
  set_flag(c, F_S, GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V));
}

/* ADD/ADC: full SREG update. cin is the carry-in (0 for ADD). */
static void flags_add(avr_t *c, uint8_t a, uint8_t b, uint8_t res,
                      uint8_t cin) {
  uint16_t full = (uint16_t)a + (uint16_t)b + cin;
  set_flag(c, F_H, (((a & 0x0F) + (b & 0x0F) + cin) & 0x10) != 0);
  set_flag(c, F_C, (full & 0x100) != 0);
  set_flag(c, F_Z, res == 0);
  set_flag(c, F_N, (res & 0x80) != 0);
  set_flag(c, F_V, ((~(a ^ b) & (a ^ res)) & 0x80) != 0);
  set_flag(c, F_S, GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V));
}

/* SUB/SBC/CP/CPC/CPI/SUBI/SBCI: full SREG update.
 * carry_is_borrow == 1 for SBC/SBCI/CPC, where Z is left unchanged if the
 * result is zero (so chained 16/32-bit subtractions report "all zero"
 * correctly). For SUB/CP/CPI it must be 0. */
static void flags_sub(avr_t *c, uint8_t a, uint8_t b, uint8_t cin,
                      int carry_is_borrow) {
  uint16_t full = (uint16_t)a - (uint16_t)b - (uint16_t)cin;
  uint8_t res = (uint8_t)full;

  uint8_t hb = (uint8_t)((~a & b) | (b & res) | (res & ~a));
  set_flag(c, F_H, (hb & 0x08) != 0);
  set_flag(c, F_C, (full & 0x100) != 0);

  if (carry_is_borrow && res == 0) {
    /* Preserve Z for chained multi-byte subtract/compare operations. */
  } else {
    set_flag(c, F_Z, res == 0);
  }

  set_flag(c, F_N, (res & 0x80) != 0);
  set_flag(c, F_V, (((a & ~b & ~res) | (~a & b & res)) & 0x80) != 0);
  set_flag(c, F_S, GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V));
}

/* LSR/ROR/ASR: C takes the LSB of the value before the shift; V = N ^ C. */
static void flags_shift_right(avr_t *c, uint8_t orig, uint8_t res) {
  set_flag(c, F_C, (orig & 0x01) != 0);
  set_flag(c, F_Z, res == 0);
  set_flag(c, F_N, (res & 0x80) != 0);
  set_flag(c, F_V, GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_C));
  set_flag(c, F_S, GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V));
}

/* -------------------------------------------------------------------------
 * Decoder helpers
 * ------------------------------------------------------------------------- */

/* The four AVR instructions that occupy two 16-bit words. Used by CPSE,
 * SBRC, SBRS, SBIC and SBIS so they skip the correct number of words. */
static int is_2_word_instruction(uint16_t op) {
  if ((op & 0xFE0F) == 0x9000)
    return 1; /* LDS */
  if ((op & 0xFE0F) == 0x9200)
    return 1; /* STS */
  if ((op & 0xFE0E) == 0x940C)
    return 1; /* JMP */
  if ((op & 0xFE0E) == 0x940E)
    return 1; /* CALL */
  return 0;
}

static void do_skip(avr_t *c) {
  uint16_t nxt = avr_flash_word(c, c->pc);
  if (is_2_word_instruction(nxt)) {
    c->pc += 4;
    c->cycles += 2;
  } else {
    c->pc += 2;
    c->cycles += 1;
  }
}

/* Decode the 22-bit jump target of JMP/CALL:
 *   word 1 = 1001 010k kkkk 11Xk    word 2 = kkkk kkkk kkkk kkkk
 *   bit 0 of word 1   -> k16
 *   bits 8..4 of w1   -> k21..k17
 */
static uint32_t decode_jmp_call_k(uint16_t op, uint16_t op2) {
  uint32_t k = op2;
  k |= (uint32_t)(op & 0x0001) << 16;
  k |= (uint32_t)(op & 0x01F0) << 13;
  return k;
}

/* Field extractors for AVR opcode encodings.
 *   reg_d5/r5 : 5-bit register index for the "rd dddd" / "r rrrr" fields
 *   reg_d4    : 4-bit field, register R16..R31 (LDI/CPI/ANDI/ORI/SUBI/SBCI)
 *   reg_d3/r3 : 3-bit field, register R16..R23 (MULS/MULSU/FMUL*)
 *   imm_K8    : 8-bit immediate "KKKK ____ KKKK" (LDI family)
 *   imm_K6    : 6-bit immediate for ADIW/SBIW
 *   imm_q     : 6-bit displacement for LDD/STD (Y+q, Z+q)
 */
static uint8_t reg_d5(uint16_t op) { return (uint8_t)((op >> 4) & 0x1F); }
static uint8_t reg_r5(uint16_t op) {
  return (uint8_t)(((op & 0x0200) >> 5) | (op & 0x0F));
}
static uint8_t reg_d4(uint16_t op) {
  return (uint8_t)(16 + ((op >> 4) & 0x0F));
}
static uint8_t reg_d3(uint16_t op) {
  return (uint8_t)(16 + ((op >> 4) & 0x07));
}
static uint8_t reg_r3(uint16_t op) { return (uint8_t)(16 + (op & 0x07)); }
static uint8_t imm_K8(uint16_t op) {
  return (uint8_t)(((op >> 4) & 0xF0) | (op & 0x0F));
}
static uint8_t imm_K6(uint16_t op) {
  return (uint8_t)(((op >> 2) & 0x30) | (op & 0x0F));
}
static uint8_t imm_q(uint16_t op) {
  return (uint8_t)(((op >> 8) & 0x20) | ((op >> 7) & 0x18) | (op & 0x07));
}

/* Branch offset is a 7-bit signed value (-64..+63) in word units. */
static int8_t branch_offset(uint16_t op) {
  int8_t k = (int8_t)((op >> 3) & 0x7F);
  if (k & 0x40)
    k = (int8_t)(k | 0x80);
  return k;
}

/* Relative jump/call offset is 12-bit signed (-2048..+2047) in word units. */
static int16_t rjmp_offset(uint16_t op) {
  int16_t k = (int16_t)(op & 0x0FFF);
  if (k & 0x0800)
    k = (int16_t)(k | 0xF000);
  return k;
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

/* Allocate the memory buffers from the configured sizes and set SP to the top
 * of SRAM. The config fields must already be populated. */
static void avr_alloc(avr_t *c) {
  c->io_bytes = (c->sram_start > 0x20) ? c->sram_start - 0x20 : 0;
  c->flash = (uint8_t *)calloc(c->flash_bytes ? c->flash_bytes : 1, 1);
  c->sram = (uint8_t *)calloc(c->sram_bytes ? c->sram_bytes : 1, 1);
  c->eeprom = (uint8_t *)calloc(c->eeprom_bytes ? c->eeprom_bytes : 1, 1);
  c->io = (uint8_t *)calloc(c->io_bytes ? c->io_bytes : 1, 1);
  c->sp = (uint16_t)(c->sram_start + c->sram_bytes - 1);
  c->running = 1;
}

void avr_init(avr_t *c) {
  memset(c, 0, sizeof(*c));
  c->device = "generic";
  c->core = AVR_CORE_UNKNOWN; /* no gating, single cycle model */
  c->flash_bytes = AVR_FLASH_BYTES;
  c->sram_bytes = AVR_SRAM_BYTES;
  c->eeprom_bytes = AVR_EEPROM_BYTES;
  c->sram_start = AVR_SRAM_START;
  avr_alloc(c);
}

int avr_init_device(avr_t *c, const char *device) {
  for (int i = 0; i < avr_device_count; i++) {
    if (strcmp(avr_device_table[i].name, device) == 0) {
      const avr_device_t *d = &avr_device_table[i];
      memset(c, 0, sizeof(*c));
      c->device = d->name;
      c->core = d->core;
      c->flash_bytes = d->flash_bytes;
      c->sram_bytes = d->sram_bytes;
      c->eeprom_bytes = d->eeprom_bytes;
      c->sram_start = d->sram_start;
      avr_alloc(c);
      c->sp = (uint16_t)d->ram_end; /* exact RAMEND from the device header */
      /* TIMER0_COMPA vector index for devices whose modern Timer0 we model. */
      if (strcmp(d->name, "atmega328p") == 0 || strcmp(d->name, "atmega328") == 0) {
        c->timer0_compa_vec = 14;
      } else if (strcmp(d->name, "atmega1284") == 0 || strcmp(d->name, "atmega1284p") == 0) {
        c->timer0_compa_vec = 16;
      } else {
        c->timer0_compa_vec = 0; /* timer peripheral not modeled */
      }
      return 0;
    }
  }
  return 1;
}

void avr_free(avr_t *c) {
  free(c->flash);
  c->flash = NULL;
  free(c->sram);
  c->sram = NULL;
  free(c->eeprom);
  c->eeprom = NULL;
  free(c->io);
  c->io = NULL;
}

void avr_reset(avr_t *c) {
  memset(c->R, 0, sizeof(c->R));
  memset(c->io, 0, c->io_bytes);
  memset(c->sram, 0, c->sram_bytes);
  c->pc = 0;
  c->sp = (uint16_t)(c->sram_start + c->sram_bytes - 1);
  c->sreg = 0;
  c->rampx = c->rampy = c->rampz = c->rampd = c->eind = 0;
  c->cycles = 0;
  c->running = 1;
  c->unknown_opcode = 0;
}

/* -------------------------------------------------------------------------
 * LD/ST helpers for X/Y/Z indirect addressing
 *
 * mode = 0 : no change (LD Rd, X)
 * mode = 1 : post-increment (LD Rd, X+)
 * mode = 2 : pre-decrement  (LD Rd, -X)
 * ------------------------------------------------------------------------- */

static void exec_ld_st_ptr(avr_t *c, uint16_t op,
                           uint16_t (*get_ptr)(const avr_t *),
                           void (*set_ptr)(avr_t *, uint16_t), uint8_t ramp,
                           int is_store, int mode) {
  uint8_t d = reg_d5(op);
  uint16_t ptr = get_ptr(c);
  uint32_t addr;

  if (mode == 2) {
    ptr--;
    set_ptr(c, ptr);
  }

  addr = ((uint32_t)ramp << 16) | ptr;

  if (is_store)
    avr_write_data(c, addr, c->R[d]);
  else
    c->R[d] = avr_read_data(c, addr);

  if (mode == 1) {
    set_ptr(c, ptr + 1);
  }
  c->cycles += ld_st_cyc(c, is_store, mode);
}

/* LDD/STD Rd, Y+q / Z+q: 6-bit unsigned displacement, pointer unchanged. */
static void exec_ldd_std(avr_t *c, uint16_t op, int use_y, int is_store) {
  uint8_t d = reg_d5(op);
  uint8_t q = imm_q(op);
  uint16_t base = use_y ? avr_get_y(c) : avr_get_z(c);
  uint8_t ramp = use_y ? c->rampy : c->rampz;
  uint32_t addr = ((uint32_t)ramp << 16) | (uint16_t)(base + q);
  if (is_store) {
    avr_write_data(c, addr, c->R[d]);
    c->cycles += cyc(c, 2, 2, 1, 0); /* STD Y+q/Z+q (N/A on AVRrc) */
  } else {
    c->R[d] = avr_read_data(c, addr);
    c->cycles += cyc(c, 2, 3, 2, 0); /* LDD Y+q/Z+q (N/A on AVRrc) */
  }
}

/* -------------------------------------------------------------------------
 * DES -- one round of the Data Encryption Standard (XMEGA extension).
 *
 * The 64-bit data block lives in R0..R7 (LSB of the block in the LSB of R0)
 * and the 64-bit key in R8..R15. Each DES instruction performs one of the
 * 16 rounds, selected by K; the H flag chooses encryption (0) or decryption
 * (1). The round applies the initial permutation and its
 * inverse on every iteration, so consecutive rounds compose into a full DES.
 * The DES preoutput half-swap is folded into the final round (K == 15) so
 * that running rounds 0..15 yields the standard ciphertext/plaintext.
 * Permutation tables follow FIPS PUB 46; bit 1 denotes the most significant
 * bit of the operand.
 * ------------------------------------------------------------------------- */

static const uint8_t DES_IP[64] = {
    58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9,  1, 59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7};
static const uint8_t DES_FP[64] = {
    40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9,  49, 17, 57, 25};
static const uint8_t DES_E[48] = {
    32, 1,  2,  3,  4,  5,  4,  5,  6,  7,  8,  9,  8,  9,  10, 11,
    12, 13, 12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21,
    22, 23, 24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1};
static const uint8_t DES_P[32] = {16, 7, 20, 21, 29, 12, 28, 17, 1,  15, 23,
                                  26, 5, 18, 31, 10, 2,  8,  24, 14, 32, 27,
                                  3,  9, 19, 13, 30, 6,  22, 11, 4,  25};
static const uint8_t DES_PC1[56] = {
    57, 49, 41, 33, 25, 17, 9,  1,  58, 50, 42, 34, 26, 18, 10, 2,  59, 51, 43,
    35, 27, 19, 11, 3,  60, 52, 44, 36, 63, 55, 47, 39, 31, 23, 15, 7,  62, 54,
    46, 38, 30, 22, 14, 6,  61, 53, 45, 37, 29, 21, 13, 5,  28, 20, 12, 4};
static const uint8_t DES_PC2[48] = {
    14, 17, 11, 24, 1,  5,  3,  28, 15, 6,  21, 10, 23, 19, 12, 4,
    26, 8,  16, 7,  27, 20, 13, 2,  41, 52, 31, 37, 47, 55, 30, 40,
    51, 45, 33, 48, 44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32};
static const uint8_t DES_SHIFT[16] = {1, 1, 2, 2, 2, 2, 2, 2,
                                      1, 2, 2, 2, 2, 2, 2, 1};
static const uint8_t DES_SBOX[8][64] = {
    {14, 4,  13, 1, 2,  15, 11, 8,  3,  10, 6,  12, 5,  9,  0, 7,
     0,  15, 7,  4, 14, 2,  13, 1,  10, 6,  12, 11, 9,  5,  3, 8,
     4,  1,  14, 8, 13, 6,  2,  11, 15, 12, 9,  7,  3,  10, 5, 0,
     15, 12, 8,  2, 4,  9,  1,  7,  5,  11, 3,  14, 10, 0,  6, 13},
    {15, 1,  8,  14, 6,  11, 3,  4,  9,  7, 2,  13, 12, 0, 5,  10,
     3,  13, 4,  7,  15, 2,  8,  14, 12, 0, 1,  10, 6,  9, 11, 5,
     0,  14, 7,  11, 10, 4,  13, 1,  5,  8, 12, 6,  9,  3, 2,  15,
     13, 8,  10, 1,  3,  15, 4,  2,  11, 6, 7,  12, 0,  5, 14, 9},
    {10, 0,  9,  14, 6, 3,  15, 5,  1,  13, 12, 7,  11, 4,  2,  8,
     13, 7,  0,  9,  3, 4,  6,  10, 2,  8,  5,  14, 12, 11, 15, 1,
     13, 6,  4,  9,  8, 15, 3,  0,  11, 1,  2,  12, 5,  10, 14, 7,
     1,  10, 13, 0,  6, 9,  8,  7,  4,  15, 14, 3,  11, 5,  2,  12},
    {7,  13, 14, 3, 0,  6,  9,  10, 1,  2, 8, 5,  11, 12, 4,  15,
     13, 8,  11, 5, 6,  15, 0,  3,  4,  7, 2, 12, 1,  10, 14, 9,
     10, 6,  9,  0, 12, 11, 7,  13, 15, 1, 3, 14, 5,  2,  8,  4,
     3,  15, 0,  6, 10, 1,  13, 8,  9,  4, 5, 11, 12, 7,  2,  14},
    {2,  12, 4,  1,  7,  10, 11, 6,  8,  5,  3,  15, 13, 0, 14, 9,
     14, 11, 2,  12, 4,  7,  13, 1,  5,  0,  15, 10, 3,  9, 8,  6,
     4,  2,  1,  11, 10, 13, 7,  8,  15, 9,  12, 5,  6,  3, 0,  14,
     11, 8,  12, 7,  1,  14, 2,  13, 6,  15, 0,  9,  10, 4, 5,  3},
    {12, 1,  10, 15, 9, 2,  6,  8,  0,  13, 3,  4,  14, 7,  5,  11,
     10, 15, 4,  2,  7, 12, 9,  5,  6,  1,  13, 14, 0,  11, 3,  8,
     9,  14, 15, 5,  2, 8,  12, 3,  7,  0,  4,  10, 1,  13, 11, 6,
     4,  3,  2,  12, 9, 5,  15, 10, 11, 14, 1,  7,  6,  0,  8,  13},
    {4,  11, 2,  14, 15, 0, 8,  13, 3,  12, 9, 7,  5,  10, 6, 1,
     13, 0,  11, 7,  4,  9, 1,  10, 14, 3,  5, 12, 2,  15, 8, 6,
     1,  4,  11, 13, 12, 3, 7,  14, 10, 15, 6, 8,  0,  5,  9, 2,
     6,  11, 13, 8,  1,  4, 10, 7,  9,  5,  0, 15, 14, 2,  3, 12},
    {13, 2,  8,  4, 6,  15, 11, 1,  10, 9,  3,  14, 5,  0,  12, 7,
     1,  15, 13, 8, 10, 3,  7,  4,  12, 5,  6,  11, 0,  14, 9,  2,
     7,  11, 4,  1, 9,  12, 14, 2,  0,  6,  10, 13, 15, 3,  5,  8,
     2,  1,  14, 7, 4,  10, 8,  13, 15, 12, 9,  0,  3,  5,  6,  11}};

/* Permute the low nin bits of in through table t (1-based, bit 1 = MSB). */
static uint64_t des_permute(uint64_t in, const uint8_t *t, int nout, int nin) {
  uint64_t out = 0;
  for (int i = 0; i < nout; i++)
    out = (out << 1) | ((in >> (nin - t[i])) & 1u);
  return out;
}

/* DES f-function: expand R to 48 bits, mix the subkey, S-box, then permute. */
static uint32_t des_feistel(uint32_t r, uint64_t subkey) {
  uint64_t x = des_permute(r, DES_E, 48, 32) ^ subkey;
  uint32_t out = 0;
  for (int i = 0; i < 8; i++) {
    int six = (int)((x >> (42 - 6 * i)) & 0x3F);
    int row = (((six >> 5) & 1) << 1) | (six & 1);
    int col = (six >> 1) & 0x0F;
    out = (out << 4) | DES_SBOX[i][row * 16 + col];
  }
  return (uint32_t)des_permute(out, DES_P, 32, 32);
}

/* Derive the 16 round subkeys from the 64-bit key. */
static void des_subkeys(uint64_t key, uint64_t sk[16]) {
  uint64_t k = des_permute(key, DES_PC1, 56, 64);
  uint32_t cc = (uint32_t)((k >> 28) & 0x0FFFFFFF);
  uint32_t dd = (uint32_t)(k & 0x0FFFFFFF);
  for (int i = 0; i < 16; i++) {
    int s = DES_SHIFT[i];
    cc = ((cc << s) | (cc >> (28 - s))) & 0x0FFFFFFF;
    dd = ((dd << s) | (dd >> (28 - s))) & 0x0FFFFFFF;
    sk[i] = des_permute(((uint64_t)cc << 28) | dd, DES_PC2, 48, 56);
  }
}

/* Execute one DES round K (0..15); H selects encrypt (0) or decrypt (1). */
static void exec_des(avr_t *c, int k, int decrypt) {
  uint64_t block = 0, key = 0, sk[16];
  for (int i = 7; i >= 0; i--)
    block = (block << 8) | c->R[i];
  for (int i = 7; i >= 0; i--)
    key = (key << 8) | c->R[8 + i];
  des_subkeys(key, sk);

  uint64_t x = des_permute(block, DES_IP, 64, 64);
  uint32_t l = (uint32_t)(x >> 32);
  uint32_t r = (uint32_t)x;
  uint32_t nl = r;
  uint32_t nr = l ^ des_feistel(r, sk[decrypt ? 15 - k : k]);
  if (k == 15) { /* fold in the DES preoutput swap */
    uint32_t tmp = nl;
    nl = nr;
    nr = tmp;
  }
  uint64_t res = des_permute(((uint64_t)nl << 32) | nr, DES_FP, 64, 64);
  for (int i = 0; i < 8; i++)
    c->R[i] = (uint8_t)(res >> (8 * i));
}

/* -------------------------------------------------------------------------
 * Per-core instruction gating
 *
 * Not every instruction exists on every CPU version. When a concrete core is
 * selected, reject the instructions it lacks by halting on them. The generic
 * core (AVR_CORE_UNKNOWN) allows everything, so it is unaffected.
 * ------------------------------------------------------------------------- */
static int gate_opcode(avr_t *c, uint16_t op, uint32_t pc) {
  if (c->core == AVR_CORE_UNKNOWN)
    return 0;
  int xm = (c->core == AVR_CORE_XM);
  int e = (c->core == AVR_CORE_E);
  int rc = (c->core == AVR_CORE_RC);
  int bad = 0;

  if ((op & 0xFE0F) == 0x9204 || (op & 0xFE0F) == 0x9205 ||
      (op & 0xFE0F) == 0x9206 || (op & 0xFE0F) == 0x9207 ||
      (op & 0xFF0F) == 0x940B) {
    bad = !xm; /* RMW (XCH/LAS/LAC/LAT) and DES: AVRxm only */
  } else if ((op & 0xFC00) == 0x9C00 || (op & 0xFF00) == 0x0200 ||
             (op & 0xFF88) == 0x0300 || (op & 0xFF88) == 0x0308 ||
             (op & 0xFF88) == 0x0380 || (op & 0xFF88) == 0x0388) {
    bad = (e || rc); /* multiply family: AVRe+ and up */
  } else if (op == 0x9419 || op == 0x9519 || (op & 0xFE0F) == 0x9006 ||
             (op & 0xFE0F) == 0x9007 || op == 0x95D8) {
    bad = (e || rc); /* extended addressing: EIJMP/EICALL/ELPM */
  } else if ((op & 0xFF00) == 0x9600 || (op & 0xFF00) == 0x9700 ||
             (op & 0xFF00) == 0x0100 || (op & 0xFE0E) == 0x940C ||
             (op & 0xFE0E) == 0x940E || (op & 0xFE0F) == 0x9004 ||
             (op & 0xFE0F) == 0x9005 || op == 0x95C8 || op == 0x95E8 ||
             op == 0x95F8) {
    bad = rc; /* ADIW/SBIW/MOVW/JMP/CALL/LPM/SPM: not on AVRrc */
  } else if (((op & 0xD208) == 0x8000 || (op & 0xD208) == 0x8008 ||
              (op & 0xD208) == 0x8200 || (op & 0xD208) == 0x8208) &&
             imm_q(op) != 0) {
    bad = rc; /* LDD/STD with displacement: not on AVRrc */
  }

  if (!bad)
    return 0;
  fprintf(stderr, "Illegal instruction 0x%04X for %s at PC=0x%06lX\n", op,
          avr_core_name(c->core), (unsigned long)pc);
  c->unknown_opcode = 1;
  c->running = 0;
  return 1;
}

/* -------------------------------------------------------------------------
 * avr_step -- fetch, decode and execute one instruction.
 *
 * The decoder is a long if/else chain. Each entry is labeled with the
 * mnemonic and opcode bit pattern. Each branch advances PC and updates
 * the cycle count.
 * ------------------------------------------------------------------------- */
static void avr_step_internal(avr_t *c);

/* Modern Timer0 (ATmega328P/1284) registers, as io[] offsets (data addr-0x20). */
#define IO_TCCR0A 0x24
#define IO_TCCR0B 0x25
#define IO_TCNT0  0x26
#define IO_OCR0A  0x27
#define IO_TIFR0  0x15
#define IO_TIMSK0 0x4E
#define BIT_OCF0A  1 /* TIFR0 / TIMSK0 bit 1 (OCF0A / OCIE0A) */

/* Vector slot width mirrors ik8b codegen:
 * - AVRrc uses 1-word RJMP slots  (2 bytes)
 * - other cores use 2-word JMP slots (4 bytes) */
static uint32_t vector_slot_bytes(const avr_t *c) {
  return (c->core == AVR_CORE_RC) ? 2u : 4u;
}

void avr_raise_interrupt(avr_t *c, uint8_t vector_index) {
  if (vector_index == 0) return; /* RESET is not queued as a maskable interrupt */
  c->irq_pending[vector_index] = 1;
}

/* Advances Timer0 by `cycles` CPU cycles. Models the common CTC mode
 * (WGM = 010): TCNT0 counts up through the prescaler; when it reaches OCR0A it
 * wraps to 0 and raises the OCF0A compare-match flag. Other modes count freely.
 * Devices without a modeled Timer0 (timer0_compa_vec == 0) are skipped. */
static void avr_timer0_tick(avr_t *c, uint32_t cycles) {
  if (c->timer0_compa_vec == 0) return;
  uint8_t cs = c->io[IO_TCCR0B] & 0x07;
  uint32_t presc;
  switch (cs) {
    case 1: presc = 1; break;
    case 2: presc = 8; break;
    case 3: presc = 64; break;
    case 4: presc = 256; break;
    case 5: presc = 1024; break;
    default: return; /* stopped or external clock: do not advance */
  }
  int ctc = ((c->io[IO_TCCR0A] & 0x03) == 0x02); /* WGM01=1, WGM00=0 */
  uint8_t ocr = c->io[IO_OCR0A];
  c->timer0_acc += cycles;
  while (c->timer0_acc >= presc) {
    c->timer0_acc -= presc;
    uint8_t t = c->io[IO_TCNT0];
    if (ctc && t == ocr) {
      c->io[IO_TCNT0] = 0;
      SETBIT(c->io[IO_TIFR0], BIT_OCF0A);
    } else {
      c->io[IO_TCNT0] = (uint8_t)(t + 1);
      if (!ctc && c->io[IO_TCNT0] == ocr) {
        SETBIT(c->io[IO_TIFR0], BIT_OCF0A);
      }
    }
  }
}

/* Delivers a pending Timer0 compare-match interrupt when enabled. Mirrors AVRe
 * hardware: requires global I (SREG bit 7), OCIE0A and OCF0A set; on entry it
 * clears I and OCF0A, pushes the return address, and vectors to TIMER0_COMPA.
 * One interrupt is serviced per call (between instructions). */
static void avr_service_interrupts(avr_t *c) {
  /* Peripheral source model: queue TIMER0_COMPA when its mask+flag are high. */
  if (c->timer0_compa_vec != 0 &&
      GETBIT(c->io[IO_TIMSK0], BIT_OCF0A) &&
      GETBIT(c->io[IO_TIFR0], BIT_OCF0A)) {
    avr_raise_interrupt(c, c->timer0_compa_vec);
  }

  if (!GETBIT(c->sreg, 7)) return; /* global interrupts disabled */

  /* AVR priority: lowest vector index wins (RESET excluded). */
  uint16_t vec = 0;
  for (uint16_t i = 1; i < 256; i++) {
    if (c->irq_pending[i]) {
      vec = i;
      break;
    }
  }
  if (vec == 0) return;

  c->irq_pending[vec] = 0;
  CLRBIT(c->sreg, 7); /* hardware clears I on interrupt entry */
  if (c->timer0_compa_vec != 0 && vec == c->timer0_compa_vec) {
    CLRBIT(c->io[IO_TIFR0], BIT_OCF0A); /* clear served Timer0 compare flag */
  }
  push16(c, (uint16_t)c->pc); /* return address (byte PC) */
  c->pc = (uint32_t)vec * vector_slot_bytes(c);
  c->cycles += 5;
  TRACE(c, "IRQ vector %u -> PC=0x%06lX\n", (unsigned)vec, (unsigned long)c->pc);
}

void avr_step(avr_t *c) {
  uint64_t start_cycles = c->cycles;
  avr_step_internal(c);
  uint64_t consumed = c->cycles - start_cycles;
  
  const eeprom_hw_t* ee = get_ee_hw(c->device);
  uint32_t ctrl_reg = ee ? ee->ctrl : 0x1F;

  if (c->eempe_timer > 0) {
    if (consumed >= c->eempe_timer) {
      c->eempe_timer = 0;
      if (!ee || ee->is_modern == 0) CLRBIT(c->io[ctrl_reg], 2); // EEMPE
    } else {
      c->eempe_timer -= consumed;
    }
  }

  if (c->eeprom_write_cycles_left > 0) {
    if (consumed >= c->eeprom_write_cycles_left) {
      c->eeprom_write_cycles_left = 0;
      if (c->eeprom_write_addr < c->eeprom_bytes) {
        c->eeprom[c->eeprom_write_addr] = c->eeprom_write_val;
      }
      if (!ee || ee->is_modern == 0) {
        CLRBIT(c->io[ctrl_reg], 1); // Clear EEPE (write complete)
      } else {
        c->io[ee->status] &= ~0x01; // Clear EEBUSY
        c->io[ee->ctrl] &= ~0x07;   // Clear CMD
      }
    } else {
      c->eeprom_write_cycles_left -= consumed;
    }
  }

  avr_timer0_tick(c, (uint32_t)consumed);
  avr_service_interrupts(c);
}

static void avr_step_internal(avr_t *c) {
  uint32_t cur_pc = c->pc;
  uint16_t op = avr_flash_word(c, cur_pc);
  c->pc += 2;
  if (gate_opcode(c, op, cur_pc))
    return;

  /* NOP -- 0000 0000 0000 0000 */
  if (op == 0x0000) {
    c->cycles += 1;
    TRACE(c, "PC=%06lX  0000  NOP\n", (unsigned long)cur_pc);
    return;
  }

  /* MOVW Rd+1:Rd, Rr+1:Rr -- 0000 0001 dddd rrrr  (register-pair copy) */
  if ((op & 0xFF00) == 0x0100) {
    uint8_t d = (uint8_t)((op >> 3) & 0x1E);
    uint8_t r = (uint8_t)((op & 0x0F) << 1);
    c->R[d] = c->R[r];
    c->R[d + 1] = c->R[r + 1];
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  MOVW R%u:R%u <- R%u:R%u\n", (unsigned long)cur_pc,
          op, d + 1, d, r + 1, r);
    return;
  }

  /* MULS Rd, Rr -- 0000 0010 dddd rrrr  (signed * signed -> R1:R0) */
  if ((op & 0xFF00) == 0x0200) {
    uint8_t d = 16 + ((op >> 4) & 0x0F);
    uint8_t r = 16 + (op & 0x0F);
    int16_t res = (int16_t)((int8_t)c->R[d] * (int8_t)c->R[r]);
    c->R[0] = (uint8_t)(res & 0xFF);
    c->R[1] = (uint8_t)((res >> 8) & 0xFF);
    set_flag(c, F_C, (res & 0x8000) != 0);
    set_flag(c, F_Z, (uint16_t)res == 0);
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  MULS R%u,R%u -> R1:R0=0x%04X\n",
          (unsigned long)cur_pc, op, d, r, (uint16_t)res);
    return;
  }

  /* MULSU Rd, Rr -- 0000 0011 0ddd 0rrr  (signed Rd * unsigned Rr) */
  if ((op & 0xFF88) == 0x0300) {
    uint8_t d = reg_d3(op);
    uint8_t r = reg_r3(op);
    int16_t res = (int16_t)((int8_t)c->R[d] * (uint8_t)c->R[r]);
    c->R[0] = (uint8_t)(res & 0xFF);
    c->R[1] = (uint8_t)((res >> 8) & 0xFF);
    set_flag(c, F_C, (res & 0x8000) != 0);
    set_flag(c, F_Z, (uint16_t)res == 0);
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  MULSU R%u,R%u\n", (unsigned long)cur_pc, op, d,
          r);
    return;
  }

  /* FMUL Rd, Rr -- 0000 0011 0ddd 1rrr  (unsigned fractional, result << 1) */
  if ((op & 0xFF88) == 0x0308) {
    uint8_t d = reg_d3(op);
    uint8_t r = reg_r3(op);
    uint16_t res = (uint16_t)((uint16_t)c->R[d] * (uint16_t)c->R[r]);
    uint16_t shifted = (uint16_t)(res << 1);
    c->R[0] = (uint8_t)(shifted & 0xFF);
    c->R[1] = (uint8_t)((shifted >> 8) & 0xFF);
    set_flag(c, F_C, (res & 0x8000) != 0);
    set_flag(c, F_Z, shifted == 0);
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  FMUL R%u,R%u\n", (unsigned long)cur_pc, op, d, r);
    return;
  }

  /* FMULS Rd, Rr -- 0000 0011 1ddd 0rrr  (signed fractional, result << 1) */
  if ((op & 0xFF88) == 0x0380) {
    uint8_t d = reg_d3(op);
    uint8_t r = reg_r3(op);
    int16_t res = (int16_t)((int8_t)c->R[d] * (int8_t)c->R[r]);
    uint16_t shifted = (uint16_t)((uint16_t)res << 1);
    c->R[0] = (uint8_t)(shifted & 0xFF);
    c->R[1] = (uint8_t)((shifted >> 8) & 0xFF);
    set_flag(c, F_C, (res & 0x8000) != 0);
    set_flag(c, F_Z, shifted == 0);
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  FMULS R%u,R%u\n", (unsigned long)cur_pc, op, d,
          r);
    return;
  }

  /* FMULSU Rd, Rr -- 0000 0011 1ddd 1rrr  (signed * unsigned, result << 1) */
  if ((op & 0xFF88) == 0x0388) {
    uint8_t d = reg_d3(op);
    uint8_t r = reg_r3(op);
    int16_t res = (int16_t)((int8_t)c->R[d] * (uint8_t)c->R[r]);
    uint16_t shifted = (uint16_t)((uint16_t)res << 1);
    c->R[0] = (uint8_t)(shifted & 0xFF);
    c->R[1] = (uint8_t)((shifted >> 8) & 0xFF);
    set_flag(c, F_C, (res & 0x8000) != 0);
    set_flag(c, F_Z, shifted == 0);
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  FMULSU R%u,R%u\n", (unsigned long)cur_pc, op, d,
          r);
    return;
  }

  /* CPC Rd, Rr -- 0000 01rd dddd rrrr  (compare with carry) */
  if ((op & 0xFC00) == 0x0400) {
    uint8_t d = reg_d5(op), r = reg_r5(op);
    uint8_t cin = (uint8_t)GETBIT(c->sreg, F_C);
    flags_sub(c, c->R[d], c->R[r], cin, 1);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  CPC R%u,R%u\n", (unsigned long)cur_pc, op, d, r);
    return;
  }

  /* SBC Rd, Rr -- 0000 10rd dddd rrrr  (subtract with carry) */
  if ((op & 0xFC00) == 0x0800) {
    uint8_t d = reg_d5(op), r = reg_r5(op);
    uint8_t a = c->R[d], b = c->R[r];
    uint8_t cin = (uint8_t)GETBIT(c->sreg, F_C);
    uint8_t res = (uint8_t)(a - b - cin);
    c->R[d] = res;
    flags_sub(c, a, b, cin, 1);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  SBC R%u,R%u\n", (unsigned long)cur_pc, op, d, r);
    return;
  }

  /* ADD Rd, Rr -- 0000 11rd dddd rrrr  (also encodes LSL Rd as ADD Rd,Rd) */
  if ((op & 0xFC00) == 0x0C00) {
    uint8_t d = reg_d5(op), r = reg_r5(op);
    uint8_t a = c->R[d], b = c->R[r];
    uint8_t res = (uint8_t)(a + b);
    c->R[d] = res;
    flags_add(c, a, b, res, 0);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  ADD R%u,R%u -> 0x%02X\n", (unsigned long)cur_pc,
          op, d, r, res);
    return;
  }

  /* CPSE Rd, Rr -- 0001 00rd dddd rrrr  (skip next if equal) */
  if ((op & 0xFC00) == 0x1000) {
    uint8_t d = reg_d5(op), r = reg_r5(op);
    int skip = (c->R[d] == c->R[r]);
    c->cycles += 1;
    if (skip)
      do_skip(c);
    TRACE(c, "PC=%06lX  %04X  CPSE R%u,R%u %s\n", (unsigned long)cur_pc, op, d,
          r, skip ? "(skip)" : "");
    return;
  }

  /* CP Rd, Rr -- 0001 01rd dddd rrrr  (compare, flags only) */
  if ((op & 0xFC00) == 0x1400) {
    uint8_t d = reg_d5(op), r = reg_r5(op);
    flags_sub(c, c->R[d], c->R[r], 0, 0);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  CP R%u,R%u\n", (unsigned long)cur_pc, op, d, r);
    return;
  }

  /* SUB Rd, Rr -- 0001 10rd dddd rrrr */
  if ((op & 0xFC00) == 0x1800) {
    uint8_t d = reg_d5(op), r = reg_r5(op);
    uint8_t a = c->R[d], b = c->R[r];
    uint8_t res = (uint8_t)(a - b);
    c->R[d] = res;
    flags_sub(c, a, b, 0, 0);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  SUB R%u,R%u -> 0x%02X\n", (unsigned long)cur_pc,
          op, d, r, res);
    return;
  }

  /* ADC Rd, Rr -- 0001 11rd dddd rrrr  (also encodes ROL Rd as ADC Rd,Rd) */
  if ((op & 0xFC00) == 0x1C00) {
    uint8_t d = reg_d5(op), r = reg_r5(op);
    uint8_t a = c->R[d], b = c->R[r];
    uint8_t cin = (uint8_t)GETBIT(c->sreg, F_C);
    uint8_t res = (uint8_t)(a + b + cin);
    c->R[d] = res;
    flags_add(c, a, b, res, cin);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  ADC R%u,R%u -> 0x%02X\n", (unsigned long)cur_pc,
          op, d, r, res);
    return;
  }

  /* AND Rd, Rr -- 0010 00rd dddd rrrr  (TST Rd is AND Rd,Rd) */
  if ((op & 0xFC00) == 0x2000) {
    uint8_t d = reg_d5(op), r = reg_r5(op);
    uint8_t res = (uint8_t)(c->R[d] & c->R[r]);
    c->R[d] = res;
    flags_logic(c, res);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  AND R%u,R%u\n", (unsigned long)cur_pc, op, d, r);
    return;
  }

  /* EOR Rd, Rr -- 0010 01rd dddd rrrr  (CLR Rd is EOR Rd,Rd) */
  if ((op & 0xFC00) == 0x2400) {
    uint8_t d = reg_d5(op), r = reg_r5(op);
    uint8_t res = (uint8_t)(c->R[d] ^ c->R[r]);
    c->R[d] = res;
    flags_logic(c, res);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  EOR R%u,R%u\n", (unsigned long)cur_pc, op, d, r);
    return;
  }

  /* OR Rd, Rr -- 0010 10rd dddd rrrr */
  if ((op & 0xFC00) == 0x2800) {
    uint8_t d = reg_d5(op), r = reg_r5(op);
    uint8_t res = (uint8_t)(c->R[d] | c->R[r]);
    c->R[d] = res;
    flags_logic(c, res);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  OR R%u,R%u\n", (unsigned long)cur_pc, op, d, r);
    return;
  }

  /* MOV Rd, Rr -- 0010 11rd dddd rrrr */
  if ((op & 0xFC00) == 0x2C00) {
    uint8_t d = reg_d5(op), r = reg_r5(op);
    c->R[d] = c->R[r];
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  MOV R%u,R%u\n", (unsigned long)cur_pc, op, d, r);
    return;
  }

  /* CPI Rd, K -- 0011 KKKK dddd KKKK  (R16..R31) */
  if ((op & 0xF000) == 0x3000) {
    uint8_t d = reg_d4(op);
    uint8_t K = imm_K8(op);
    flags_sub(c, c->R[d], K, 0, 0);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  CPI R%u,0x%02X\n", (unsigned long)cur_pc, op, d,
          K);
    return;
  }

  /* SBCI Rd, K -- 0100 KKKK dddd KKKK  (subtract immediate with carry) */
  if ((op & 0xF000) == 0x4000) {
    uint8_t d = reg_d4(op);
    uint8_t K = imm_K8(op);
    uint8_t cin = (uint8_t)GETBIT(c->sreg, F_C);
    uint8_t a = c->R[d];
    uint8_t res = (uint8_t)(a - K - cin);
    c->R[d] = res;
    flags_sub(c, a, K, cin, 1);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  SBCI R%u,0x%02X\n", (unsigned long)cur_pc, op, d,
          K);
    return;
  }

  /* SUBI Rd, K -- 0101 KKKK dddd KKKK */
  if ((op & 0xF000) == 0x5000) {
    uint8_t d = reg_d4(op);
    uint8_t K = imm_K8(op);
    uint8_t a = c->R[d];
    uint8_t res = (uint8_t)(a - K);
    c->R[d] = res;
    flags_sub(c, a, K, 0, 0);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  SUBI R%u,0x%02X\n", (unsigned long)cur_pc, op, d,
          K);
    return;
  }

  /* ORI Rd, K -- 0110 KKKK dddd KKKK  (alias SBR Rd, K) */
  if ((op & 0xF000) == 0x6000) {
    uint8_t d = reg_d4(op);
    uint8_t K = imm_K8(op);
    uint8_t res = (uint8_t)(c->R[d] | K);
    c->R[d] = res;
    flags_logic(c, res);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  ORI R%u,0x%02X\n", (unsigned long)cur_pc, op, d,
          K);
    return;
  }

  /* ANDI Rd, K -- 0111 KKKK dddd KKKK  (alias CBR Rd, ~K) */
  if ((op & 0xF000) == 0x7000) {
    uint8_t d = reg_d4(op);
    uint8_t K = imm_K8(op);
    uint8_t res = (uint8_t)(c->R[d] & K);
    c->R[d] = res;
    flags_logic(c, res);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  ANDI R%u,0x%02X\n", (unsigned long)cur_pc, op, d,
          K);
    return;
  }

  /* LDD/STD with 6-bit displacement -- 10q0 qq.d dddd .qqq
   *   bit 9  = 0/1 selects LD/ST
   *   bit 3  = 0/1 selects Z/Y as base
   */
  if ((op & 0xD208) == 0x8000) {
    exec_ldd_std(c, op, 0, 0);
    TRACE(c, "PC=%06lX  %04X  LDD R%u, Z+%u\n", (unsigned long)cur_pc, op,
          reg_d5(op), imm_q(op));
    return;
  }
  if ((op & 0xD208) == 0x8008) {
    exec_ldd_std(c, op, 1, 0);
    TRACE(c, "PC=%06lX  %04X  LDD R%u, Y+%u\n", (unsigned long)cur_pc, op,
          reg_d5(op), imm_q(op));
    return;
  }
  if ((op & 0xD208) == 0x8200) {
    exec_ldd_std(c, op, 0, 1);
    TRACE(c, "PC=%06lX  %04X  STD Z+%u, R%u\n", (unsigned long)cur_pc, op,
          imm_q(op), reg_d5(op));
    return;
  }
  if ((op & 0xD208) == 0x8208) {
    exec_ldd_std(c, op, 1, 1);
    TRACE(c, "PC=%06lX  %04X  STD Y+%u, R%u\n", (unsigned long)cur_pc, op,
          imm_q(op), reg_d5(op));
    return;
  }

  /* LDS Rd, k -- 1001 000d dddd 0000 + 16-bit address word */
  if ((op & 0xFE0F) == 0x9000) {
    uint8_t d = reg_d5(op);
    uint16_t ea = avr_flash_word(c, c->pc);
    c->pc += 2;
    c->R[d] = avr_read_data(c, ea);
    c->cycles += cyc(c, 2, 3, 3, 2);
    TRACE(c, "PC=%06lX  %04X %04X  LDS R%u,[0x%04X]\n", (unsigned long)cur_pc,
          op, ea, d, ea);
    return;
  }
  /* LD Rd, Z+ -- 1001 000d dddd 0001 */
  if ((op & 0xFE0F) == 0x9001) {
    uint8_t d = reg_d5(op);
    uint16_t z = avr_get_z(c);
    c->R[d] = avr_read_data(c, ((uint32_t)c->rampz << 16) | z);
    avr_set_z(c, (uint16_t)(z + 1));
    c->cycles += ld_st_cyc(c, 0, 1);
    TRACE(c, "PC=%06lX  %04X  LD R%u,Z+\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* LD Rd, -Z -- 1001 000d dddd 0010 */
  if ((op & 0xFE0F) == 0x9002) {
    uint8_t d = reg_d5(op);
    uint16_t z = (uint16_t)(avr_get_z(c) - 1);
    avr_set_z(c, z);
    c->R[d] = avr_read_data(c, ((uint32_t)c->rampz << 16) | z);
    c->cycles += ld_st_cyc(c, 0, 2);
    TRACE(c, "PC=%06lX  %04X  LD R%u,-Z\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* LPM Rd, Z -- 1001 000d dddd 0100  (read flash byte at Z) */
  if ((op & 0xFE0F) == 0x9004) {
    uint8_t d = reg_d5(op);
    uint16_t z = avr_get_z(c);
    c->R[d] = c->flash[z];
    c->cycles += 3;
    TRACE(c, "PC=%06lX  %04X  LPM R%u,Z\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* LPM Rd, Z+ -- 1001 000d dddd 0101 */
  if ((op & 0xFE0F) == 0x9005) {
    uint8_t d = reg_d5(op);
    uint16_t z = avr_get_z(c);
    c->R[d] = c->flash[z];
    avr_set_z(c, (uint16_t)(z + 1));
    c->cycles += 3;
    TRACE(c, "PC=%06lX  %04X  LPM R%u,Z+\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* ELPM Rd, Z -- 1001 000d dddd 0110  (uses RAMPZ:Z) */
  if ((op & 0xFE0F) == 0x9006) {
    uint8_t d = reg_d5(op);
    uint32_t z = ((uint32_t)c->rampz << 16) | avr_get_z(c);
    c->R[d] = c->flash[z % c->flash_bytes];
    c->cycles += 3;
    TRACE(c, "PC=%06lX  %04X  ELPM R%u,Z\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* ELPM Rd, Z+ -- 1001 000d dddd 0111  (RAMPZ:Z incremented) */
  if ((op & 0xFE0F) == 0x9007) {
    uint8_t d = reg_d5(op);
    uint32_t z = ((uint32_t)c->rampz << 16) | avr_get_z(c);
    c->R[d] = c->flash[z % c->flash_bytes];
    z = (z + 1) & 0xFFFFFF;
    c->rampz = (uint8_t)((z >> 16) & 0xFF);
    avr_set_z(c, (uint16_t)(z & 0xFFFF));
    c->cycles += 3;
    TRACE(c, "PC=%06lX  %04X  ELPM R%u,Z+\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* LD Rd, Y+ -- 1001 000d dddd 1001 */
  if ((op & 0xFE0F) == 0x9009) {
    exec_ld_st_ptr(c, op, avr_get_y, avr_set_y, c->rampy, 0, 1);
    TRACE(c, "PC=%06lX  %04X  LD R%u,Y+\n", (unsigned long)cur_pc, op,
          reg_d5(op));
    return;
  }
  /* LD Rd, -Y -- 1001 000d dddd 1010 */
  if ((op & 0xFE0F) == 0x900A) {
    exec_ld_st_ptr(c, op, avr_get_y, avr_set_y, c->rampy, 0, 2);
    TRACE(c, "PC=%06lX  %04X  LD R%u,-Y\n", (unsigned long)cur_pc, op,
          reg_d5(op));
    return;
  }
  /* LD Rd, X -- 1001 000d dddd 1100 */
  if ((op & 0xFE0F) == 0x900C) {
    exec_ld_st_ptr(c, op, avr_get_x, avr_set_x, c->rampx, 0, 0);
    TRACE(c, "PC=%06lX  %04X  LD R%u,X\n", (unsigned long)cur_pc, op,
          reg_d5(op));
    return;
  }
  /* LD Rd, X+ -- 1001 000d dddd 1101 */
  if ((op & 0xFE0F) == 0x900D) {
    exec_ld_st_ptr(c, op, avr_get_x, avr_set_x, c->rampx, 0, 1);
    TRACE(c, "PC=%06lX  %04X  LD R%u,X+\n", (unsigned long)cur_pc, op,
          reg_d5(op));
    return;
  }
  /* LD Rd, -X -- 1001 000d dddd 1110 */
  if ((op & 0xFE0F) == 0x900E) {
    exec_ld_st_ptr(c, op, avr_get_x, avr_set_x, c->rampx, 0, 2);
    TRACE(c, "PC=%06lX  %04X  LD R%u,-X\n", (unsigned long)cur_pc, op,
          reg_d5(op));
    return;
  }
  /* POP Rd -- 1001 000d dddd 1111 */
  if ((op & 0xFE0F) == 0x900F) {
    uint8_t d = reg_d5(op);
    c->R[d] = pop8(c);
    c->cycles += cyc(c, 2, 2, 2, 3);
    TRACE(c, "PC=%06lX  %04X  POP R%u\n", (unsigned long)cur_pc, op, d);
    return;
  }

  /* STS k, Rr -- 1001 001r rrrr 0000 + 16-bit address word */
  if ((op & 0xFE0F) == 0x9200) {
    uint8_t r = reg_d5(op);
    uint16_t ea = avr_flash_word(c, c->pc);
    c->pc += 2;
    avr_write_data(c, ea, c->R[r]);
    c->cycles += cyc(c, 2, 2, 2, 1);
    TRACE(c, "PC=%06lX  %04X %04X  STS [0x%04X],R%u\n", (unsigned long)cur_pc,
          op, ea, ea, r);
    return;
  }
  /* ST Z+, Rr -- 1001 001r rrrr 0001 */
  if ((op & 0xFE0F) == 0x9201) {
    exec_ld_st_ptr(c, op, avr_get_z, avr_set_z, c->rampz, 1, 1);
    TRACE(c, "PC=%06lX  %04X  ST Z+,R%u\n", (unsigned long)cur_pc, op,
          reg_d5(op));
    return;
  }
  /* ST -Z, Rr -- 1001 001r rrrr 0010 */
  if ((op & 0xFE0F) == 0x9202) {
    exec_ld_st_ptr(c, op, avr_get_z, avr_set_z, c->rampz, 1, 2);
    TRACE(c, "PC=%06lX  %04X  ST -Z,R%u\n", (unsigned long)cur_pc, op,
          reg_d5(op));
    return;
  }
  /* XCH Z, Rd -- 1001 001d dddd 0100  (atomic exchange with (Z)) */
  if ((op & 0xFE0F) == 0x9204) {
    uint8_t d = reg_d5(op);
    uint32_t addr = ((uint32_t)c->rampz << 16) | avr_get_z(c);
    uint8_t mem = avr_read_data(c, addr);
    uint8_t rd = c->R[d];
    avr_write_data(c, addr, rd);
    c->R[d] = mem;
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  XCH Z,R%u\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* LAS Z, Rd -- 1001 001d dddd 0101  ((Z) |= Rd, then Rd = old (Z)) */
  if ((op & 0xFE0F) == 0x9205) {
    uint8_t d = reg_d5(op);
    uint32_t addr = ((uint32_t)c->rampz << 16) | avr_get_z(c);
    uint8_t mem = avr_read_data(c, addr);
    uint8_t rd = c->R[d];
    avr_write_data(c, addr, (uint8_t)(mem | rd));
    c->R[d] = mem;
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  LAS Z,R%u\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* LAC Z, Rd -- 1001 001d dddd 0110  ((Z) &= ~Rd, then Rd = old (Z)) */
  if ((op & 0xFE0F) == 0x9206) {
    uint8_t d = reg_d5(op);
    uint32_t addr = ((uint32_t)c->rampz << 16) | avr_get_z(c);
    uint8_t mem = avr_read_data(c, addr);
    uint8_t rd = c->R[d];
    avr_write_data(c, addr, (uint8_t)((uint8_t)(0xFF - rd) & mem));
    c->R[d] = mem;
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  LAC Z,R%u\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* LAT Z, Rd -- 1001 001d dddd 0111  ((Z) ^= Rd, then Rd = old (Z)) */
  if ((op & 0xFE0F) == 0x9207) {
    uint8_t d = reg_d5(op);
    uint32_t addr = ((uint32_t)c->rampz << 16) | avr_get_z(c);
    uint8_t mem = avr_read_data(c, addr);
    uint8_t rd = c->R[d];
    avr_write_data(c, addr, (uint8_t)(mem ^ rd));
    c->R[d] = mem;
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  LAT Z,R%u\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* ST Y+, Rr -- 1001 001r rrrr 1001 */
  if ((op & 0xFE0F) == 0x9209) {
    exec_ld_st_ptr(c, op, avr_get_y, avr_set_y, c->rampy, 1, 1);
    TRACE(c, "PC=%06lX  %04X  ST Y+,R%u\n", (unsigned long)cur_pc, op,
          reg_d5(op));
    return;
  }
  /* ST -Y, Rr -- 1001 001r rrrr 1010 */
  if ((op & 0xFE0F) == 0x920A) {
    exec_ld_st_ptr(c, op, avr_get_y, avr_set_y, c->rampy, 1, 2);
    TRACE(c, "PC=%06lX  %04X  ST -Y,R%u\n", (unsigned long)cur_pc, op,
          reg_d5(op));
    return;
  }
  /* ST X, Rr -- 1001 001r rrrr 1100 */
  if ((op & 0xFE0F) == 0x920C) {
    exec_ld_st_ptr(c, op, avr_get_x, avr_set_x, c->rampx, 1, 0);
    TRACE(c, "PC=%06lX  %04X  ST X,R%u\n", (unsigned long)cur_pc, op,
          reg_d5(op));
    return;
  }
  /* ST X+, Rr -- 1001 001r rrrr 1101 */
  if ((op & 0xFE0F) == 0x920D) {
    exec_ld_st_ptr(c, op, avr_get_x, avr_set_x, c->rampx, 1, 1);
    TRACE(c, "PC=%06lX  %04X  ST X+,R%u\n", (unsigned long)cur_pc, op,
          reg_d5(op));
    return;
  }
  /* ST -X, Rr -- 1001 001r rrrr 1110 */
  if ((op & 0xFE0F) == 0x920E) {
    exec_ld_st_ptr(c, op, avr_get_x, avr_set_x, c->rampx, 1, 2);
    TRACE(c, "PC=%06lX  %04X  ST -X,R%u\n", (unsigned long)cur_pc, op,
          reg_d5(op));
    return;
  }
  /* PUSH Rr -- 1001 001r rrrr 1111 */
  if ((op & 0xFE0F) == 0x920F) {
    uint8_t r = reg_d5(op);
    push8(c, c->R[r]);
    c->cycles += cyc(c, 2, 1, 1, 1);
    TRACE(c, "PC=%06lX  %04X  PUSH R%u\n", (unsigned long)cur_pc, op, r);
    return;
  }

  /* COM Rd -- 1001 010d dddd 0000  (one's complement; sets C=1) */
  if ((op & 0xFE0F) == 0x9400) {
    uint8_t d = reg_d5(op);
    uint8_t res = (uint8_t)~c->R[d];
    c->R[d] = res;
    SETBIT(c->sreg, F_C);
    flags_logic(c, res);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  COM R%u\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* NEG Rd -- 1001 010d dddd 0001  (two's complement; H = R3|Rd3) */
  if ((op & 0xFE0F) == 0x9401) {
    uint8_t d = reg_d5(op);
    uint8_t a = c->R[d];
    uint8_t res = (uint8_t)(0 - a);
    c->R[d] = res;
    set_flag(c, F_V, res == 0x80);
    set_flag(c, F_C, res != 0x00);
    set_flag(c, F_H, ((res | a) & 0x08) != 0);
    set_flag(c, F_Z, res == 0);
    set_flag(c, F_N, (res & 0x80) != 0);
    set_flag(c, F_S, GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V));
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  NEG R%u\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* SWAP Rd -- 1001 010d dddd 0010  (swap nibbles) */
  if ((op & 0xFE0F) == 0x9402) {
    uint8_t d = reg_d5(op);
    uint8_t v = c->R[d];
    c->R[d] = (uint8_t)((v << 4) | (v >> 4));
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  SWAP R%u\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* INC Rd -- 1001 010d dddd 0011 */
  if ((op & 0xFE0F) == 0x9403) {
    uint8_t d = reg_d5(op);
    uint8_t res = (uint8_t)(c->R[d] + 1);
    c->R[d] = res;
    set_flag(c, F_Z, res == 0);
    set_flag(c, F_N, (res & 0x80) != 0);
    set_flag(c, F_V, res == 0x80);
    set_flag(c, F_S, GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V));
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  INC R%u -> 0x%02X\n", (unsigned long)cur_pc, op,
          d, res);
    return;
  }
  /* ASR Rd -- 1001 010d dddd 0101  (arithmetic shift right, MSB preserved) */
  if ((op & 0xFE0F) == 0x9405) {
    uint8_t d = reg_d5(op);
    uint8_t orig = c->R[d];
    uint8_t res = (uint8_t)((orig >> 1) | (orig & 0x80));
    c->R[d] = res;
    flags_shift_right(c, orig, res);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  ASR R%u\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* LSR Rd -- 1001 010d dddd 0110  (logical shift right, MSB=0) */
  if ((op & 0xFE0F) == 0x9406) {
    uint8_t d = reg_d5(op);
    uint8_t orig = c->R[d];
    uint8_t res = (uint8_t)(orig >> 1);
    c->R[d] = res;
    flags_shift_right(c, orig, res);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  LSR R%u\n", (unsigned long)cur_pc, op, d);
    return;
  }
  /* ROR Rd -- 1001 010d dddd 0111  (rotate right through C) */
  if ((op & 0xFE0F) == 0x9407) {
    uint8_t d = reg_d5(op);
    uint8_t orig = c->R[d];
    uint8_t cin = (uint8_t)GETBIT(c->sreg, F_C);
    uint8_t res = (uint8_t)((orig >> 1) | (cin << 7));
    c->R[d] = res;
    flags_shift_right(c, orig, res);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  ROR R%u\n", (unsigned long)cur_pc, op, d);
    return;
  }

  /* SREG bit-set / bit-clear family
   *   BSET s -- 1001 0100 0sss 1000  (SEC, SEZ, ..., SEI)
   *   BCLR s -- 1001 0100 1sss 1000  (CLC, CLZ, ..., CLI)
   */
  if (op == 0x9408) {
    SETBIT(c->sreg, F_C);
    c->cycles += 1;
    TRACE(c, "SEC\n");
    return;
  }
  if (op == 0x9418) {
    SETBIT(c->sreg, F_Z);
    c->cycles += 1;
    TRACE(c, "SEZ\n");
    return;
  }
  if (op == 0x9428) {
    SETBIT(c->sreg, F_N);
    c->cycles += 1;
    TRACE(c, "SEN\n");
    return;
  }
  if (op == 0x9438) {
    SETBIT(c->sreg, F_V);
    c->cycles += 1;
    TRACE(c, "SEV\n");
    return;
  }
  if (op == 0x9448) {
    SETBIT(c->sreg, F_S);
    c->cycles += 1;
    TRACE(c, "SES\n");
    return;
  }
  if (op == 0x9458) {
    SETBIT(c->sreg, F_H);
    c->cycles += 1;
    TRACE(c, "SEH\n");
    return;
  }
  if (op == 0x9468) {
    SETBIT(c->sreg, F_T);
    c->cycles += 1;
    TRACE(c, "SET\n");
    return;
  }
  if (op == 0x9478) {
    SETBIT(c->sreg, F_I);
    c->cycles += 1;
    TRACE(c, "SEI\n");
    return;
  }
  if (op == 0x9488) {
    CLRBIT(c->sreg, F_C);
    c->cycles += 1;
    TRACE(c, "CLC\n");
    return;
  }
  if (op == 0x9498) {
    CLRBIT(c->sreg, F_Z);
    c->cycles += 1;
    TRACE(c, "CLZ\n");
    return;
  }
  if (op == 0x94A8) {
    CLRBIT(c->sreg, F_N);
    c->cycles += 1;
    TRACE(c, "CLN\n");
    return;
  }
  if (op == 0x94B8) {
    CLRBIT(c->sreg, F_V);
    c->cycles += 1;
    TRACE(c, "CLV\n");
    return;
  }
  if (op == 0x94C8) {
    CLRBIT(c->sreg, F_S);
    c->cycles += 1;
    TRACE(c, "CLS\n");
    return;
  }
  if (op == 0x94D8) {
    CLRBIT(c->sreg, F_H);
    c->cycles += 1;
    TRACE(c, "CLH\n");
    return;
  }
  if (op == 0x94E8) {
    CLRBIT(c->sreg, F_T);
    c->cycles += 1;
    TRACE(c, "CLT\n");
    return;
  }
  if (op == 0x94F8) {
    CLRBIT(c->sreg, F_I);
    c->cycles += 1;
    TRACE(c, "CLI\n");
    return;
  }

  /* IJMP -- 1001 0100 0000 1001  (PC <- Z, word address) */
  if (op == 0x9409) {
    uint16_t z = avr_get_z(c);
    c->pc = (uint32_t)z << 1;
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  IJMP -> 0x%06lX\n", (unsigned long)cur_pc, op,
          (unsigned long)c->pc);
    return;
  }
  /* EIJMP -- 1001 0100 0001 1001  (PC <- EIND:Z) */
  if (op == 0x9419) {
    uint32_t z = ((uint32_t)c->eind << 16) | avr_get_z(c);
    c->pc = z << 1;
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  EIJMP\n", (unsigned long)cur_pc, op);
    return;
  }
  /* RET -- 1001 0101 0000 1000  (pop return address) */
  if (op == 0x9508) {
    c->pc = pop16(c);
    c->cycles += cyc(c, 4, 4, 4, 6);
    TRACE(c, "PC=%06lX  %04X  RET -> 0x%06lX\n", (unsigned long)cur_pc, op,
          (unsigned long)c->pc);
    return;
  }
  /* ICALL -- 1001 0101 0000 1001  (push PC, jump to Z) */
  if (op == 0x9509) {
    uint16_t z = avr_get_z(c);
    push16(c, (uint16_t)(cur_pc + 2));
    c->pc = (uint32_t)z << 1;
    c->cycles += cyc(c, 3, 2, 2, 3);
    TRACE(c, "PC=%06lX  %04X  ICALL\n", (unsigned long)cur_pc, op);
    return;
  }
  /* RETI -- 1001 0101 0001 1000  (RET + I flag set) */
  if (op == 0x9518) {
    c->pc = pop16(c);
    SETBIT(c->sreg, F_I);
    c->cycles += cyc(c, 4, 4, 4, 6);
    TRACE(c, "PC=%06lX  %04X  RETI\n", (unsigned long)cur_pc, op);
    return;
  }
  /* EICALL -- 1001 0101 0001 1001  (push PC, jump to EIND:Z) */
  if (op == 0x9519) {
    uint32_t z = ((uint32_t)c->eind << 16) | avr_get_z(c);
    push16(c, (uint16_t)(cur_pc + 2));
    c->pc = z << 1;
    c->cycles += cyc(c, 4, 3, 3, 0);
    TRACE(c, "PC=%06lX  %04X  EICALL\n", (unsigned long)cur_pc, op);
    return;
  }
  /* SLEEP -- 1001 0101 1000 1000  (treated as NOP, no peripherals) */
  if (op == 0x9588) {
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  SLEEP\n", (unsigned long)cur_pc, op);
    return;
  }
  /* BREAK -- 1001 0101 1001 1000  (treated as NOP, no debug unit) */
  if (op == 0x9598) {
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  BREAK\n", (unsigned long)cur_pc, op);
    return;
  }
  /* WDR -- 1001 0101 1010 1000  (watchdog reset, no-op here) */
  if (op == 0x95A8) {
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  WDR\n", (unsigned long)cur_pc, op);
    return;
  }
  /* LPM (implicit R0, Z) -- 1001 0101 1100 1000 */
  if (op == 0x95C8) {
    c->R[0] = c->flash[avr_get_z(c)];
    c->cycles += 3;
    TRACE(c, "PC=%06lX  %04X  LPM\n", (unsigned long)cur_pc, op);
    return;
  }
  /* ELPM (implicit R0, RAMPZ:Z) -- 1001 0101 1101 1000 */
  if (op == 0x95D8) {
    uint32_t z = ((uint32_t)c->rampz << 16) | avr_get_z(c);
    c->R[0] = c->flash[z % c->flash_bytes];
    c->cycles += 3;
    TRACE(c, "PC=%06lX  %04X  ELPM\n", (unsigned long)cur_pc, op);
    return;
  }
  /* SPM -- 1001 0101 1110 1000  (write R1:R0 word to flash at RAMPZ:Z)
   * SPM Z+ -- 1001 0101 1111 1000  (same, then increment Z by 2) */
  if (op == 0x95E8 || op == 0x95F8) {
    uint32_t z = ((uint32_t)c->rampz << 16) | avr_get_z(c);
    if (z + 1 < c->flash_bytes) {
      c->flash[z] = c->R[0];
      c->flash[z + 1] = c->R[1];
    }
    if (op == 0x95F8) {
      z = (z + 2) & 0xFFFFFF;
      c->rampz = (uint8_t)((z >> 16) & 0xFF);
      avr_set_z(c, (uint16_t)(z & 0xFFFF));
    }
    c->cycles += 4;
    TRACE(c, "PC=%06lX  %04X  SPM\n", (unsigned long)cur_pc, op);
    return;
  }

  /* JMP k -- 1001 010k kkkk 110k + 16 more k bits (22-bit absolute) */
  if ((op & 0xFE0E) == 0x940C) {
    uint16_t op2 = avr_flash_word(c, c->pc);
    c->pc += 2;
    uint32_t k = decode_jmp_call_k(op, op2);
    c->pc = k << 1;
    c->cycles += 3;
    TRACE(c, "PC=%06lX  %04X %04X  JMP 0x%06lX\n", (unsigned long)cur_pc, op,
          op2, (unsigned long)c->pc);
    return;
  }
  /* CALL k -- 1001 010k kkkk 111k + 16 more k bits */
  if ((op & 0xFE0E) == 0x940E) {
    uint16_t op2 = avr_flash_word(c, c->pc);
    c->pc += 2;
    uint32_t k = decode_jmp_call_k(op, op2);
    push16(c, (uint16_t)(cur_pc + 4));
    c->pc = k << 1;
    c->cycles += cyc(c, 4, 3, 3, 0); /* CALL (N/A on AVRrc) */
    TRACE(c, "PC=%06lX  %04X %04X  CALL 0x%06lX\n", (unsigned long)cur_pc, op,
          op2, (unsigned long)c->pc);
    return;
  }
  /* DEC Rd -- 1001 010d dddd 1010 */
  if ((op & 0xFE0F) == 0x940A) {
    uint8_t d = reg_d5(op);
    uint8_t res = (uint8_t)(c->R[d] - 1);
    c->R[d] = res;
    set_flag(c, F_Z, res == 0);
    set_flag(c, F_N, (res & 0x80) != 0);
    set_flag(c, F_V, res == 0x7F);
    set_flag(c, F_S, GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V));
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  DEC R%u -> 0x%02X\n", (unsigned long)cur_pc, op,
          d, res);
    return;
  }
  /* DES K -- 1001 0100 KKKK 1011  (one DES round; H selects en/decrypt) */
  if ((op & 0xFF0F) == 0x940B) {
    int k = (op >> 4) & 0x0F;
    exec_des(c, k, GETBIT(c->sreg, F_H));
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  DES round %d (%s)\n", (unsigned long)cur_pc, op,
          k, GETBIT(c->sreg, F_H) ? "decrypt" : "encrypt");
    return;
  }

  /* ADIW Rd+1:Rd,K -- 1001 0110 KKdd KKKK  (d in {24,26,28,30}) */
  if ((op & 0xFF00) == 0x9600) {
    uint8_t dd = (uint8_t)((op >> 4) & 0x03);
    uint8_t d = (uint8_t)(24 + dd * 2);
    uint8_t K = imm_K6(op);
    uint16_t orig = (uint16_t)((c->R[d + 1] << 8) | c->R[d]);
    uint32_t r32 = (uint32_t)orig + K;
    uint16_t res = (uint16_t)r32;
    c->R[d] = (uint8_t)(res & 0xFF);
    c->R[d + 1] = (uint8_t)((res >> 8) & 0xFF);
    set_flag(c, F_C, (r32 & 0x10000) != 0);
    set_flag(c, F_Z, res == 0);
    set_flag(c, F_N, (res & 0x8000) != 0);
    set_flag(c, F_V, (~orig & res & 0x8000) != 0);
    set_flag(c, F_S, GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V));
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  ADIW R%u:R%u,%u\n", (unsigned long)cur_pc, op,
          d + 1, d, K);
    return;
  }
  /* SBIW Rd+1:Rd,K -- 1001 0111 KKdd KKKK  (d in {24,26,28,30}) */
  if ((op & 0xFF00) == 0x9700) {
    uint8_t dd = (uint8_t)((op >> 4) & 0x03);
    uint8_t d = (uint8_t)(24 + dd * 2);
    uint8_t K = imm_K6(op);
    uint16_t orig = (uint16_t)((c->R[d + 1] << 8) | c->R[d]);
    uint32_t r32 = (uint32_t)orig - K;
    uint16_t res = (uint16_t)r32;
    c->R[d] = (uint8_t)(res & 0xFF);
    c->R[d + 1] = (uint8_t)((res >> 8) & 0xFF);
    set_flag(c, F_C, (r32 & 0x10000) != 0);
    set_flag(c, F_Z, res == 0);
    set_flag(c, F_N, (res & 0x8000) != 0);
    set_flag(c, F_V, (orig & ~res & 0x8000) != 0);
    set_flag(c, F_S, GETBIT(c->sreg, F_N) ^ GETBIT(c->sreg, F_V));
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  SBIW R%u:R%u,%u\n", (unsigned long)cur_pc, op,
          d + 1, d, K);
    return;
  }

  /* CBI A,b -- 1001 1000 AAAA Abbb  (clear bit b in I/O register A) */
  if ((op & 0xFF00) == 0x9800) {
    uint8_t A = (uint8_t)((op >> 3) & 0x1F);
    uint8_t b = (uint8_t)(op & 0x07);
    uint32_t addr = 0x20u + A;
    avr_write_data(c, addr, (uint8_t)(avr_read_data(c, addr) & ~(1u << b)));
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  CBI 0x%02X,%u\n", (unsigned long)cur_pc, op,
          (unsigned)addr, b);
    return;
  }
  /* SBIC A,b -- 1001 1001 AAAA Abbb  (skip next if I/O bit b is clear) */
  if ((op & 0xFF00) == 0x9900) {
    uint8_t A = (uint8_t)((op >> 3) & 0x1F);
    uint8_t b = (uint8_t)(op & 0x07);
    uint8_t v = avr_read_data(c, 0x20u + A);
    int skip = !(v & (1u << b));
    c->cycles += 1;
    if (skip)
      do_skip(c);
    TRACE(c, "PC=%06lX  %04X  SBIC 0x%02X,%u %s\n", (unsigned long)cur_pc, op,
          0x20u + A, b, skip ? "(skip)" : "");
    return;
  }
  /* SBI A,b -- 1001 1010 AAAA Abbb  (set bit b in I/O register A) */
  if ((op & 0xFF00) == 0x9A00) {
    uint8_t A = (uint8_t)((op >> 3) & 0x1F);
    uint8_t b = (uint8_t)(op & 0x07);
    uint32_t addr = 0x20u + A;
    avr_write_data(c, addr, (uint8_t)(avr_read_data(c, addr) | (1u << b)));
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  SBI 0x%02X,%u\n", (unsigned long)cur_pc, op,
          (unsigned)addr, b);
    return;
  }
  /* SBIS A,b -- 1001 1011 AAAA Abbb  (skip next if I/O bit b is set) */
  if ((op & 0xFF00) == 0x9B00) {
    uint8_t A = (uint8_t)((op >> 3) & 0x1F);
    uint8_t b = (uint8_t)(op & 0x07);
    uint8_t v = avr_read_data(c, 0x20u + A);
    int skip = (v & (1u << b)) != 0;
    c->cycles += 1;
    if (skip)
      do_skip(c);
    TRACE(c, "PC=%06lX  %04X  SBIS 0x%02X,%u %s\n", (unsigned long)cur_pc, op,
          0x20u + A, b, skip ? "(skip)" : "");
    return;
  }

  /* MUL Rd,Rr -- 1001 11rd dddd rrrr  (unsigned 8x8 -> R1:R0) */
  if ((op & 0xFC00) == 0x9C00) {
    uint8_t d = reg_d5(op);
    uint8_t r = reg_r5(op);
    uint16_t res = (uint16_t)((uint16_t)c->R[d] * (uint16_t)c->R[r]);
    c->R[0] = (uint8_t)(res & 0xFF);
    c->R[1] = (uint8_t)((res >> 8) & 0xFF);
    set_flag(c, F_C, (res & 0x8000) != 0);
    set_flag(c, F_Z, res == 0);
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  MUL R%u,R%u -> 0x%04X\n", (unsigned long)cur_pc,
          op, d, r, res);
    return;
  }

  /* IN/OUT Rd,A -- 1011 0AAd dddd AAAA  (bit 11 selects OUT vs IN) */
  if ((op & 0xF000) == 0xB000) {
    uint8_t d = reg_d5(op);
    uint8_t A = (uint8_t)(((op >> 5) & 0x30) | (op & 0x0F));
    if (op & 0x0800) {
      avr_write_data(c, 0x20u + A, c->R[d]);
      TRACE(c, "PC=%06lX  %04X  OUT 0x%02X,R%u\n", (unsigned long)cur_pc, op,
            0x20u + A, d);
    } else {
      c->R[d] = avr_read_data(c, 0x20u + A);
      TRACE(c, "PC=%06lX  %04X  IN R%u,0x%02X\n", (unsigned long)cur_pc, op, d,
            0x20u + A);
    }
    c->cycles += 1;
    return;
  }

  /* RJMP k -- 1100 kkkk kkkk kkkk  (relative jump, k is signed words) */
  if ((op & 0xF000) == 0xC000) {
    int16_t k = rjmp_offset(op);
    if (k == -1) {
      c->cycles += 1;
      TRACE(c, "PC=%06lX  %04X  RJMP -1 (HALT)\n", (unsigned long)cur_pc, op);
      c->running = 0;
      return;
    }
    c->pc = (uint32_t)((int32_t)cur_pc + 2 + ((int32_t)k * 2));
    c->cycles += 2;
    TRACE(c, "PC=%06lX  %04X  RJMP %+d -> 0x%06lX\n", (unsigned long)cur_pc, op,
          k, (unsigned long)c->pc);
    return;
  }
  /* RCALL k -- 1101 kkkk kkkk kkkk  (relative call, pushes return addr) */
  if ((op & 0xF000) == 0xD000) {
    int16_t k = rjmp_offset(op);
    push16(c, (uint16_t)(cur_pc + 2));
    c->pc = (uint32_t)((int32_t)cur_pc + 2 + ((int32_t)k * 2));
    c->cycles += cyc(c, 3, 2, 2, 3);
    TRACE(c, "PC=%06lX  %04X  RCALL %+d\n", (unsigned long)cur_pc, op, k);
    return;
  }

  /* LDI Rd,K -- 1110 KKKK dddd KKKK  (load immediate, d in R16..R31) */
  if ((op & 0xF000) == 0xE000) {
    uint8_t d = reg_d4(op);
    uint8_t K = imm_K8(op);
    c->R[d] = K;
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  LDI R%u,0x%02X\n", (unsigned long)cur_pc, op, d,
          K);
    return;
  }

  /* BRBS s,k -- 1111 00kk kkkk ksss  (branch if SREG bit s is set) */
  if ((op & 0xFC00) == 0xF000) {
    uint8_t s = (uint8_t)(op & 0x07);
    int8_t k = branch_offset(op);
    int take = GETBIT(c->sreg, s);
    if (take) {
      c->pc = (uint32_t)((int32_t)cur_pc + 2 + ((int32_t)k * 2));
      c->cycles += 2;
    } else {
      c->cycles += 1;
    }
    TRACE(c, "PC=%06lX  %04X  BRBS %u,%+d %s\n", (unsigned long)cur_pc, op, s,
          k, take ? "(taken)" : "");
    return;
  }
  /* BRBC s,k -- 1111 01kk kkkk ksss  (branch if SREG bit s is clear) */
  if ((op & 0xFC00) == 0xF400) {
    uint8_t s = (uint8_t)(op & 0x07);
    int8_t k = branch_offset(op);
    int take = !GETBIT(c->sreg, s);
    if (take) {
      c->pc = (uint32_t)((int32_t)cur_pc + 2 + ((int32_t)k * 2));
      c->cycles += 2;
    } else {
      c->cycles += 1;
    }
    TRACE(c, "PC=%06lX  %04X  BRBC %u,%+d %s\n", (unsigned long)cur_pc, op, s,
          k, take ? "(taken)" : "");
    return;
  }

  /* BLD Rd,b -- 1111 100d dddd 0bbb  (load bit b of Rd from T flag) */
  if ((op & 0xFE08) == 0xF800) {
    uint8_t d = reg_d5(op);
    uint8_t b = (uint8_t)(op & 0x07);
    if (GETBIT(c->sreg, F_T))
      c->R[d] |= (uint8_t)(1u << b);
    else
      c->R[d] &= (uint8_t)~(1u << b);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  BLD R%u,%u\n", (unsigned long)cur_pc, op, d, b);
    return;
  }
  /* BST Rd,b -- 1111 101d dddd 0bbb  (store bit b of Rd into T flag) */
  if ((op & 0xFE08) == 0xFA00) {
    uint8_t d = reg_d5(op);
    uint8_t b = (uint8_t)(op & 0x07);
    set_flag(c, F_T, (c->R[d] & (1u << b)) != 0);
    c->cycles += 1;
    TRACE(c, "PC=%06lX  %04X  BST R%u,%u\n", (unsigned long)cur_pc, op, d, b);
    return;
  }
  /* SBRC Rr,b -- 1111 110r rrrr 0bbb  (skip next if bit b of Rr is clear) */
  if ((op & 0xFE08) == 0xFC00) {
    uint8_t r = reg_d5(op);
    uint8_t b = (uint8_t)(op & 0x07);
    int skip = !(c->R[r] & (1u << b));
    c->cycles += 1;
    if (skip)
      do_skip(c);
    TRACE(c, "PC=%06lX  %04X  SBRC R%u,%u %s\n", (unsigned long)cur_pc, op, r,
          b, skip ? "(skip)" : "");
    return;
  }
  /* SBRS Rr,b -- 1111 111r rrrr 0bbb  (skip next if bit b of Rr is set) */
  if ((op & 0xFE08) == 0xFE00) {
    uint8_t r = reg_d5(op);
    uint8_t b = (uint8_t)(op & 0x07);
    int skip = (c->R[r] & (1u << b)) != 0;
    c->cycles += 1;
    if (skip)
      do_skip(c);
    TRACE(c, "PC=%06lX  %04X  SBRS R%u,%u %s\n", (unsigned long)cur_pc, op, r,
          b, skip ? "(skip)" : "");
    return;
  }

  /* No pattern matched: flag the opcode and halt the core. */
  fprintf(stderr, "Unknown opcode 0x%04X at PC=0x%06lX\n", op,
          (unsigned long)cur_pc);
  c->unknown_opcode = 1;
  c->running = 0;
}

int avr_load_hex(avr_t *c, const char *fn) {
  FILE *f = fopen(fn, "r");
  if (!f) {
    perror(fn);
    return -1;
  }

  char line[600];
  uint32_t base = 0;

  while (fgets(line, sizeof(line), f)) {
    if (line[0] != ':')
      continue;

    unsigned cnt, addr, type, chk = 0, i;
    if (sscanf(line + 1, "%2x%4x%2x", &cnt, &addr, &type) != 3)
      continue;

    for (i = 0; i < cnt + 4; i++) {
      unsigned v;
      if (sscanf(line + 1 + i * 2, "%2x", &v) != 1) {
        fclose(f);
        return -1;
      }
      chk += v;
    }
    unsigned cksum_byte;
    if (sscanf(line + 1 + (cnt + 4) * 2, "%2x", &cksum_byte) != 1) {
      fclose(f);
      return -1;
    }
    chk += cksum_byte;
    if ((chk & 0xFF) != 0) {
      fprintf(stderr, "HEX checksum error\n");
      fclose(f);
      return -1;
    }

    if (type == 0) {
      for (i = 0; i < cnt; i++) {
        unsigned v;
        sscanf(line + 9 + i * 2, "%2x", &v);
        uint32_t a = base + addr + i;
        if (a >= c->flash_bytes) {
          fprintf(stderr, "HEX overflows flash\n");
          fclose(f);
          return -1;
        }
        c->flash[a] = (uint8_t)v;
      }
    } else if (type == 4) {
      unsigned hi;
      sscanf(line + 9, "%4x", &hi);
      base = ((uint32_t)hi) << 16;
    } else if (type == 1) {
      break;
    }
  }
  fclose(f);
  return 0;
}

void avr_dump_regs(const avr_t *c) {
  int i;
  printf("Device: %s (%s)  flash=%u SRAM=%u EEPROM=%u\n", c->device,
         avr_core_name(c->core), c->flash_bytes, c->sram_bytes,
         c->eeprom_bytes);
  printf("Registers:\n");
  for (i = 0; i < AVR_REG_COUNT; i++) {
    printf("R%-2d = 0x%02X", i, c->R[i]);
    printf(i % 4 == 3 ? "\n" : " | ");
  }
  printf("\nPC   = 0x%06lX\n", (unsigned long)c->pc);
  printf("SP   = 0x%04X\n", c->sp);
  printf("SREG = 0x%02X  [%c%c%c%c%c%c%c%c]\n", c->sreg,
         (c->sreg & 0x80) ? 'I' : '-', (c->sreg & 0x40) ? 'T' : '-',
         (c->sreg & 0x20) ? 'H' : '-', (c->sreg & 0x10) ? 'S' : '-',
         (c->sreg & 0x08) ? 'V' : '-', (c->sreg & 0x04) ? 'N' : '-',
         (c->sreg & 0x02) ? 'Z' : '-', (c->sreg & 0x01) ? 'C' : '-');
  printf("Cycles = %llu\n", (unsigned long long)c->cycles);
}

const char *avr_core_name(avr_core_class_t cls) {
  switch (cls) {
  case AVR_CORE_RC:
    return "AVRrc";
  case AVR_CORE_E:
    return "AVRe";
  case AVR_CORE_EP:
    return "AVRe+";
  case AVR_CORE_XT:
    return "AVRxt";
  case AVR_CORE_XM:
    return "AVRxm";
  default:
    return "generic";
  }
}

void avr_list_devices(void) {
  printf("%-22s %-7s %9s %8s %8s\n", "DEVICE", "CORE", "FLASH", "SRAM",
         "EEPROM");
  for (int i = 0; i < avr_device_count; i++) {
    const avr_device_t *d = &avr_device_table[i];
    printf("%-22s %-7s %8uB %7uB %7uB\n", d->name, avr_core_name(d->core),
           d->flash_bytes, d->sram_bytes, d->eeprom_bytes);
  }
}
