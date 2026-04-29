;--------------------------------------------------------------------------
;  crt0_msx.s - bare-metal crt0 for MSX1, 64 KB plain ROM
;
;  Pattern follows MSXgl's crt0_rom48_isr.asm + ROM_64K_ISR target:
;    - vectors in _HEADER (ABS) at $0000
;    - ISR vector in _DRIVER (ABS) at $0038
;    - "AB" cart header inlined at the very start of _CODE
;      (linker invoked with --code-loc 0x4000, header lands at $4000)
;    - SP loaded from BIOS HIMEM ($FC4A), not hardcoded
;    - Slot switch maps cart into pg 0+1+2 and preserves pg 3 (RAM).
;      Cart's pg 3 ($C000-$FFFF in the binary) is left as 0xFF padding;
;      it is never accessible at runtime because pg 3 stays mapped to
;      RAM. Total binary size = 65536 B exact.
;
;  Image layout (64 KB ROM, only first 48 KB mapped at runtime):
;    $0000-$3FFF  ROM page 0 (vectors + ISR + assets in _DATA_BANK)
;    $4000-$BFFF  ROM page 1+2 (cart header + _CODE + _GSINIT etc.)
;    $C000-$FFFF  RAM (provided by another slot, kept by BIOS at boot)
;    [ROM bytes $C000-$FFFF in the file: padding, unreachable at runtime]
;
;  Boot:
;    1. BIOS finds "AB" at $4000 of our cart, calls INIT (in pg 1).
;    2. We replicate cart slot into pg 0 + pg 2 via $A8.
;    3. SP <- (HIMEM). BIOS workspace stays untouched.
;    4. gsinit (SDCC ABI), MSX_init, EI, main.
;--------------------------------------------------------------------------

.module 	crt0
.globl		_main
.globl		_MSX_init
.globl		_MSX_isr

;--------------------------------------------------------------------------
; Page 0 vectors. Visible only after slot switch in INIT.
;--------------------------------------------------------------------------
.area	_HEADER (ABS)

.org	0x0000
		di
		jp		crt0_init		; safety only; BIOS uses $4002 path

.org	0x0008
		ret
.org	0x0010
		ret
.org	0x0018
		ret
.org	0x0020
		ret
.org	0x0028
		ret
.org	0x0030
		ret

;--------------------------------------------------------------------------
; ISR vector ($0038) - separate area to keep linker from packing it away.
;--------------------------------------------------------------------------
.area	_DRIVER (ABS)
.org	0x0038
		jp		_MSX_isr

;--------------------------------------------------------------------------
; NMI vector ($0066): unused on MSX.
;--------------------------------------------------------------------------
.area	_NMI (ABS)
.org	0x0066
		retn

;--------------------------------------------------------------------------
; Cartridge header + INIT, inlined at the very top of _CODE.
; The Makefile passes --code-loc 0x4000 so this lands at $4000.
;--------------------------------------------------------------------------
.area	_HOME
.area	_CODE

		; ROM header at $4000
		.db		#0x41, #0x42	; "AB" magic
		.dw		crt0_init		; INIT
		.dw		#0x0000			; STATEMENT
		.dw		#0x0000			; DEVICE
		.dw		#0x0000			; TEXT
		.db		#0,#0,#0,#0,#0,#0	; reserved (6 bytes), ends at $4010

;--------------------------------------------------------------------------
; INIT (BIOS jumps here from $4002).
;--------------------------------------------------------------------------
crt0_init::
		di

		; --- SP at $F380 (high RAM, far from BSS) ---
		; HIMEM may move during gameplay if BIOS has hooks; fixed value
		; matches the SG-1000 build's stack layout more faithfully.
		ld		sp, #0xF380

		; --- Slot switch (MSXgl-style, handles expanded sub-slots) ---
		; Replicates pg-1 cart slot into pg 0 + pg 2, preserves pg 3 (RAM).
		; Source: MSXgl INIT_P1_TO_P02 macro.
		in		a, (#0xA8)		; A=[P3|P2|P1|P0]
		ld		d, a			; backup
		and		a, #0x0C		; A=[00|00|P1|00]
		ld		c, a
		rrca
		rrca					; A=[00|00|00|P1]
		or		a, c			; A=[00|00|P1|P1]
		ld		c, a
		add		a, a
		add		a, a
		add		a, a
		add		a, a			; A=[P1|P1|00|00]
		or		a, c			; A=[P1|P1|P1|P1]
		out		(#0xA8), a		; map all pages to cart slot (temporary)
		ld		e, a

		; --- Sub-slot register: replicate P1 sub-slot to P0+P2 ---
		ld		a, (#0xFFFF)	; SLTSL is at $FFFF (read-only return ~SLTSL)
		cpl						; A=[S3|S2|S1|S0]
		ld		b, a
		and		a, #0x0C		; A=[00|00|S1|00]
		ld		c, a
		rrca
		rrca					; A=[00|00|00|S1]
		or		a, c			; A=[00|00|S1|S1]
		ld		c, a
		add		a, a
		add		a, a			; A=[00|S1|S1|00]
		or		a, c			; A=[00|S1|S1|S1]
		ld		c, a
		ld		a, b
		and		a, #0xC0		; A=[S3|00|00|00]
		or		a, c			; A=[S3|S1|S1|S1]
		ld		(#0xFFFF), a	; commit sub-slot register

		; --- Restore pg 3 to its original slot (RAM) ---
		ld		a, d			; original [P3|P2|P1|P0]
		and		a, #0xC0		; A=[P3|00|00|00]
		ld		c, a
		ld		a, e			; new [P1|P1|P1|P1]
		and		a, #0x3F		; A=[00|P1|P1|P1]
		or		a, c			; A=[P3|P1|P1|P1]
		out		(#0xA8), a		; final: cart in pg0+1+2, RAM in pg3

		; --- Zero-fill RAM ($C000-$F37F) ---
		; SDCC for z80 puts uninitialised globals in _DATA (not _BSS),
		; so a BSS-only clear leaves them as garbage. Clearing the whole
		; user-RAM range covers _DATA + _INITIALIZED + _BSS + _HEAP at
		; once. We stop at $F380 to keep BIOS workspace ($F380-$FFFF)
		; intact (it holds HIMEM, slot info, etc.).
		xor		a
		ld		hl, #0xC000
		ld		(hl), a
		ld		de, #0xC001
		ld		bc, #0x337F		; $C000..$F37E (initialiser will overwrite later)
		ldir

		; --- SDCC global init ---
		call	gsinit
		call	_MSX_init
		ei
		call	_main
		rst		0

;--------------------------------------------------------------------------
; OUTI block for fast batched VRAM transfers (used by MSXlib).
; 128 OUTI; the public label points to the END so callers do
; "call _outi_block-N*2" to do N OUTIs.
;--------------------------------------------------------------------------
.rept	128
		outi
.endm
_outi_block::
		ret

;--------------------------------------------------------------------------
; SDCC linker segment ordering
;--------------------------------------------------------------------------
	.area	_INITIALIZER
	.area	_GSINIT
	.area	_GSFINAL

	.area	_DATA
	.area	_INITIALIZED
	.area	_BSEG
	.area	_BSS
	.area	_HEAP

	.area	_CODE
__clock::
		ld		a, #2
		rst		0x08
		ret

_exit::
		di
1$:
		halt
		jr		1$

.area	_GSINIT
gsinit::
		; Zero-fill BSS (C standard: uninit globals must be 0).
		; Only the BSS range, not all RAM, so BIOS workspace stays alive.
		ld		bc, #l__BSS
		ld		a, b
		or		a, c
		jr		Z, gsinit_init
		ld		hl, #s__BSS
		ld		(hl), #0
		dec		bc
		ld		a, b
		or		a, c
		jr		Z, gsinit_init
		ld		d, h
		ld		e, l
		inc		de
		ldir
gsinit_init:
		; Copy INITIALIZER -> INITIALIZED (data with explicit init values).
		ld		bc, #l__INITIALIZER
		ld		a, b
		or		a, c
		jr		Z, gsinit_next
		ld		de, #s__INITIALIZED
		ld		hl, #s__INITIALIZER
		ldir
gsinit_next:

.area	_GSFINAL
		ret
