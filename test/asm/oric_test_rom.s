; oric_test_rom.s — a 16 KiB ROM of this project's own for the socket at
; #C000 (design.md §13.3). CI has no Oric ROM (§10.2), so this is what
; proves the machine's wiring on every push: test_test_rom runs it and
; reads back what it leaves in RAM. It uses none of the Oric's code.
;
;   ca65 -o oric_test_rom.o oric_test_rom.s
;   ld65 -C oric_test_rom.cfg -o oric_test_rom.bin oric_test_rom.o
;
; What it leaves, all in page zero except the screen:
;   #02       #A5 once everything below is in place (done)
;   #03       RAM size: 16 or 48, by whether #4400 aliases #0400
;   #04-#11   AY registers 0-13 read back through the VIA
;   #12       AY port A read back
;   #18-#1F   the keyboard: a byte per row, a bit per column held down
;   #20/#21   T1 interrupts, counted by the IRQ handler
;   #22       NMIs, counted by the NMI handler
;   #BB80-    text rows with serial attributes; #B400- a font; #A000-
;             #AF9F 100 lines of a hires pattern, clear of the font and
;             the text screen, which the bitmap's last lines overlap
;             (§2.2). M4 draws them.

VIA_ORB   = $0300
VIA_ORA   = $0301
VIA_DDRB  = $0302
VIA_DDRA  = $0303
VIA_T1CL  = $0304
VIA_T1CH  = $0305
VIA_T1LL  = $0306
VIA_T1LH  = $0307
VIA_ACR   = $030B
VIA_PCR   = $030C
VIA_IFR   = $030D
VIA_IER   = $030E
VIA_ORANH = $030F

; PCR values as both BASIC ROMs write them: CA2 = BC1, CB2 = BDIR, held
; at levels (§2.3); CA1 and CB1 on rising edges.
PCR_IDLE  = $CC     ; BDIR 0 BC1 0
PCR_LATCH = $EE     ; BDIR 1 BC1 1
PCR_WRITE = $EC     ; BDIR 1 BC1 0
PCR_READ  = $CE     ; BDIR 0 BC1 1

DONE      = $02
RAMSIZE   = $03
AYBACK    = $04
PORTA     = $12
KEYS      = $18
IRQS      = $20
NMIS      = $22
COLMASK   = $24
COL       = $25
PTR       = $26     ; and $27

T1_LATCH  = 9998    ; period latch + 2 = 10,000 cycles (via6522.h)

        .segment "CODE"

reset:  sei
        cld
        ldx     #$FF
        txs
        lda     #0
        ldx     #0
@zero:  sta     $00,x               ; page zero clean, whatever RAM held
        inx
        bne     @zero

        ; The VIA as the ROMs set it up (#F960 in 1.0, #F9AA in 1.1).
        lda     #$FF
        sta     VIA_DDRA
        lda     #$F7                ; PB3 the keyboard sense, input
        sta     VIA_DDRB
        lda     #$B7
        sta     VIA_ORB
        lda     #$DD
        sta     VIA_PCR
        lda     #$7F
        sta     VIA_IER
        lda     #0
        sta     VIA_ACR

        ; ---- RAM size: does #4400 alias #0400? -----------------------
        lda     #$55
        sta     $0400
        lda     #$AA
        sta     $4400
        ldx     #48
        lda     $0400
        cmp     #$AA
        bne     @big
        ldx     #16
@big:   stx     RAMSIZE

        ; ---- AY: write 0-13, then read them back ----------------------
        ldx     #0
@wr:    stx     COL                 ; the register
        lda     ayvals,x
        tax                         ; X = the value
        lda     COL                 ; A = the register
        jsr     ay_write_reg
        ldx     COL
        inx
        cpx     #14
        bne     @wr

        ldx     #0
@rd:    jsr     ay_read
        sta     AYBACK,x
        inx
        cpx     #14
        bne     @rd

        ldx     #$40                ; mixer: port A an output
        lda     #7
        jsr     ay_write_reg
        ldx     #$5A
        lda     #14
        jsr     ay_write_reg
        ldx     #14
        jsr     ay_read
        sta     PORTA

        ; ---- the keyboard: every row in every column ------------------
        lda     #$7F                ; one zero bit, from column 7 down
        sta     COLMASK
        lda     #7
        sta     COL
@col:   ldx     COLMASK
        lda     #14
        jsr     ay_write_reg
        ldy     #0
@row:   tya
        ora     #$B8
        sta     VIA_ORB
        lda     VIA_ORB
        and     #$08
        beq     @up
        ldx     COL
        lda     KEYS,y
        ora     bits,x
        sta     KEYS,y
@up:    iny
        cpy     #8
        bne     @row
        sec
        ror     COLMASK
        dec     COL
        bpl     @col

        ; ---- the screen ------------------------------------------------
        jsr     screen

        ; ---- T1: free-running, 10,000 cycles, PB7 toggling -----------
        lda     #$C0                ; free-run, and PB7 from T1
        sta     VIA_ACR
        lda     #<T1_LATCH
        sta     VIA_T1LL
        sta     VIA_T1CL
        lda     #>T1_LATCH
        sta     VIA_T1LH
        sta     VIA_T1CH
        lda     #$C0                ; enable T1
        sta     VIA_IER
        cli

        lda     #$A5
        sta     DONE
idle:   jmp     idle

; ---- AY access, as the ROMs do it (#F590 in 1.1) ----------------------

; A = register, X = value. X and Y are kept.
ay_write_reg:
        php
        sei
        sta     VIA_ORANH
        lda     #PCR_LATCH
        sta     VIA_PCR
        lda     #PCR_IDLE
        sta     VIA_PCR
        stx     VIA_ORANH
        lda     #PCR_WRITE
        sta     VIA_PCR
        lda     #PCR_IDLE
        sta     VIA_PCR
        plp
        rts

; X = register; returns its value in A. X is kept.
ay_read:
        php
        sei
        stx     VIA_ORANH
        lda     #PCR_LATCH
        sta     VIA_PCR
        lda     #PCR_IDLE
        sta     VIA_PCR
        lda     #0
        sta     VIA_DDRA
        lda     #PCR_READ
        sta     VIA_PCR
        lda     VIA_ORA
        pha
        lda     #PCR_IDLE
        sta     VIA_PCR
        lda     #$FF
        sta     VIA_DDRA
        pla
        plp
        rts

; ---- the screen -------------------------------------------------------

; A font at #B400: glyph c, row r is (c + r * 3) & #3F, so every byte
; says where it came from. Then text rows at #BB80 and a hires pattern
; at #A000. The ROM's own screen layout is not copied.
screen:
        lda     #$00
        sta     PTR
        lda     #$B4
        sta     PTR+1
        ldx     #0                  ; glyph
@glyph: ldy     #0
@grow:  tya
        sta     COL
        asl     a
        adc     COL                 ; r * 3
        stx     COL
        adc     COL                 ; + c
        and     #$3F
        sta     (PTR),y
        iny
        cpy     #8
        bne     @grow
        lda     PTR
        clc
        adc     #8
        sta     PTR
        bcc     @gnext
        inc     PTR+1
@gnext: inx
        cpx     #128
        bne     @glyph

        ldx     #0
@text:  lda     text,x
        beq     @hires
        sta     $BB80,x
        inx
        bne     @text

@hires: lda     #$00
        sta     PTR
        lda     #$A0
        sta     PTR+1
        ldx     #0                  ; 100 lines of 40 bytes
@hline: ldy     #39
@hbyte: tya
        eor     hpat,x              ; never a serial attribute: bit 6 set
        and     #$3F
        ora     #$40
        sta     (PTR),y
        dey
        bpl     @hbyte
        lda     PTR
        clc
        adc     #40
        sta     PTR
        bcc     @hnext
        inc     PTR+1
@hnext: inx
        cpx     #100
        bne     @hline
        rts

; ---- interrupts -------------------------------------------------------

irq:    pha
        lda     VIA_T1CL            ; acknowledges T1
        inc     IRQS
        bne     @out
        inc     IRQS+1
@out:   pla
        rti

nmi:    inc     NMIS
        rti

; ---- data -------------------------------------------------------------

; Register values that every bit of every register's mask can show.
ayvals: .byte   $A5, $0F, $5A, $0A, $3C, $05, $1F, $3F
        .byte   $10, $0F, $1A, $C3, $3C, $0E

bits:   .byte   $01, $02, $04, $08, $10, $20, $40, $80

; Row 0: ink red (#01), paper blue (#14), text. Row 1: the alternate
; set with double height and blink (#0F), text. Row 2: inverse text.
text:   .byte   $01, $14, "ORIC TEST ROM"
        .res    40 - 15, ' '
        .byte   $0F, "DOUBLE BLINK ALT"
        .res    40 - 17, ' '
        .byte   $07, $10, $C9, $CE, $D6, $C5, $D2, $D3, $C5
        .byte   0

hpat:   .repeat 100, I
        .byte   (I * 7) .MOD 64
        .endrepeat

        .segment "VECTORS"
        .word   nmi, reset, irq
