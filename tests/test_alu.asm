; Copyright 2025-present Isak Ruas
;
; Licensed under the Apache License, Version 2.0 (the "License");
; you may not use this file except in compliance with the License.
; You may obtain a copy of the License at
;
;     http://www.apache.org/licenses/LICENSE-2.0
;
; Unless required by applicable law or agreed to in writing, software
; distributed under the License is distributed on an "AS IS" BASIS,
; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; See the License for the specific language governing permissions and
; limitations under the License.

; test_alu.asm -- arithmetic and logic instructions.
;
; Self-checking program: each step compares a computed value against its
; expected result; on a mismatch it falls through to `rjmp fail`. The short
; conditional branch hops over that rjmp on success (the AVR idiom for a
; distant fail handler). R16 = 0x42 on success, 0xEE on the first failure;
; the program then spins in `done` so the runner can read R16 from a dump.

.global main
main:
    ; ADD: 0x10 + 0x20 = 0x30
    ldi  r17, 0x10
    ldi  r18, 0x20
    add  r17, r18
    cpi  r17, 0x30
    breq 1f
    rjmp fail
1:
    ; ADC: carry-in set, 0x01 + 0x01 + 1 = 0x03
    sec
    ldi  r17, 0x01
    ldi  r18, 0x01
    adc  r17, r18
    cpi  r17, 0x03
    breq 1f
    rjmp fail
1:
    ; SUB: 0x30 - 0x10 = 0x20
    ldi  r17, 0x30
    ldi  r18, 0x10
    sub  r17, r18
    cpi  r17, 0x20
    breq 1f
    rjmp fail
1:
    ; SBC: carry-in set, 0x20 - 0x10 - 1 = 0x0F
    sec
    ldi  r17, 0x20
    ldi  r18, 0x10
    sbc  r17, r18
    cpi  r17, 0x0F
    breq 1f
    rjmp fail
1:
    ; SUBI: 0x50 - 0x05 = 0x4B
    ldi  r17, 0x50
    subi r17, 0x05
    cpi  r17, 0x4B
    breq 1f
    rjmp fail
1:
    ; SBCI: carry-in set, 0x50 - 0x05 - 1 = 0x4A
    sec
    ldi  r17, 0x50
    sbci r17, 0x05
    cpi  r17, 0x4A
    breq 1f
    rjmp fail
1:
    ; AND: 0xF0 & 0x3C = 0x30
    ldi  r17, 0xF0
    ldi  r18, 0x3C
    and  r17, r18
    cpi  r17, 0x30
    breq 1f
    rjmp fail
1:
    ; OR: 0xF0 | 0x0C = 0xFC
    ldi  r17, 0xF0
    ldi  r18, 0x0C
    or   r17, r18
    cpi  r17, 0xFC
    breq 1f
    rjmp fail
1:
    ; EOR: 0xFF ^ 0x0F = 0xF0
    ldi  r17, 0xFF
    ldi  r18, 0x0F
    eor  r17, r18
    cpi  r17, 0xF0
    breq 1f
    rjmp fail
1:
    ; ANDI then ORI: (0xFF & 0x0F) | 0xA0 = 0xAF
    ldi  r17, 0xFF
    andi r17, 0x0F
    ori  r17, 0xA0
    cpi  r17, 0xAF
    breq 1f
    rjmp fail
1:
    ; COM: ~0x0F = 0xF0
    ldi  r17, 0x0F
    com  r17
    cpi  r17, 0xF0
    breq 1f
    rjmp fail
1:
    ; NEG: -0x01 = 0xFF
    ldi  r17, 0x01
    neg  r17
    cpi  r17, 0xFF
    breq 1f
    rjmp fail
1:
    ; INC then DEC around the signed boundary
    ldi  r17, 0x7F
    inc  r17
    cpi  r17, 0x80
    breq 1f
    rjmp fail
1:
    dec  r17
    cpi  r17, 0x7F
    breq 1f
    rjmp fail
1:
    ; ADIW: r25:r24 = 0x00FF + 1 = 0x0100
    ldi  r24, 0xFF
    ldi  r25, 0x00
    adiw r24, 1
    cpi  r24, 0x00
    breq 1f
    rjmp fail
1:
    cpi  r25, 0x01
    breq 1f
    rjmp fail
1:
    ; SBIW: 0x0100 - 1 = 0x00FF
    sbiw r24, 1
    cpi  r24, 0xFF
    breq 1f
    rjmp fail
1:
    cpi  r25, 0x00
    breq 1f
    rjmp fail
1:
    ldi  r16, 0x42          ; all checks passed
    rjmp done
fail:
    ldi  r16, 0xEE          ; a check failed
done:
    rjmp done
