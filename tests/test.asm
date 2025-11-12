; test.asm
;
; Complete test program for 'avr_vm.c'
; Exercises 100% of the CPU instructions implemented in the emulator.
;
; To assemble (compile) with the avr-gcc toolchain:
; avr-gcc -mmcu=atmega1284 -x assembler -o test.elf test.asm
; avr-objcopy -O ihex test.elf test.hex
;
; To run in the emulator:
; ./avr_vm test.hex

.global main

; I/O Definitions. Addresses must be < 0x20 for SBI/CBI
.equ DDRB,  0x04
.equ PORTB, 0x05
.equ PINB,  0x03

main:
; -- Data Transfer: LDI, MOV, MOVW, LDS, STS
ldi  r16, 0x5A        ; r16 = 0x5A
ldi  r17, 0xA5        ; r17 = 0xA5
mov  r18, r16         ; r18 = 0x5A
movw r20, r16         ; r20 = r16, r21 = r17
sts  0x0200, r18      ; SRAM[0x0200] = r18
lds  r19, 0x0200      ; r19 = SRAM[0x0200]
ldi  r22, 0xF0

; -- Arithmetic, Logical Immediate: ANDI, ORI, SER (alias for LDI)
andi r22, 0x0F        ; r22 = r22 & 0x0F
ori  r22, 0xC0        ; r22 = r22 | 0xC0 (alias SBR)
ser  r23              ; r23 = 0xFF (alias LDI)

; -- Arithmetic, Logic with registers: ADD, ADC, SUB, SBC, AND, OR, EOR
ldi  r24, 0x3C
ldi  r25, 0x0C
add  r24, r25
adc  r24, r25
sub  r24, r25
sbc  r24, r25
and  r24, r25
or   r24, r25
eor  r24, r25         ; r24 = 0x00 (alias CLR)

; -- INC/DEC/NEG/COM
inc  r25
dec  r25
neg  r25
com  r25

; -- Control flow: RJMP, JMP, RCALL, CALL, RET, RETI
; -- Compare: CP, CPC, TST (alias), CPI, CPSE
; -- Branch: BRBC, BRBS
ldi  r26, 1
ldi  r27, 1
cp   r26, r27         ; set Z flag (Z=1)
cpc  r26, r27         ; with C=0, still Z=1
tst  r26              ; r26=1, so Z=0
cpi  r26, 1           ; r26=1, so Z=1
cpse r26, r27         ; r26 == r27 (1==1), SKIPS next instruction
brbs 1, brbs_taken    ; This line is skipped
nop                   ; Executes this NOP

brbs_taken:
brbc 1, brbc_not_taken; Z flag (bit 1) is SET, so it does NOT branch
nop                   ; Executes this NOP
brbc_not_taken:
rcall short_func
call absolute_func
rjmp after_funcs

short_func:
ret

absolute_func:
reti

after_funcs:
; -- Bit ops: SBI, CBI, BST, BLD, SBRC, SBRS
;    (Using I/O addrs < 0x20)
ldi  r16, 0x00
out  DDRB, r16        ; DDRB (0x04) = 0
sbi  DDRB, 3          ; Set bit 3 of I/O 0x04
cbi  DDRB, 3          ; Clear bit 3 of I/O 0x04
bst  r17, 0           ; T = bit 0 of r17 (r17=0xA5, T=1)
bld  r18, 0           ; bit 0 of r18 = T (r18=0x5A -> 0x5B)

; r18 = 0x5B (0101 1011)
sbrc r18, 0           ; Bit 0 is SET, does NOT skip
nop                   ; Executes this NOP
sbrs r18, 1           ; Bit 1 is SET, SKIPS
nop                   ; This line is skipped
nop                   ; Executes this NOP

; -- Stack: PUSH/POP
push r19
pop  r20

; -- IO: IN/OUT
in   r21, PINB        ; r21 = I/O[0x03]
out  PORTB, r21       ; I/O[0x05] = r21

; -- Shift/rotate: LSR, ROR, ROL, ASR, SWAP
ldi  r30, 0xA5
lsr  r30
ror  r30
rol  r30              ; (Alias for ADC r30, r30)
asr  r30
swap r30

; -- SREG manip (Full Coverage)
; -- SET
sec                   ; Set C
sez                   ; Set Z
sen                   ; Set N
sev                   ; Set V
ses                   ; Set S
seh                   ; Set H
set                   ; Set T
sei                   ; Set I

; -- CLEAR
clc                   ; Clear C
clz                   ; Clear Z
cln                   ; Clear N
clv                   ; Clear V
cls                   ; Clear S
clh                   ; Clear H
clt                   ; Clear T
cli                   ; Clear I

; -- MCU control: NOP, SLEEP, WDR
nop
sleep
wdr

; -- Infinite loop
forever:
rjmp forever
