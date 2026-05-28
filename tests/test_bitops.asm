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

; test_bitops.asm -- shifts, rotates, and bit / I/O-bit operations.
;
; Self-checking: a failed compare falls through to `rjmp fail`; the short
; branch hops over it on success. R16 = 0x42 on success, 0xEE on failure.

.global main

.equ PORTB, 0x05            ; I/O register number for SBI/CBI/SBIC/SBIS

main:
    ; LSR: 0x02 >> 1 = 0x01, carry out = 0
    ldi  r17, 0x02
    lsr  r17
    cpi  r17, 0x01
    breq 1f
    rjmp fail
1:
    ; LSR shifting bit 0 out sets carry: 0x01 >> 1 = 0x00, C = 1
    ldi  r17, 0x01
    lsr  r17
    brcs 1f
    rjmp fail
1:
    ; ROR rotates carry into bit 7: C=1, 0x00 -> 0x80
    sec
    ldi  r17, 0x00
    ror  r17
    cpi  r17, 0x80
    breq 1f
    rjmp fail
1:
    ; ASR preserves the sign bit: 0x80 -> 0xC0
    ldi  r17, 0x80
    asr  r17
    cpi  r17, 0xC0
    breq 1f
    rjmp fail
1:
    ; SWAP exchanges nibbles: 0x4B -> 0xB4
    ldi  r17, 0x4B
    swap r17
    cpi  r17, 0xB4
    breq 1f
    rjmp fail
1:
    ; SBI sets a bit, SBIS then sees it set and skips the rjmp
    sbi  PORTB, 3
    sbis PORTB, 3
    rjmp fail
    ; CBI clears the bit, SBIC then sees it clear and skips the rjmp
    cbi  PORTB, 3
    sbic PORTB, 3
    rjmp fail

    ; BST stores a register bit into T, BLD copies T into another bit
    ldi  r17, 0x80
    bst  r17, 7             ; T = 1
    clr  r18
    bld  r18, 0             ; r18 bit 0 = T = 1
    cpi  r18, 0x01
    breq 1f
    rjmp fail
1:
    ; SEC / CLC drive the carry flag directly
    sec
    brcs 1f
    rjmp fail
1:
    clc
    brcc 1f
    rjmp fail
1:
    ldi  r16, 0x42
    rjmp done
fail:
    ldi  r16, 0xEE
done:
    rjmp done
