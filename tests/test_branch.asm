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

; test_branch.asm -- compare, conditional branch, skip, call/return.
;
; Self-checking: R16 = 0x42 on success, 0xEE on the first failed check.

.global main
main:
    ; BREQ taken when operands are equal
    ldi  r17, 0x10
    ldi  r18, 0x10
    cp   r17, r18
    breq 1f
    rjmp fail
1:
    ; BRNE taken when operands differ
    ldi  r18, 0x11
    cp   r17, r18
    brne 2f
    rjmp fail
2:
    ; BRCS taken: 0x10 - 0x20 borrows, so C = 1
    ldi  r17, 0x10
    ldi  r18, 0x20
    cp   r17, r18
    brcs 3f
    rjmp fail
3:
    ; BRCC taken: 0x20 - 0x10 does not borrow, so C = 0
    ldi  r17, 0x20
    ldi  r18, 0x10
    cp   r17, r18
    brcc 4f
    rjmp fail
4:
    ; BRLT taken (signed): -1 < 1
    ldi  r17, 0xFF
    ldi  r18, 0x01
    cp   r17, r18
    brlt 5f
    rjmp fail
5:
    ; BRGE taken (signed): 1 >= -1
    ldi  r17, 0x01
    ldi  r18, 0xFF
    cp   r17, r18
    brge 6f
    rjmp fail
6:
    ; CPI + BREQ
    ldi  r17, 0x55
    cpi  r17, 0x55
    breq 7f
    rjmp fail
7:
    ; CPSE skips the next instruction when operands are equal
    ldi  r17, 0x01
    ldi  r18, 0x01
    cpse r17, r18
    rjmp fail               ; skipped when equal
    ; CPSE does not skip when operands differ
    ldi  r18, 0x02
    cpse r17, r18
    rjmp 8f                 ; executed when not equal
    rjmp fail
8:
    ; SBRC skips when the tested bit is clear
    ldi  r17, 0xFE          ; bit 0 = 0
    sbrc r17, 0
    rjmp fail               ; skipped
    ; SBRS skips when the tested bit is set
    ldi  r17, 0x01          ; bit 0 = 1
    sbrs r17, 0
    rjmp fail               ; skipped

    ; RCALL / RET: subroutine writes 0xAB into r19
    rcall subr
    cpi  r19, 0xAB
    brne fail

    ldi  r16, 0x42
    rjmp done
subr:
    ldi  r19, 0xAB
    ret
fail:
    ldi  r16, 0xEE
done:
    rjmp done
