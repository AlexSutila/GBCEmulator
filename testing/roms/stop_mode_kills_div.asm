DEF rDIV  EQU $FF04
DEF rLCDC EQU $FF40
DEF rBCPS EQU $FF68
DEF rBCPD EQU $FF69
DEF rKEY1 EQU $FF4D

SECTION "Header", ROM0[$100]

    nop
    jp Start

    ds $134 - @, 0
    db "DORCE"
    ds $143 - @
    db $80  ; Need to denote CGB support

SECTION "Code", ROM0

Start:
    ld a, $00
    ldh [rDIV], a
    ld b, $30

.delay:
    dec b
    jr nz, .delay

    ld a, $01
    ldh [rKEY1], a
    stop

    ldh a, [rDIV]
    or a
    jr z, .pass

.fail:
    ld hl, PalFail
    jr .write

.pass:
    ld hl, PalPass

.write:
    ld a, %10000000
    ldh [rBCPS], a

    ld a, [hl+]
    ldh [rBCPD], a
    ld a, [hl+]
    ldh [rBCPD], a

    ld a, [hl+]
    ldh [rBCPD], a
    ld a, [hl+]
    ldh [rBCPD], a

    ld a, [hl+]
    ldh [rBCPD], a
    ld a, [hl+]
    ldh [rBCPD], a

    ld a, [hl+]
    ldh [rBCPD], a
    ld a, [hl+]
    ldh [rBCPD], a

    ld a, %10010001
    ldh [rLCDC], a

.loop:
    jr .loop

PalPass:
    dw $03E0, $03E0

PalFail:
    dw $001F, $03E0
