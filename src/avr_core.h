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
 * avr_core.h -- public API for the AVR-VM core.
 *
 * The core emulates an 8-bit AVR CPU (AVRe / AVRe+ / AVRxm / AVRxt class)
 * with a unified data address space:
 *
 *   0x0000 - 0x001F  Register file R0..R31
 *   0x0020 - 0x00FF  I/O register space
 *   0x0100 - 0x40FF  Internal SRAM (16 KiB)
 *
 * Program memory (flash) is byte-addressable but instructions are 16-bit
 * words, so all PC values are even byte offsets. RAMPX/Y/Z/D and EIND
 * implement 24-bit addressing for devices with more than 64 KiB of data
 * or 128 KiB of program memory.
 */
#ifndef AVR_CORE_H
#define AVR_CORE_H

#include <stdint.h>

/* Default ("generic") configuration used by avr_init when no device preset
 * is selected: a large memory map that accepts any program. avr_init_device
 * overrides these per device. */
#define AVR_FLASH_BYTES (256 * 1024) /* 128 Kwords */
#define AVR_SRAM_BYTES (16 * 1024)
#define AVR_EEPROM_BYTES (4 * 1024)
#define AVR_IO_BYTES 0x100
#define AVR_REG_COUNT 32
#define AVR_SRAM_START 0x100

/* SREG flag bit positions (bit 0 = C, bit 7 = I). */
enum { F_C = 0, F_Z, F_N, F_V, F_S, F_H, F_T, F_I };

/* CPU version class. AVR_CORE_UNKNOWN means "generic": all instructions
 * allowed and a single cycle model. A concrete class enables per-core cycle
 * timing and instruction gating. */
typedef enum {
  AVR_CORE_UNKNOWN = 0,
  AVR_CORE_RC, /* AVRrc  -- reduced core, 16 registers */
  AVR_CORE_E,  /* AVRe   -- classic, MOVW + enhanced LPM */
  AVR_CORE_EP, /* AVRe+  -- AVRe plus multiply / extended addressing */
  AVR_CORE_XT, /* AVRxt  -- megaAVR 0-series, AVR Dx/Ex */
  AVR_CORE_XM  /* AVRxm  -- XMEGA, adds RMW + DES */
} avr_core_class_t;

typedef struct {
  uint8_t R[AVR_REG_COUNT];
  uint32_t pc;
  uint16_t sp;
  uint8_t sreg;
  uint8_t rampx, rampy, rampz, rampd, eind;

  uint8_t *flash;
  uint8_t *sram;
  uint8_t *eeprom;
  uint8_t *io;

  /* Runtime memory configuration (see avr_init / avr_init_device). */
  const char *device;    /* selected device name, or "generic" */
  avr_core_class_t core; /* CPU version for timing / gating */
  uint32_t flash_bytes;
  uint32_t sram_bytes;
  uint32_t eeprom_bytes;
  uint32_t io_bytes;   /* size of the I/O window [0x20, sram_start) */
  uint32_t sram_start; /* first internal SRAM address (RAMSTART) */

  uint64_t cycles;
  int running;
  int trace;
  int unknown_opcode;
  uint32_t eempe_timer;
  uint32_t eeprom_write_cycles_left;
  uint32_t eeprom_write_addr;
  uint8_t eeprom_write_val;
  uint32_t timer0_acc;       /* Timer0 prescaler cycle accumulator */
  uint8_t timer0_compa_vec;  /* TIMER0_COMPA vector index for this device (0 = unsupported) */
} avr_t;

/* Lifecycle: allocate memories, zero state, free buffers. avr_init applies the
 * generic defaults; avr_init_device applies a device preset by name (returns 0
 * on success, non-zero if the device is unknown). */
void avr_init(avr_t *c);
int avr_init_device(avr_t *c, const char *device);
void avr_free(avr_t *c);
void avr_reset(avr_t *c);

/* Decode and execute one instruction starting at PC. */
void avr_step(avr_t *c);

/* Unified data-space access (R0..R31 / I/O / SRAM). */
uint8_t avr_read_data(avr_t *c, uint32_t addr);
void avr_write_data(avr_t *c, uint32_t addr, uint8_t v);

/* Flash helpers: read a 16-bit word, write a 16-bit opcode at a byte addr. */
uint16_t avr_flash_word(const avr_t *c, uint32_t byte_addr);
void avr_put_op(avr_t *c, uint32_t byte_addr, uint16_t op);

/* X = R27:R26, Y = R29:R28, Z = R31:R30. */
uint16_t avr_get_x(const avr_t *c);
uint16_t avr_get_y(const avr_t *c);
uint16_t avr_get_z(const avr_t *c);
void avr_set_x(avr_t *c, uint16_t v);
void avr_set_y(avr_t *c, uint16_t v);
void avr_set_z(avr_t *c, uint16_t v);

/* Intel-HEX loader and register dump. */
int avr_load_hex(avr_t *c, const char *fn);
void avr_dump_regs(const avr_t *c);

/* Human-readable core-class name; list every known device preset. */
const char *avr_core_name(avr_core_class_t cls);
void avr_list_devices(void);

#endif
