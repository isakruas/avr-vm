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
  printf("  --list-mcus  list known devices and exit\n");
}

int main(int argc, char **argv) {
  int trace = 0;
  int dump = 0;
  uint64_t max_instr = 0;
  const char *mmcu = NULL;
  const char *hexfile = NULL;

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

  /* Run until the core halts or the instruction budget is exhausted. */
  uint64_t executed = 0;
  while (cpu.running) {
    avr_step(&cpu);
    executed++;
    if (max_instr && executed >= max_instr)
      break;
  }

  if (dump || cpu.unknown_opcode) {
    avr_dump_regs(&cpu);
  }

  int rc = cpu.unknown_opcode ? 2 : 0;
  avr_free(&cpu);
  return rc;
}
