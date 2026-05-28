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

; test_des.asm -- full DES encrypt/decrypt using the FIPS-46 test vector.
;
;   key        = 0x133457799BBCDFF1   (R8..R15, LSB in R8)
;   plaintext  = 0x0123456789ABCDEF   (R0..R7,  LSB in R0)
;   ciphertext = 0x85E813540F0AB405
;
; Encrypt = clear H then run rounds 0..15; decrypt = set H and run them again.
; R16 = 0x42 on success, 0xEE on the first failed byte check.

; Compare a low register against an expected byte; fail on mismatch.
.macro CHK reg, val
    mov  r16, \reg
    cpi  r16, \val
    breq .Lok\@
    rjmp fail
.Lok\@:
.endm

; Run all sixteen DES rounds in order.
.macro DES16
    des  0x00
    des  0x01
    des  0x02
    des  0x03
    des  0x04
    des  0x05
    des  0x06
    des  0x07
    des  0x08
    des  0x09
    des  0x0A
    des  0x0B
    des  0x0C
    des  0x0D
    des  0x0E
    des  0x0F
.endm

.global main
main:
    ; plaintext into R0..R7 (LSB in R0)
    ldi  r16, 0xEF
    mov  r0, r16
    ldi  r16, 0xCD
    mov  r1, r16
    ldi  r16, 0xAB
    mov  r2, r16
    ldi  r16, 0x89
    mov  r3, r16
    ldi  r16, 0x67
    mov  r4, r16
    ldi  r16, 0x45
    mov  r5, r16
    ldi  r16, 0x23
    mov  r6, r16
    ldi  r16, 0x01
    mov  r7, r16

    ; key into R8..R15 (LSB in R8)
    ldi  r16, 0xF1
    mov  r8, r16
    ldi  r16, 0xDF
    mov  r9, r16
    ldi  r16, 0xBC
    mov  r10, r16
    ldi  r16, 0x9B
    mov  r11, r16
    ldi  r16, 0x79
    mov  r12, r16
    ldi  r16, 0x57
    mov  r13, r16
    ldi  r16, 0x34
    mov  r14, r16
    ldi  r16, 0x13
    mov  r15, r16

    ; --- encrypt ---
    clh
    DES16

    ; expect ciphertext 0x85E813540F0AB405 in R0..R7
    CHK  r0, 0x05
    CHK  r1, 0xB4
    CHK  r2, 0x0A
    CHK  r3, 0x0F
    CHK  r4, 0x54
    CHK  r5, 0x13
    CHK  r6, 0xE8
    CHK  r7, 0x85

    ; --- decrypt (key is untouched, ciphertext already in R0..R7) ---
    seh
    DES16

    ; expect plaintext 0x0123456789ABCDEF back in R0..R7
    CHK  r0, 0xEF
    CHK  r1, 0xCD
    CHK  r2, 0xAB
    CHK  r3, 0x89
    CHK  r4, 0x67
    CHK  r5, 0x45
    CHK  r6, 0x23
    CHK  r7, 0x01

    ldi  r16, 0x42
    rjmp done
fail:
    ldi  r16, 0xEE
done:
    rjmp done
