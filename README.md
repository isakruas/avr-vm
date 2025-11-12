# AVR-VM

**For: ATmega1284-Class Devices**
**Target: AVR-VM Simulation (`avr_vm.c`)**
**Document Version:** 1.0 (2025-11)
**Author:** Isak Ruas
**License:** Apache License 2.0

---

## 1. Overview

This manual describes the instruction set implemented by the `AVR-VM` simulator (`avr_vm.c`). The VM is a C89-compatible core emulator for the 8-bit AVR ISA, targeting a device class similar to the ATmega1284.

It is designed to execute AVR-8 machine code loaded from standard Intel `.hex` files.

**Build:**
```bash
gcc -std=c89 -Wall -O2 avr_vm.c -o avr_vm
````

**Run:**

```bash
./avr_vm <file.hex> [options]
```

-----

## 2\. CPU Architecture & Memory

The VM emulates the following core architecture:

  * **General Purpose Registers:** 32 8-bit registers (R0–R31).
  * **Program Counter (PC):** 22-bit effective address (emulated as `uint32_t`), pointing to byte addresses in Flash.
  * **Stack Pointer (SP):** 16-bit stack pointer (`uint16_t`) that descends from the top of internal SRAM.
  * **Status Register (SREG):** 8-bit flag register.
  * **Program Memory (Flash):** 128 Kwords (256 KB)
  * **Data Memory (SRAM):** 16 KB
  * **EEPROM:** 4 KB *(Note: Memory is allocated, but no EEPROM access instructions are currently implemented.)*
  * **I/O Space:** 256 bytes (accessible via `IN`/`OUT` and memory-mapped)

-----

## 3\. Data Memory Map

The `avr_vm` implements a unified data address space. All data access (except for program flash) is handled by the `read_data` and `write_data` functions.

| Address Range | Size | Description |
| :--- | :--- | :--- |
| `0x0000 - 0x001F` | 32 Bytes | **Register File** (R0–R31) |
| `0x0020 - 0x00FF` | 224 Bytes | **I/O Register Space** |
| `0x0100 - 0x40FF` | 16 KB | **Internal SRAM** |

The Stack Pointer (`sp`) is initialized to the top of SRAM: `0x40FF`.

-----

## 4\. Status Register (SREG)

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Flag** | **I** | **T** | **H** | **S** | **V** | **N** | **Z** | **C** |
| **Bit (enum)** | `F_I` | `F_T` | `F_H` | `F_S` | `F_V` | `F_N` | `F_Z` | `F_C` |

  * **I:** Global Interrupt Enable
  * **T:** Bit Copy Storage (`BST`/`BLD`)
  * **H:** Half-carry Flag
  * **S:** Sign Flag (N ⊕ V)
  * **V:** Two's Complement Overflow
  * **N:** Negative Flag
  * **Z:** Zero Flag
  * **C:** Carry Flag

-----

## 5\. Implemented Instruction Set

This section details all instructions currently implemented in `avr_vm.c`.

### 5.1. Arithmetic and Logic Instructions

| Mnemonic | Description | Opcode Mask | Cycles | Flags |
| :--- | :--- | :--- | :--- | :--- |
| `ADD` | Add without Carry | `0x0C00` | 1 | C,Z,N,V,S,H |
| `ADC` | Add with Carry | `0x1C00` | 1 | C,Z,N,V,S,H |
| `SUB` | Subtract without Carry | `0x1800` | 1 | C,Z,N,V,S,H |
| `SBC` | Subtract with Carry | `0x0800` | 1 | C,Z,N,V,S,H |
| `AND` | Logical AND | `0x2000` | 1 | Z,N,V,S |
| `OR` | Logical OR | `0x2800` | 1 | Z,N,V,S |
| `EOR` | Exclusive OR | `0x2400` | 1 | Z,N,V,S |
| `ANDI` | AND with Immediate | `0x7000` | 1 | Z,N,V,S |
| `ORI` | OR with Immediate | `0x6000` | 1 | Z,N,V,S |
| `INC` | Increment | `0x9403` | 1 | Z,N,V,S |
| `DEC` | Decrement | `0x940A` | 1 | Z,N,V,S |
| `NEG` | Two's Complement Negate | `0x9401` | 1 | C,Z,N,V,S,H |
| `COM` | One's Complement | `0x9400` | 1 | C,Z,N,V,S |

**Aliases:**

  * `TST Rd` is implemented as `AND Rd, Rd`.
  * `CLR Rd` is implemented as `EOR Rd, Rd`.
  * `SBR Rd, K` is implemented as `ORI Rd, K`.
  * `CBR Rd, K` is implemented as `ANDI Rd, 0xFF-K`.

### 5.2. Branch Instructions

| Mnemonic | Description | Opcode Mask | Cycles | Flags |
| :--- | :--- | :--- | :--- | :--- |
| `RJMP` | Relative Jump (±2K words) | `0xC000` | 2 | - |
| `JMP` | Absolute Jump (22-bit) | `0x940C` | 3 | - |
| `RCALL` | Relative Call (±2K words) | `0xD000` | 3 | - |
| `CALL` | Absolute Call (22-bit) | `0x940E` | 4 | - |
| `RET` | Return from Subroutine | `0x9508` | 4 | - |
| `RETI` | Return from Interrupt | `0x9518` | 4 | I |
| `BRBS` | Branch if SREG Bit Set | `0xF000` | 1/2 | - |
| `BRBC` | Branch if SREG Bit Clear | `0xF400` | 1/2 | - |

### 5.3. Data Transfer Instructions

| Mnemonic | Description | Opcode Mask | Cycles | Flags |
| :--- | :--- | :--- | :--- | :--- |
| `MOV` | Copy Register | `0x2C00` | 1 | - |
| `MOVW` | Copy Register Pair | `0x0100` | 1 | - |
| `LDI` | Load Immediate (R16-R31) | `0xE000` | 1 | - |
| `LDS` | Load Direct from Data Space | `0x9000` | 2 | - |
| `STS` | Store Direct to Data Space | `0x9200` | 2 | - |
| `PUSH` | Push Register on Stack | `0x920F` | 2 | - |
| `POP` | Pop Register from Stack | `0x900F` | 2 | - |
| `IN` | In from I/O Location | `0xB000` | 1 | - |
| `OUT` | Out to I/O Location | `0xB800` | 1 | - |

**Aliases:**

  * `SER Rd` is implemented as `LDI Rd, 0xFF`.

### 5.4. Compare and Test Instructions

| Mnemonic | Description | Opcode Mask | Cycles | Flags |
| :--- | :--- | :--- | :--- | :--- |
| `CP` | Compare | `0x1400` | 1 | C,Z,N,V,S,H |
| `CPC` | Compare with Carry | `0x0400` | 1 | C,Z,N,V,S,H |
| `CPI` | Compare with Immediate | `0x3000` | 1 | C,Z,N,V,S,H |
| `CPSE` | Compare, Skip if Equal | `0x1000` | 1/2/3 | - |

**Note on Skip Cycles:** `CPSE`, `SBRC`, and `SBRS` take 1 cycle if no skip occurs. If a skip occurs, they take 2 cycles (to skip a 1-word instruction) or 3 cycles (to skip a 2-word instruction like `LDS`, `STS`, `CALL`, or `JMP`).

### 5.5. Bit and Bit-Test Instructions

| Mnemonic | Description | Opcode Mask | Cycles | Flags |
| :--- | :--- | :--- | :--- | :--- |
| `SBI` | Set Bit in I/O (I/O 0x00-0x1F) | `0x9A00` | 2 | - |
| `CBI` | Clear Bit in I/O (I/O 0x00-0x1F) | `0x9800` | 2 | - |
| `SBRC` | Skip if Bit in Register Clear | `0xFC00` | 1/2/3 | - |
| `SBRS` | Skip if Bit in Register Set | `0xFE00` | 1/2/3 | - |
| `BST` | Bit Store (Reg -\> T) | `0xFA00` | 1 | T |
| `BLD` | Bit Load (T -\> Reg) | `0xF800` | 1 | - |

### 5.6. Shift and Rotate Instructions

| Mnemonic | Description | Opcode Mask | Cycles | Flags |
| :--- | :--- | :--- | :--- | :--- |
| `LSR` | Logical Shift Right | `0x9406` | 1 | C,Z,N,V,S |
| `ROR` | Rotate Right through Carry | `0x9407` | 1 | C,Z,N,V,S |
| `ASR` | Arithmetic Shift Right | `0x9405` | 1 | C,Z,N,V,S |
| `ROL` | Rotate Left through Carry | `0x1C00` | 1 | C,Z,N,V,S,H |
| `SWAP` | Swap Nibbles | `0x9402` | 1 | - |

**Note:** `ROL Rd` is implemented as an alias for `ADC Rd, Rd`.

### 5.7. MCU Control & SREG Instructions

| Mnemonic | Description | Opcode | Cycles | Flag |
| :--- | :--- | :--- | :--- | :--- |
| `NOP` | No Operation | `0x0000` | 1 | - |
| `SLEEP` | Sleep (NOP in VM) | `0x9588` | 1 | - |
| `WDR` | Watchdog Reset (NOP in VM) | `0x95A8` | 1 | - |
| `SEI` | Set Global Interrupt Flag | `0x9478` | 1 | I=1 |
| `CLI` | Clear Global Interrupt Flag | `0x94F8` | 1 | I=0 |
| `SEC` | Set Carry Flag | `0x9408` | 1 | C=1 |
| `CLC` | Clear Carry Flag | `0x9488` | 1 | C=0 |
| `SEN` | Set Negative Flag | `0x9428` | 1 | N=1 |
| `CLN` | Clear Negative Flag | `0x94A8` | 1 | N=0 |
| `SEZ` | Set Zero Flag | `0x9418` | 1 | Z=1 |
| `CLZ` | Clear Zero Flag | `0x9498` | 1 | Z=0 |
| `SEV` | Set Overflow Flag | `0x9438` | 1 | V=1 |
| `CLV` | Clear Overflow Flag | `0x94B8` | 1 | V=0 |
| `SES` | Set Sign Flag | `0x9448` | 1 | S=1 |
| `CLS` | Clear Sign Flag | `0x94C8` | 1 | S=0 |
| `SEH` | Set Half-carry Flag | `0x9458` | 1 | H=1 |
| `CLH` | Clear Half-carry Flag | `0x94D8` | 1 | H=0 |
| `SET` | Set T Flag | `0x9468` | 1 | T=1 |
| `CLT` | Clear T Flag | `0x94E8` | 1 | T=0 |

-----

## 6\. Implementation Notes & Limitations

While titled "Fully-featured," the VM implementation has several key limitations:

1.  **No Pointer (`X`, `Y`, `Z`) Instructions:** The most significant omission. The VM **does not** implement any of the `LD`, `LDD`, `ST`, or `STD` instructions that use the X, Y, or Z pointer registers. This means code compiled by `avr-gcc` (which relies heavily on these) will not execute.
2.  **No Multiplication:** None of the `MUL`, `MULS`, `MULSU`, or `FMUL` instructions are implemented.
3.  **No Program Memory Access:** `LPM` (Load Program Memory) and `SPM` (Store Program Memory) are not implemented.
4.  **No Indirect Jumps:** `ICALL`, `IJMP`, `EICALL`, and `EIJMP` are not implemented.
5.  **Limited Extended Addressing:**
      * `JMP` and `CALL` *are* 22-bit, but they build the address from the opcode itself. They do **not** use the `EIND` register.
      * `LDS` and `STS` are 16-bit only. They do **not** use the `RAMPD` register for extended SRAM access.
      * The `rampx`, `rampy`, `rampz`, `rampd`, and `eind` registers are defined in the `avr_t` struct but are **not used** by any instruction.
6.  **No Peripherals:** The I/O space (`0x20-0xFF`) is a simple byte array. There is no simulation of timers, UART, SPI, or any other peripheral. `IN` and `OUT` simply read/write to this array.
7.  **No Interrupts:** While `SEI`, `CLI`, and `RETI` are implemented, there is no interrupt vector table or interrupt controller logic. No interrupts will ever be triggered.
8.  **No EEPROM Access:** The 4KB of EEPROM is allocated, but no instructions to read or write to it are implemented.

 