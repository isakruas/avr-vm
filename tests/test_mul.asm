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

; test_mul.asm -- multiply family (results land in r1:r0).
;
; Self-checking: a failed compare falls through to `rjmp fail`; the short
; branch hops over it on success. R16 = 0x42 on success, 0xEE on failure.

.global main
main:
    ; MUL unsigned: 0x10 * 0x10 = 0x0100  (r1:r0 = 0x01:0x00)
    ldi  r17, 0x10
    ldi  r18, 0x10
    mul  r17, r18
    mov  r19, r0
    cpi  r19, 0x00
    breq 1f
    rjmp fail
1:
    mov  r19, r1
    cpi  r19, 0x01
    breq 1f
    rjmp fail
1:
    ; MUL with carry: 0xFF * 0xFF = 0xFE01
    ldi  r17, 0xFF
    ldi  r18, 0xFF
    mul  r17, r18
    mov  r19, r0
    cpi  r19, 0x01
    breq 1f
    rjmp fail
1:
    mov  r19, r1
    cpi  r19, 0xFE
    breq 1f
    rjmp fail
1:
    ; MULS signed: (-5) * 10 = -50 = 0xFFCE
    ldi  r17, 0xFB
    ldi  r18, 0x0A
    muls r17, r18
    mov  r19, r0
    cpi  r19, 0xCE
    breq 1f
    rjmp fail
1:
    mov  r19, r1
    cpi  r19, 0xFF
    breq 1f
    rjmp fail
1:
    ; MULSU signed*unsigned: (-2) * 100 = -200 = 0xFF38
    ldi  r17, 0xFE
    ldi  r18, 0x64
    mulsu r17, r18
    mov  r19, r0
    cpi  r19, 0x38
    breq 1f
    rjmp fail
1:
    mov  r19, r1
    cpi  r19, 0xFF
    breq 1f
    rjmp fail
1:
    ; FMUL fractional: 0x80 * 0x80 = 0x4000, shifted left = 0x8000
    ldi  r17, 0x80
    ldi  r18, 0x80
    fmul r17, r18
    mov  r19, r0
    cpi  r19, 0x00
    breq 1f
    rjmp fail
1:
    mov  r19, r1
    cpi  r19, 0x80
    breq 1f
    rjmp fail
1:
    ldi  r16, 0x42
    rjmp done
fail:
    ldi  r16, 0xEE
done:
    rjmp done
