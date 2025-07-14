# AVR-VM Instruction Set Manual

**For: ATmega1284-Class Devices**  
**Target: AVR-VM Simulation (`avr_vm.c`)**  
**Document Version:** 2024-06  
**License:** CC0/Public Domain

---

## Table of Contents

1. [Overview](#overview)
2. [CPU Architecture](#cpu-architecture)
3. [Status Register (SREG)](#status-register-sreg)
4. [Instruction Summary Table](#instruction-summary-table)
5. [Instruction Details](#instruction-details)  
   * [Arithmetic and Logic Instructions](#arithmetic-and-logic-instructions)
   * [Data Transfer Instructions](#data-transfer-instructions)
   * [Branch Instructions](#branch-instructions)
   * [Bit and Bit-Test Instructions](#bit-and-bit-test-instructions)
   * [Shift and Rotate Instructions](#shift-and-rotate-instructions)
   * [MCU Control Instructions](#mcu-control-instructions)
   * [SREG Manipulation](#sreg-manipulation)
6. [Memory Map](#memory-map)
7. [Examples](#examples)
8. [Implementation Notes](#implementation-notes)

---

## Overview

This manual describes the instruction set implemented by the `avr_vm` simulator. The VM supports all core instructions of 8-bit AVR devices (ATmega1284-class), as well as basic I/O and stack operations. The syntax and instruction set closely match Microchip AVR Assembly, except as noted.

---

## CPU Architecture

- **Registers**: 32 general purpose (R0-R31)
- **PC**: 22-bit program counter
- **SP**: 16-bit stack pointer (stack in SRAM)
- **SREG**: 8-bit status register
- **Flash**: 128 Kwords (256 KB bytes)
- **SRAM**: 16 KB
- **EEPROM**: 4 KB
- **I/O Space**: 256 bytes

---

## Status Register (SREG)

| Bit | Flag | Meaning                         |
|-----|------|---------------------------------|
| 0   | C    | Carry Flag                      |
| 1   | Z    | Zero Flag                       |
| 2   | N    | Negative Flag                   |
| 3   | V    | Twos Complement Overflow        |
| 4   | S    | Sign (N⊕V)                      |
| 5   | H    | Half-carry Flag                 |
| 6   | T    | Bit Copy Storage                |
| 7   | I    | Global Interrupt Enable         |

---

## Instruction Summary Table

| Mnemonic      | Description                    | Example                                       |
|---------------|--------------------------------|-----------------------------------------------|
| `ADD`         | Add Registers                  | `add r17, r16`                                |
| `ADC`         | Add with Carry                 | `adc r17, r16`                                |
| `SUB`         | Subtract Registers             | `sub r17, r16`                                |
| `SBC`         | Subtract with Carry            | `sbc r17, r16`                                |
| `AND`         | And Registers                  | `and r17, r16`                                |
| `ORI`         | Or Register with Immediate     | `ori r17, 0x80`                               |
| `ANDI`        | And Register with Immediate    | `andi r17, 0x0F`                              |
| `EOR`         | Exclusive Or Registers         | `eor r17, r16`                                |
| `INC`         | Increment Register             | `inc r16`                                     |
| `DEC`         | Decrement Register             | `dec r16`                                     |
| `NEG`         | Two's Complement Negate        | `neg r16`                                     |
| `COM`         | Complement                     | `com r16`                                     |
| `SER`         | Set Register                   | `ser r16` (R16..R31)                          |
| `MOV`         | Copy Register                  | `mov r17, r16`                                |
| `MOVW`        | Copy Register Pair             | `movw r30, r28` (copy r28:r29 to r30:r31)     |
| `LDI`         | Load Immediate (R16-R31)       | `ldi r16, 0x42`                               |
| `LDS`         | Load Direct from SRAM          | `lds r16, 0x800`                              |
| `STS`         | Store Direct to SRAM           | `sts 0x800, r16`                              |
| `IN`          | Input from I/O                 | `in  r16, 0x17`                               |
| `OUT`         | Output to I/O                  | `out 0x18, r16`                               |
| `PUSH`        | Push Register on Stack         | `push r16`                                    |
| `POP`         | Pop Register from Stack        | `pop r16`                                     |
| `RJMP`        | Relative Jump                  | `rjmp main`                                   |
| `JMP`         | Absolute Jump                  | `jmp 0x100`                                   |
| `RCALL`       | Relative Call                  | `rcall func`                                  |
| `CALL`        | Absolute Call                  | `call 0x200`                                  |
| `RET`         | Return                         | `ret`                                         |
| `RETI`        | Return from Interrupt          | `reti`                                        |
| `CPI`         | Compare Immediate              | `cpi r16, 0x12`                               |
| `CP`          | Compare Register               | `cp r17, r16`                                 |
| `CPC`         | Compare with Carry             | `cpc r17, r16`                                |
| `CPSE`        | Compare, Skip if Equal         | `cpse r17, r16`                               |
| `TST`         | Test for Zero/N                | `tst r16`                                     |
| `SBI`         | Set Bit in I/O                 | `sbi 0x18, 5`                                 |
| `CBI`         | Clear Bit in I/O               | `cbi 0x18, 5`                                 |
| `BST`         | Store Bit to T                 | `bst r17, 0`                                  |
| `BLD`         | Load Bit from T                | `bld r17, 3`                                  |
| `SBRC`        | Skip if Bit in Reg Clear       | `sbrc r17, 2`                                 |
| `SBRS`        | Skip if Bit in Reg Set         | `sbrs r17, 2`                                 |
| `LSR`         | Logical Shift Right            | `lsr r17`                                     |
| `ROR`         | Rotate Right through Carry     | `ror r17`                                     |
| `ROL`         | Rotate Left through Carry      | `rol r17`                                     |
| `ASR`         | Arithmetic Shift Right         | `asr r17`                                     |
| `SWAP`        | Swap Nibbles                   | `swap r17`                                    |
| `NOP`         | No Operation                   | `nop`                                         |
| `SLEEP`       | Sleep (ignored in VM)          | `sleep`                                       |
| `WDR`         | Watchdog Reset (ignored)       | `wdr`                                         |
| `SEI`         | Set Interrupt Flag             | `sei`                                         |
| `CLI`         | Clear Interrupt Flag           | `cli`                                         |
| `CLC`         | Clear Carry Flag               | `clc`                                         |
| `CLZ`         | Clear Zero Flag                | `clz`                                         |
| `CLN`         | Clear Negative Flag            | `cln`                                         |
| `CLV`         | Clear Overflow Flag            | `clv`                                         |
| `CLT`         | Clear T Flag                   | `clt`                                         |

---

## Instruction Details

Below, each instruction is described in full **AVR-style**, including **syntax, description, cycles, flags**, and **assembly code snippet for the VM**.

---

### Arithmetic and Logic Instructions

#### ADD – Add without Carry

**Syntax:**  
```asm
add Rd, Rr
```
**Description:**  
Adds two registers (Rd + Rr), stores result in Rd.

**Operation:** `Rd ← Rd + Rr`

**Cycles:** 1  
**Flags:** C, Z, N, V, S, H

**Example:**  
```asm
.global main
main:
    ldi r16, 0x05
    ldi r17, 0x03
    add r16, r17 ; r16 = 0x08
    nop
```

---

#### ADC – Add with Carry

**Syntax:**  
```asm
adc Rd, Rr
```
**Description:**  
Adds Rd, Rr, and carry flag.

**Cycles:** 1  
**Flags:** C, Z, N, V, S, H

**Example:**  
```asm
.global main
main:
    ldi r16, 0xFF
    ldi r17, 0x01
    add r16, r17     ; carry set
    adc r16, r17     ; r16 = 1 + carry (r16 = 1 + 1 = 2)
    nop
```

---

#### SUB – Subtract without Carry

**Syntax:**  
```asm
sub Rd, Rr
```
**Description:**  
Subtracts Rr from Rd.

**Cycles:** 1  
**Flags:** C, Z, N, V, S, H

**Example:**  
```asm
.global main
main:
    ldi r16, 0x05
    ldi r17, 0x03
    sub r16, r17 ; r16 = 0x02
    nop
```

---

#### SBC – Subtract with Carry

**Syntax:**  
```asm
sbc Rd, Rr
```
**Description:**  
Subtracts Rr and carry from Rd.

**Cycles:** 1  
**Flags:** C, Z, N, V, S, H

**Example:**  
```asm
.global main
main:
    ldi r16, 0x05
    ldi r17, 0x05
    sub r16, r17 ; Z=1, C=0
    sbc r16, r17 ; with C=0, r16 remains 0
    nop
```

---

#### INC – Increment Register

**Syntax:**  
```asm
inc Rd
```
**Description:**  
Increments register by 1.

**Cycles:** 1  
**Flags:** Z, N, V, S

**Example:**  
```asm
.global main
main:
    ldi r16, 0x7F
    inc r16 ; r16 = 0x80
    nop
```

---

#### DEC – Decrement Register

**Syntax:**  
```asm
dec Rd
```
**Description:**  
Decrements register by 1.

**Cycles:** 1  
**Flags:** Z, N, V, S

**Example:**  
```asm
.global main
main:
    ldi r16, 0x01
    dec r16 ; r16 = 0x00, Z=1
    nop
```

---

#### NEG – Two's Complement Negate

**Syntax:**  
```asm
neg Rd
```
**Description:**  
Replaces register with its two's complement.

**Cycles:** 1  
**Flags:** C, Z, N, V, S, H

**Example:**  
```asm
.global main
main:
    ldi r16, 0x01
    neg r16 ; r16 = 0xFF
    nop
```

---

#### COM – One's Complement

**Syntax:**  
```asm
com Rd
```
**Description:**  
Replaces register with its one's complement.

**Cycles:** 1  
**Flags:** C, Z, N, V, S

**Example:**  
```asm
.global main
main:
    ldi r16, 0xAA
    com r16 ; r16 = 0x55
    nop
```

---

#### AND – Logical AND

**Syntax:**  
```asm
and Rd, Rr
```
**Description:**  
Bitwise AND between Rd and Rr.

**Cycles:** 1  
**Flags:** Z, N, V, S

**Example:**  
```asm
.global main
main:
    ldi r16, 0x0F
    ldi r17, 0xF0
    and r16, r17 ; r16 = 0x00, Z=1
    nop
```

---

#### OR – Logical OR

**Syntax:**  
```asm
or Rd, Rr
```
**Description:**  
Bitwise OR between Rd and Rr.

**Cycles:** 1  
**Flags:** Z, N, V, S

**Example:**  
```asm
.global main
main:
    ldi r16, 0xAA
    ldi r17, 0x0F
    or r16, r17 ; r16 = 0xAF
    nop
```

---

#### EOR – Logical Exclusive OR

**Syntax:**  
```asm
eor Rd, Rr
```
**Description:**  
Bitwise exclusive OR between Rd and Rr.

**Cycles:** 1  
**Flags:** Z, N, V, S

**Example:**  
```asm
.global main
main:
    ldi r16, 0xF0
    ldi r17, 0xAA
    eor r16, r17 ; r16 = 0x5A
    nop
```

---

#### ANDI – AND with Immediate

**Syntax:**  
```asm
andi Rd, K  
; Rd = R16..R31 only
```
**Description:**  
Bitwise AND between Rd and immediate K.

**Cycles:** 1  
**Flags:** Z, N, V, S

**Example:**  
```asm
.global main
main:
    ldi r16, 0xFB
    andi r16, 0xF0 ; r16 = 0xF0
    nop
```

---

#### ORI – OR with Immediate

**Syntax:**  
```asm
ori Rd, K  
; Rd = R16..R31 only
```
**Description:**  
Bitwise OR between Rd and immediate K.

**Cycles:** 1  
**Flags:** Z, N, V, S

**Example:**  
```asm
.global main
main:
    ldi r16, 0x0A
    ori r16, 0x80 ; r16 = 0x8A
    nop
```

---

#### SER – Set Register

**Syntax:**  
```asm
ser Rd
; Rd = R16..R31 only
```
**Description:**  
Sets Rd to 0xFF.

**Cycles:** 1  
**Flags:** None

**Example:**  
```asm
.global main
main:
    ser r17 ; r17 = 0xFF
    nop
```

---

### Data Transfer Instructions

#### MOV – Copy Register

**Syntax:**  
```asm
mov Rd, Rr
```
**Description:**  
Copies value from Rr to Rd.

**Cycles:** 1

**Example:**  
```asm
.global main
main:
    ldi r17, 0x67
    mov r16, r17 ; r16 = 0x67
    nop
```

---

#### MOVW – Copy Register Pair

**Syntax:**  
```asm
movw Rd, Rr
; Rd, Rr = even-numbered registers
```
**Description:**  
Copies Rr and Rr+1 to Rd and Rd+1.

**Cycles:** 1

**Example:**  
```asm
.global main
main:
    ldi r28, 0xAA
    ldi r29, 0xBB
    movw r30, r28 ; r30=r28, r31=r29
    nop
```

---

#### LDI – Load Immediate

**Syntax:**  
```asm
ldi Rd, K  
; Rd = R16..R31 only
```
**Description:**  
Loads constant K into Rd.

**Cycles:** 1

**Example:**  
```asm
.global main
main:
    ldi r16, 0x42
    nop
```

---

#### LDS – Load Direct from SRAM

**Syntax:**  
```asm
lds Rd, k
```
**Description:**  
Loads value from SRAM address k into Rd.

**Cycles:** 2

**Example:**  
```asm
.global main
main:
    ldi r16, 0x99
    sts 0x200, r16
    ldi r17, 0x00
    lds r17, 0x200 ; r17 = SRAM[0x200] = 0x99
```

---

#### STS – Store Direct to SRAM

**Syntax:**  
```asm
sts k, Rr
```
**Description:**  
Stores Rr into SRAM address k.

**Cycles:** 2

**Example:**  
```asm
.global main
main:
    ldi r16, 0x55
    sts 0x100, r16
```

---

#### IN – Input from I/O Register

**Syntax:**  
```asm
in Rd, A
```
**Description:**  
Loads value from I/O address A (0x00–0x3F) into Rd.

**Cycles:** 1

**Example:**  
```asm
.global main
main:
    in r16, 0x17 ; r16 = PINB (for example)
```

---

#### OUT – Output to I/O Register

**Syntax:**  
```asm
out A, Rr
```
**Description:**  
Stores contents of Rr to I/O address A (0x00-0x3F).

**Cycles:** 1

**Example:**  
```asm
.global main
main:
    ldi r16, 0xFF
    out 0x17, r16 ; DDRB
    out 0x18, r16 ; PORTB
```

---

#### PUSH – Push Register on Stack

**Syntax:**  
```asm
push Rr
```
**Description:**  
Pushes the value of Rr onto the stack.

**Cycles:** 2

**Example:**  
```asm
.global main
main:
    ldi r16, 0xAA
    push r16
    pop r17 ; r17 = 0xAA
    nop
```

---

#### POP – Pop Register from Stack

**Syntax:**  
```asm
pop Rd
```
**Description:**  
Pops byte from stack into Rd.

**Cycles:** 2

---

### Branch Instructions

#### RJMP – Relative Jump

**Syntax:**  
```asm
rjmp label
```
**Description:**  
Relative jump (±2K words).

**Cycles:** 2

**Example:**  
```asm
.global main
main:
    rjmp loop
loop:
    nop
    rjmp loop
```

---

#### JMP – Absolute Jump

**Syntax:**  
```asm
jmp k
```
**Description:**  
Jump to absolute program memory location.

**Cycles:** 3

**Example:**  
```asm
.global main
main:
    jmp somewhere
somewhere:
    ldi r16, 0x55
```

---

#### RCALL – Relative Call

**Syntax:**  
```asm
rcall label
```
**Description:**  
Relative subroutine call.

**Cycles:** 3

---

#### CALL – Absolute Call

**Syntax:**  
```asm
call k
```
**Description:**  
Call absolute address.

**Cycles:** 4

---

#### RET – Return

**Syntax:**  
```asm
ret
```
**Description:**  
Return from subroutine.

**Cycles:** 4

---

#### RETI – Return from Interrupt

**Syntax:**  
```asm
reti
```
**Description:**  
Return from interrupt.

**Cycles:** 4

---

#### CP – Compare Register

**Syntax:**  
```asm
cp Rd, Rr
```
**Description:**  
Compare Rd and Rr, update flags.

**Cycles:** 1

---

#### CPC – Compare with Carry

**Syntax:**  
```asm
cpc Rd, Rr
```
**Description:**  
Compare Rd and Rr with Carry.

**Cycles:** 1

---

#### CPSE – Compare, Skip if Equal

**Syntax:**  
```asm
cpse Rd, Rr
```
**Description:**  
Skip next instruction if Rd == Rr.

**Cycles:** 1/2

---

#### TST – Test for Zero/N

**Syntax:**  
```asm
tst Rd
```
**Description:**  
AND Rd, Rd for zero/negative test.

**Cycles:** 1

---

### Bit and Bit-Test Instructions

#### SBI – Set Bit in I/O Register

**Syntax:**  
```asm
sbi A, b
```
**Description:**  
Set bit b in I/O address A.

**Cycles:** 2

---

#### CBI – Clear Bit in I/O Register

**Syntax:**  
```asm
cbi A, b
```
**Description:**  
Clear bit b in I/O address A.

**Cycles:** 2

---

#### BST – Store Bit to T Flag

**Syntax:**  
```asm
bst Rr, b
```
**Description:**  
Copy bit b from Rr to T flag.

**Cycles:** 1

---

#### BLD – Load Bit from T Flag

**Syntax:**  
```asm
bld Rd, b
```
**Description:**  
Copy T flag to bit b of Rd.

**Cycles:** 1

---

#### SBRC – Skip if Bit in Register Clear

**Syntax:**  
```asm
sbrc Rr, b
```
**Description:**  
Skip next instruction if bit b in Rr is clear.

**Cycles:** 1/2

---

#### SBRS – Skip if Bit in Register Set

**Syntax:**  
```asm
sbrs Rr, b
```
**Description:**  
Skip next instruction if bit b in Rr is set.

**Cycles:** 1/2

---

### Shift and Rotate Instructions

#### LSR – Logical Shift Right

**Syntax:**  
```asm
lsr Rd
```
**Description:**  
Logical shift right by 1.

**Cycles:** 1

---

#### ROR – Rotate Right Through Carry

**Syntax:**  
```asm
ror Rd
```
**Description:**  
Rotate right through carry.

**Cycles:** 1

---

#### ROL – Rotate Left Through Carry

**Syntax:**  
```asm
rol Rd
```
**Description:**  
Rotate left through carry.

**Cycles:** 1

---

#### ASR – Arithmetic Shift Right

**Syntax:**  
```asm
asr Rd
```
**Description:**  
Arithmetic right shift (MSB unaffected).

**Cycles:** 1

---

#### SWAP – Swap Nibbles

**Syntax:**  
```asm
swap Rd
```
**Description:**  
Exchanges high and low nibbles in Rd.

**Cycles:** 1

---

### MCU Control Instructions

#### NOP – No Operation

**Syntax:**  
```asm
nop
```
**Cycles:** 1

---

#### SLEEP – Sleep (Ignored in VM)

**Syntax:**  
```asm
sleep
```
**Cycles:** 1

---

#### WDR – Watchdog Reset (Ignored in VM)

**Syntax:**  
```asm
wdr
```
**Cycles:** 1

---

### SREG Manipulation

#### SEI – Set Interrupt Flag

**Syntax:**  
```asm
sei
```

---

#### CLI – Clear Interrupt Flag

**Syntax:**  
```asm
cli
```

---

#### CLC – Clear Carry Flag

**Syntax:**  
```asm
clc
```

---

#### CLZ – Clear Zero Flag

**Syntax:**  
```asm
clz
```

---

#### CLN – Clear Negative Flag

**Syntax:**  
```asm
cln
```

---

#### CLV – Clear Overflow Flag

**Syntax:**  
```asm
clv
```

---

#### CLT – Clear T Flag

**Syntax:**  
```asm
clt
```

---

## Memory Map

| Address         | Size    | Usage          |
|-----------------|---------|---------------|
| 0x000000–0x01FFFF | 128Kwords | Flash      |
| 0x0000–0x001F   | 32B     | Registers     |
| 0x0020–0x00FF   | 224B    | I/O Registers |
| 0x0100–0x40FF   | 16 KB   | SRAM          |
| 0x0000–0x0FFF   | 4KB     | EEPROM        |

---

## Examples

### Toggle PORTB Forever

```asm
.global main

main:
    ldi     r16, 0xFF
    out     0x17, r16     ; DDRB = output
loop:
    out     0x18, r16     ; PORTB = 0xFF
    rjmp    loop
```

### Add and Push to Stack

```asm
.global main
main:
    ldi r17, 0x01
    ldi r18, 0x02
    add r17, r18
    push r17
    pop r16 ; r16 = sum
```

---

## Implementation Notes

- Only documented opcodes and forms are supported.
- Peripheral emulation is minimal: I/O registers read/write, no actual pin simulation.
- All code snippets are compatible with `avr-gcc` and `avra`.
 