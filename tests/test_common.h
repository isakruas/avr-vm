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
 * test_common.h -- minimal assertion harness shared by every test_*.c.
 *
 * CHECK/CHECK_EQ_U/CHECK_FLAG record a pass or print a located failure;
 * RUN_TEST resets the core before each case so tests stay independent;
 * TEST_SUMMARY prints the tally and returns a process exit code.
 */
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

/* Expose POSIX fileno/dup/dup2 under -std=c99. */
#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "avr_core.h"

/* Per-binary pass/fail counters and the name of the running test. */
static int g_pass = 0;
static int g_fail = 0;
static const char *g_current_test = "(unnamed)";

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (expr) {                                                                \
      g_pass++;                                                                \
    } else {                                                                   \
      g_fail++;                                                                \
      printf("  FAIL [%s] %s:%d: %s\n", g_current_test, __FILE__, __LINE__,    \
             #expr);                                                           \
    }                                                                          \
  } while (0)

#define CHECK_EQ_U(a, b)                                                       \
  do {                                                                         \
    uint64_t _av = (uint64_t)(a);                                              \
    uint64_t _bv = (uint64_t)(b);                                              \
    if (_av == _bv) {                                                          \
      g_pass++;                                                                \
    } else {                                                                   \
      g_fail++;                                                                \
      printf("  FAIL [%s] %s:%d: %s == %s  (got 0x%llX, want 0x%llX)\n",       \
             g_current_test, __FILE__, __LINE__, #a, #b,                       \
             (unsigned long long)_av, (unsigned long long)_bv);                \
    }                                                                          \
  } while (0)

#define CHECK_FLAG(c, bit, want)                                               \
  do {                                                                         \
    int _got = ((c)->sreg >> (bit)) & 1;                                       \
    int _w = (want) ? 1 : 0;                                                   \
    if (_got == _w) {                                                          \
      g_pass++;                                                                \
    } else {                                                                   \
      g_fail++;                                                                \
      printf("  FAIL [%s] %s:%d: flag %d  (got %d, want %d)\n",                \
             g_current_test, __FILE__, __LINE__, (int)(bit), _got, _w);        \
    }                                                                          \
  } while (0)

#define RUN_TEST(fn, cpu)                                                      \
  do {                                                                         \
    g_current_test = #fn;                                                      \
    avr_reset(cpu);                                                            \
    fn(cpu);                                                                   \
  } while (0)

#define TEST_SUMMARY(name)                                                     \
  do {                                                                         \
    printf("[%-20s] %d passed, %d failed\n", name, g_pass, g_fail);            \
    return g_fail ? 1 : 0;                                                     \
  } while (0)

/* Write a single 16-bit opcode at a byte address in flash. */
static inline void put_op(avr_t *c, uint32_t addr, uint16_t op) {
  avr_put_op(c, addr, op);
}

/* Load a sequence of opcodes starting at address 0. */

static inline void load_program(avr_t *c, const uint16_t *ops, size_t n) {
  for (size_t i = 0; i < n; i++) {
    avr_put_op(c, (uint32_t)(i * 2), ops[i]);
  }
}

/* Step the core n times, stopping early if it halts. */
static inline void run_n(avr_t *c, int n) {
  for (int i = 0; i < n && c->running; i++)
    avr_step(c);
}

/* Initialize test CPU. When AVR_TEST_MCU is set, use that preset. */
static inline int test_init_cpu(avr_t *c) {
  const char *mcu = getenv("AVR_TEST_MCU");
  if (mcu && *mcu) {
    if (avr_init_device(c, mcu) != 0) {
      fprintf(stderr, "Unknown AVR_TEST_MCU: %s\n", mcu);
      return 1;
    }
    return 0;
  }
  avr_init(c);
  return 0;
}

/* Step once with stderr suppressed, for opcodes that intentionally emit a
   diagnostic (e.g. the unknown-opcode halt). Keeps the test output clean. */
static inline void step_silent(avr_t *c) {
  fflush(stderr);
  int saved = dup(fileno(stderr));
  FILE *nul = freopen("/dev/null", "w", stderr);
  avr_step(c);
  fflush(stderr);
  if (nul && saved >= 0)
    dup2(saved, fileno(stderr));
  if (saved >= 0)
    close(saved);
}

#endif
