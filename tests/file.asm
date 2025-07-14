; all_instructions.asm
; Demo program exercising all AVR-VM instructions
.global main

; I/O Register Definitions - must be valid I/O addresses (0x00-0x3F)
.equ DDRB,  0x04
.equ PORTB, 0x05
.equ PINB,  0x03

main:
    ; -- Data Transfer: LDI, MOV, MOVW, LDS, STS
    ldi  r16, 0x5A         ; r16 = 0x5A
    ldi  r17, 0xA5         ; r17 = 0xA5
    mov  r18, r16          ; r18 = 0x5A
    movw r20, r16          ; r20 = r16, r21 = r17
    sts  0x200, r18        ; SRAM[0x200] = r18
    lds  r19, 0x200        ; r19 = SRAM[0x200]
    ldi  r22, 0xF0

    ; -- Arithmetic, Logical Immediate: ANDI, ORI, SER
    andi r22, 0x0F         ; r22 = r22 & 0x0F
    ori  r22, 0xC0         ; r22 = r22 | 0xC0
    ser  r23               ; r23 = 0xFF

    ; -- Arithmetic, Logic with registers: ADD, ADC, SUB, SBC, AND, OR, EOR
    ldi  r24, 0x3C
    ldi  r25, 0x0C
    add  r24, r25
    adc  r24, r25
    sub  r24, r25
    sbc  r24, r25
    and  r24, r25
    or   r24, r25
    eor  r24, r25

    ; -- INC/DEC/NEG/COM
    inc  r25
    dec  r25
    neg  r25
    com  r25

    ; -- Control flow: RJMP, JMP, RCALL, CALL, RET, RETI, BRBC, BRBS, CP, CPC, TST, CPI, CPSE
    ldi  r26, 1
    ldi  r27, 1
    cp   r26, r27          ; set Z
    cpc  r26, r27          ; with carry
    tst  r26
    cpi  r26, 1
    cpse r26, r27          ; not equal: no skip
    brbs 1, brbs_taken     ; Z flag is bit 1 in SREG
    nop
brbs_taken:
    brbc 1, brbc_not_taken ; Z flag is bit 1 in SREG
    nop
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
    ldi  r16, 0x00
    out  DDRB, r16         ; DDRB = 0
    sbi  DDRB, 3           ; set bit 3 of DDRB
    cbi  DDRB, 3           ; clear bit 3 of DDRB
    bst  r17, 0            ; T = bit 0 of r17
    bld  r18, 0            ; bit 0 of r18 = T
    sbrc r18, 0            ; skip if bit 0 clear
    nop
    sbrs r18, 1            ; skip if bit 1 set (not set)
    nop

    ; -- Stack: PUSH/POP
    push r19
    pop  r20

    ; -- IO: IN/OUT
    in   r21, PINB         ; r21 = PINB
    out  PORTB, r21        ; write to PORTB

    ; -- Shift/rotate: LSR, ROR, ROL, ASR, SWAP
    ldi  r30, 0xA5
    lsr  r30
    ror  r30
    rol  r30
    asr  r30
    swap r30

    ; -- SREG manip: SEI, CLI, CLC, CLZ, CLN, CLV, CLS, CLT, CLH
    sei
    cli
    clc
    clz
    cln
    clv
    cls
    clt
    clh

    ; -- MCU control: NOP, SLEEP, WDR
    nop
    sleep
    wdr

    ; -- Infinite loop for completeness
forever:
    rjmp forever
