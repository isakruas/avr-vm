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

; test_transfer.asm -- register moves and SRAM/stack access.
;
; Self-checking: a failed compare falls through to `rjmp fail`; the short
; branch hops over it on success. R16 = 0x42 on success, 0xEE on failure.

.global main
main:
    ; LDI / MOV
    ldi  r17, 0x5A
    mov  r18, r17
    cpi  r18, 0x5A
    breq 1f
    rjmp fail
1:
    ; MOVW copies a register pair: r23:r22 <- r21:r20
    ldi  r20, 0x11
    ldi  r21, 0x22
    movw r22, r20
    cpi  r22, 0x11
    breq 1f
    rjmp fail
1:
    cpi  r23, 0x22
    breq 1f
    rjmp fail
1:
    ; STS / LDS round trip through SRAM at 0x0200
    ldi  r17, 0x77
    sts  0x0200, r17
    lds  r19, 0x0200
    cpi  r19, 0x77
    breq 1f
    rjmp fail
1:
    ; ST X+ / ST X then read back with LD X+ / LD X
    ldi  r26, 0x10          ; X = 0x0310
    ldi  r27, 0x03
    ldi  r17, 0xC3
    st   X+, r17            ; mem[0x0310] = 0xC3, X -> 0x0311
    ldi  r17, 0xD4
    st   X,  r17            ; mem[0x0311] = 0xD4
    ldi  r26, 0x10          ; rewind X = 0x0310
    ldi  r27, 0x03
    ld   r18, X+            ; r18 = 0xC3, X -> 0x0311
    cpi  r18, 0xC3
    breq 1f
    rjmp fail
1:
    ld   r18, X             ; r18 = 0xD4
    cpi  r18, 0xD4
    breq 1f
    rjmp fail
1:
    ; LD -X pre-decrement: from 0x0312 step back to 0x0311
    ldi  r26, 0x12
    ldi  r27, 0x03
    ld   r18, -X
    cpi  r18, 0xD4
    breq 1f
    rjmp fail
1:
    ; STD Y+q / LDD Y+q with displacement
    ldi  r28, 0x00          ; Y = 0x0400
    ldi  r29, 0x04
    ldi  r17, 0x9E
    std  Y+5, r17           ; mem[0x0405] = 0x9E
    ldd  r18, Y+5
    cpi  r18, 0x9E
    breq 1f
    rjmp fail
1:
    ; PUSH / POP round trip through the stack
    ldi  r17, 0x3C
    push r17
    ldi  r17, 0x00
    pop  r17
    cpi  r17, 0x3C
    breq 1f
    rjmp fail
1:
    ldi  r16, 0x42
    rjmp done
fail:
    ldi  r16, 0xEE
done:
    rjmp done
