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
 * main.c -- command-line driver for the AVR-VM core.
 *
 * Loads an Intel-HEX program into flash and runs it instruction by
 * instruction until the core halts (unknown opcode) or the optional
 * instruction limit is reached.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avr_core.h"

static void usage(const char *prog) {
  printf("Usage: %s <file.hex> [-mmcu=DEVICE] [-t] [-n MAX_INSTR] [-d]\n",
         prog);
  printf("  -mmcu=DEVICE preload memory/core config for DEVICE\n");
  printf("  -t           enable instruction trace\n");
  printf("  -n MAX       stop after MAX instructions (default: unlimited)\n");
  printf("  -d           dump registers at exit\n");
  printf("  --irq=VEC            queue one interrupt vector (repeatable)\n");
  printf("  --irq-at=VEC:STEP    queue vector when executed-instr count reaches STEP\n");
  printf("  --irq-every=VEC:N    queue vector every N executed instructions\n");
  printf("  --list-mcus  list known devices and exit\n");
}

typedef struct {
  uint8_t vec;
  uint64_t step;
  int fired;
} irq_at_event_t;

typedef struct {
  uint8_t vec;
  uint64_t period;
  uint64_t next_at;
} irq_every_event_t;

static int parse_u64(const char *s, uint64_t *out) {
  char *end = NULL;
  unsigned long long v = strtoull(s, &end, 0);
  if (!s[0] || (end && *end != '\0')) return 0;
  *out = (uint64_t)v;
  return 1;
}

int main(int argc, char **argv) {
  int trace = 0;
  int dump = 0;
  uint64_t max_instr = 0;
  const char *mmcu = NULL;
  const char *hexfile = NULL;
  long peek_addr = -1; /* data-space address to print at exit, or -1 */
  int peek_len = 1;
  uint8_t startup_irqs[256];
  size_t startup_irq_count = 0;
  irq_at_event_t irq_at_events[256];
  size_t irq_at_count = 0;
  irq_every_event_t irq_every_events[256];
  size_t irq_every_count = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--list-mcus") == 0) {
      avr_list_devices();
      return 0;
    } else if (strcmp(argv[i], "-t") == 0) {
      trace = 1;
    } else if (strcmp(argv[i], "-d") == 0) {
      dump = 1;
    } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
      max_instr = (uint64_t)strtoull(argv[++i], NULL, 0);
    } else if (strncmp(argv[i], "-mmcu=", 6) == 0) {
      mmcu = argv[i] + 6;
    } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
      peek_addr = (long)strtol(argv[++i], NULL, 0);
    } else if (strcmp(argv[i], "-mlen") == 0 && i + 1 < argc) {
      peek_len = (int)strtol(argv[++i], NULL, 0);
    } else if (strncmp(argv[i], "--irq=", 6) == 0) {
      uint64_t vec = 0;
      if (!parse_u64(argv[i] + 6, &vec) || vec == 0 || vec > 255) {
        fprintf(stderr, "Invalid --irq vector '%s' (expected 1..255)\n", argv[i] + 6);
        return 1;
      }
      if (startup_irq_count >= 256) {
        fprintf(stderr, "Too many --irq options (max 256)\n");
        return 1;
      }
      startup_irqs[startup_irq_count++] = (uint8_t)vec;
    } else if (strncmp(argv[i], "--irq-at=", 9) == 0) {
      const char *arg = argv[i] + 9;
      const char *sep = strchr(arg, ':');
      uint64_t vec = 0, step = 0;
      if (!sep) {
        fprintf(stderr, "Invalid --irq-at format '%s' (expected VEC:STEP)\n", arg);
        return 1;
      }
      char left[32];
      size_t n = (size_t)(sep - arg);
      if (n == 0 || n >= sizeof(left)) {
        fprintf(stderr, "Invalid --irq-at vector field '%s'\n", arg);
        return 1;
      }
      memcpy(left, arg, n);
      left[n] = '\0';
      if (!parse_u64(left, &vec) || vec == 0 || vec > 255 || !parse_u64(sep + 1, &step)) {
        fprintf(stderr, "Invalid --irq-at '%s' (expected VEC:STEP, VEC=1..255)\n", arg);
        return 1;
      }
      if (irq_at_count >= 256) {
        fprintf(stderr, "Too many --irq-at options (max 256)\n");
        return 1;
      }
      irq_at_events[irq_at_count++] = (irq_at_event_t){(uint8_t)vec, step, 0};
    } else if (strncmp(argv[i], "--irq-every=", 12) == 0) {
      const char *arg = argv[i] + 12;
      const char *sep = strchr(arg, ':');
      uint64_t vec = 0, period = 0;
      if (!sep) {
        fprintf(stderr, "Invalid --irq-every format '%s' (expected VEC:N)\n", arg);
        return 1;
      }
      char left[32];
      size_t n = (size_t)(sep - arg);
      if (n == 0 || n >= sizeof(left)) {
        fprintf(stderr, "Invalid --irq-every vector field '%s'\n", arg);
        return 1;
      }
      memcpy(left, arg, n);
      left[n] = '\0';
      if (!parse_u64(left, &vec) || vec == 0 || vec > 255 || !parse_u64(sep + 1, &period) || period == 0) {
        fprintf(stderr, "Invalid --irq-every '%s' (expected VEC:N, VEC=1..255, N>0)\n", arg);
        return 1;
      }
      if (irq_every_count >= 256) {
        fprintf(stderr, "Too many --irq-every options (max 256)\n");
        return 1;
      }
      irq_every_events[irq_every_count++] = (irq_every_event_t){(uint8_t)vec, period, period};
    } else if (argv[i][0] != '-' && hexfile == NULL) {
      hexfile = argv[i];
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      usage(argv[0]);
      return 1;
    }
  }

  if (hexfile == NULL) {
    usage(argv[0]);
    return 1;
  }

  avr_t cpu;
  if (mmcu) {
    if (avr_init_device(&cpu, mmcu) != 0) {
      fprintf(stderr, "Unknown device: %s (try --list-mcus)\n", mmcu);
      return 1;
    }
  } else {
    avr_init(&cpu);
  }
  cpu.trace = trace;

  if (avr_load_hex(&cpu, hexfile) != 0) {
    avr_free(&cpu);
    return 1;
  }

  for (size_t i = 0; i < startup_irq_count; i++) {
    avr_raise_interrupt(&cpu, startup_irqs[i]);
  }

  /* Run until the core halts or the instruction budget is exhausted. */
  uint64_t executed = 0;
  while (cpu.running) {
    for (size_t i = 0; i < irq_at_count; i++) {
      if (!irq_at_events[i].fired && executed >= irq_at_events[i].step) {
        avr_raise_interrupt(&cpu, irq_at_events[i].vec);
        irq_at_events[i].fired = 1;
      }
    }
    for (size_t i = 0; i < irq_every_count; i++) {
      if (executed >= irq_every_events[i].next_at) {
        avr_raise_interrupt(&cpu, irq_every_events[i].vec);
        irq_every_events[i].next_at += irq_every_events[i].period;
      }
    }
    avr_step(&cpu);
    executed++;
    if (max_instr && executed >= max_instr)
      break;
  }

  if (dump || cpu.unknown_opcode) {
    avr_dump_regs(&cpu);
  }

  if (peek_addr >= 0) {
    for (int k = 0; k < peek_len; k++) {
      uint32_t a = (uint32_t)peek_addr + k;
      printf("MEM[0x%04lX] = 0x%02X\n", (unsigned long)a, avr_read_data(&cpu, a));
    }
  }

  int rc = cpu.unknown_opcode ? 2 : 0;
  avr_free(&cpu);
  return rc;
}
