# AVR-VM

**An 8-bit AVR CPU core emulator written in C.**
**Author:** Isak Ruas
**License:** Apache License 2.0

---

## 1. Overview

AVR-VM is a software emulator of the 8-bit AVR CPU core (AVRe / AVRe+ /
AVRxm / AVRxt class). It decodes and executes AVR-8 machine code loaded
from standard Intel `.hex` files and models the register file, status
flags, and instruction cycle counts.

The decoder implements AVR instruction groups including pointer-based
loads/stores, the multiply family, program-memory access
(`LPM`/`ELPM`/`SPM`), indirect jumps/calls, the XMEGA atomic
read-modify-write instructions, and the XMEGA `DES` round.

### Project layout

| Path | Purpose |
| :--- | :--- |
| `src/avr_core.h` | Public API and core data structures (`avr_t`, flags, memory map). |
| `src/avr_core.c` | Instruction decoder and execution engine. |
| `src/main.c` | Command-line driver: loads a `.hex` file and runs it. |
| `tests/test_*.c` | C unit tests, one binary per instruction group. |
| `tests/test_*.asm` | Self-checking AVR assembly programs run end-to-end in the VM. |
| `tests/test_common.h` | Shared assertion harness for the C tests. |
| `Makefile` | Builds the CLI, the core library object, and the test binaries. |

---

## 2. Building and running

```bash
make all      # build bin/avr_vm and the core object
make test     # run both the C and the assembly test suites
make test-c   # build and run only the C tests (tests/test_*.c)
make test-asm # assemble and run only the assembly tests (tests/test_*.asm)
make clean    # remove build artifacts
```

The assembly suite requires the `avr-gcc` toolchain (`avr-gcc`,
`avr-objcopy`) to assemble `tests/test_*.asm` into `.hex` images.

Run a program:

```bash
./bin/avr_vm <file.hex> [options]
```

| Option | Effect |
| :--- | :--- |
| `-mmcu=DEVICE` | Select a device preset (core class + memory layout). |
| `--list-mcus` | List all supported MCU presets and exit. |
| `-t` | Trace: print each instruction as it executes. |
| `-n MAX` | Stop after `MAX` instructions (default: run until halt). |
| `-d` | Dump the register file and SREG at exit. |

Example:

```bash
./bin/avr_vm prog.hex -mmcu=atmega32 -d
./bin/avr_vm prog.hex --list-mcus
```

The CLI exit code is `0` on normal completion and `2` if the core halted on
an unknown/illegal opcode for the selected core.

---

## 3. CPU architecture and memory

  * **General-purpose registers:** 32 8-bit registers, R0–R31.
  * **Program counter (PC):** byte address into flash, held as `uint32_t`
    (instructions are 16-bit words, so the PC is always even).
  * **Stack pointer (SP):** 16-bit, initialized to the top of SRAM
    (`AVR_SRAM_START + AVR_SRAM_BYTES - 1`). Push post-decrements, pop
    pre-increments.
  * **Status register (SREG):** 8 flag bits.
  * **Program memory (flash):** device-dependent (from selected `-mmcu` preset).
  * **Data memory (SRAM):** device-dependent (from selected `-mmcu` preset).
  * **EEPROM:** device-dependent allocation (from selected `-mmcu` preset; see limitations in §7).
  * **I/O space:** device-dependent window `[0x20, RAMSTART)`, reachable through
    `IN`/`OUT` and the unified data map.
  * **Extended addressing:** `RAMPX`, `RAMPY`, `RAMPZ` extend the X/Y/Z
    pointers; `RAMPZ` extends `ELPM`; `EIND` extends `EICALL`/`EIJMP`.

---

## 4. Data memory map

All data access (everything except program flash) goes through
`avr_read_data` / `avr_write_data` over a single unified address space.
The exact boundaries are preset-dependent (`-mmcu`):

| Address range | Size | Description |
| :--- | :--- | :--- |
| `0x0000 – 0x001F` | 32 B | Register file (R0–R31) |
| `0x0020 – RAMSTART-1` | device-dependent | I/O register space |
| `RAMSTART – RAMEND` | device-dependent | Internal SRAM |

Without `-mmcu`, the generic preset is used.

---

## 5. Status register (SREG)

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Flag** | I | T | H | S | V | N | Z | C |
| **enum** | `F_I` | `F_T` | `F_H` | `F_S` | `F_V` | `F_N` | `F_Z` | `F_C` |

  * **I** – global interrupt enable
  * **T** – bit copy storage (`BST`/`BLD`)
  * **H** – half-carry
  * **S** – sign (N ⊕ V)
  * **V** – two's complement overflow
  * **N** – negative
  * **Z** – zero
  * **C** – carry

---

## 6. Implemented instruction set

### 6.1 Arithmetic and logic

| Mnemonic | Description |
| :--- | :--- |
| `ADD` / `ADC` | Add without / with carry |
| `ADIW` | Add immediate to word (R24/26/28/30) |
| `SUB` / `SBC` | Subtract without / with carry |
| `SUBI` / `SBCI` | Subtract immediate without / with carry |
| `SBIW` | Subtract immediate from word |
| `AND` / `ANDI` | Logical AND, register / immediate |
| `OR` / `ORI` | Logical OR, register / immediate |
| `EOR` | Exclusive OR |
| `COM` | One's complement |
| `NEG` | Two's complement negate |
| `INC` / `DEC` | Increment / decrement |
| `MUL` / `MULS` / `MULSU` | 8×8 multiply: unsigned / signed / signed·unsigned |
| `FMUL` / `FMULS` / `FMULSU` | Fractional multiply variants |

Common assembler aliases map onto these encodings: `TST`→`AND Rd,Rd`,
`CLR`→`EOR Rd,Rd`, `SBR`→`ORI`, `CBR`→`ANDI`, `SER`→`LDI Rd,0xFF`,
`ROL`→`ADC Rd,Rd`, `LSL`→`ADD Rd,Rd`.

### 6.2 Branch, jump, and call

| Mnemonic | Description |
| :--- | :--- |
| `RJMP` / `IJMP` / `EIJMP` | Relative / indirect (Z) / extended indirect jump |
| `JMP` | Absolute 22-bit jump |
| `RCALL` / `ICALL` / `EICALL` | Relative / indirect / extended indirect call |
| `CALL` | Absolute 22-bit call |
| `RET` / `RETI` | Return from subroutine / interrupt |
| `CP` / `CPC` / `CPI` | Compare, with carry, with immediate |
| `CPSE` | Compare and skip if equal |
| `SBRC` / `SBRS` | Skip if register bit clear / set |
| `SBIC` / `SBIS` | Skip if I/O bit clear / set |
| `BRBS` / `BRBC` | Branch if SREG bit set / clear |

`BRBS`/`BRBC` cover the named conditional branches (`BREQ`, `BRNE`,
`BRCS`, `BRCC`, `BRGE`, `BRLT`, etc.), which select a specific SREG bit.

### 6.3 Data transfer

| Mnemonic | Description |
| :--- | :--- |
| `MOV` / `MOVW` | Copy register / register pair |
| `LDI` | Load immediate (R16–R31) |
| `LD` / `ST` | Indirect load/store via X, Y, Z (with post-inc / pre-dec) |
| `LDD` / `STD` | Load/store with displacement, Y+q / Z+q |
| `LDS` / `STS` | Direct load/store, data space (16-bit address) |
| `LPM` / `ELPM` | Load from program memory (Z, extended via RAMPZ) |
| `SPM` | Store to program memory |
| `IN` / `OUT` | Read / write I/O register |
| `PUSH` / `POP` | Stack push / pop |
| `XCH` / `LAS` / `LAC` / `LAT` | XMEGA atomic read-modify-write on (Z) |

### 6.4 Bit and bit-test

| Mnemonic | Description |
| :--- | :--- |
| `LSR` / `ROR` / `ASR` | Logical / rotate-through-carry / arithmetic shift right |
| `SWAP` | Swap nibbles |
| `SBI` / `CBI` | Set / clear bit in I/O register |
| `BST` / `BLD` | Store register bit to T / load T into register bit |
| `BSET` / `BCLR` | Set / clear a SREG bit |

`BSET`/`BCLR` cover the single-flag mnemonics: `SEC`/`CLC`, `SEZ`/`CLZ`,
`SEN`/`CLN`, `SEV`/`CLV`, `SES`/`CLS`, `SEH`/`CLH`, `SET`/`CLT`,
`SEI`/`CLI`.

### 6.5 MCU control

| Mnemonic | Description |
| :--- | :--- |
| `NOP` | No operation |
| `SLEEP` | Accepted; no-op in the VM (no sleep modes) |
| `WDR` | Watchdog reset; no-op in the VM |
| `BREAK` | Accepted; no-op in the VM (does not halt) |
| `DES` | One DES round on R0–R7 with key R8–R15; H selects encrypt/decrypt |

---

## 7. Limitations

The emulator models the **CPU core**, not a complete microcontroller:

1.  **No peripherals.** The I/O space is a plain byte array. There is no
    timer, UART, SPI, ADC, or port logic; `IN`/`OUT` only read and write
    that array.
2.  **No interrupt controller.** `SEI`, `CLI`, and `RETI` manage the I
    flag, but there is no vector table and no interrupt is ever raised.
3.  **No EEPROM access instructions.** The 4 KB EEPROM buffer is
    allocated but not reachable from emulated code.
4.  **`LDS`/`STS` use a 16-bit address.** `RAMPD` is not applied, so
    extended direct addressing only matters on devices with more than 64 KB
    of data space. The 16 KB SRAM modeled here is fully covered by 16 bits,
    so this is not a practical limitation for this configuration.
5.  **`SLEEP`, `WDR`, and `BREAK`** are accepted and advance the PC but have
    no architectural side effects (they need the sleep/watchdog/OCD
    subsystems, which are not modeled). `DES` is fully implemented.

---

## 8. Tests

There are two complementary suites.

**C tests** (`tests/test_*.c`) — each file targets one instruction group
(`test_arithmetic`, `test_branch`, `test_compare`, `test_logic`,
`test_shift`, `test_bit`, `test_transfer`, `test_pointer`, `test_mul`,
`test_lpm`, `test_sreg`, `test_xmega`, `test_des`, `test_mcu`). They load
opcodes directly into flash, step the core, and assert on register and flag
state.

**Assembly tests** (`tests/test_*.asm`) — real AVR programs
(`test_alu`, `test_branch`, `test_transfer`, `test_bitops`, `test_mul`,
`test_des`) assembled with `avr-gcc` and run end-to-end in the VM. Each
program is
self-checking: it compares computed values against expected results and
writes a sentinel to **R16** — `0x42` if every check passed, `0xEE` on the
first failure — then halts in a self-loop. The runner executes the image
under an instruction budget and inspects R16 in the register dump.

`make test` runs C and ASM suites in an MCU matrix:

* iterates all devices reported by `--list-mcus`
* passes `-mmcu=<device>` to VM runs
* for ASM, recompiles each `.asm` with `avr-gcc -mmcu=<device>` before run
* skips incompatible test/device pairs (core or memory mismatch) and reports
  `PASS/FAIL/SKIP` per run

Use `make test-c` / `make test-asm` to run a single suite.
