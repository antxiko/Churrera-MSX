/* **************************************************
   MSXlib - C programming library for MSX1
   ( port of SGlib from devkitSMS to MSX )
   based on: na_th_an, sverx
   ************************************************** */

// Configure add-ons by The Mojon Twins:
#define AUTOCYCLE_SPRITES				// Sprites cycle automaticly
#define AUTOCYCLE_PRIME			3		// Prime to 32.
#define AUTOCYCLE_INIT_PRIME 	3		// Prime to 32.
#define AUTODETECT_ONE_COLOUR			// Detect 1 colour sprites in MSX_addMetaSprite1x1
#define AUTOMUSIC						// ISR calls PSGPlay and PSGSFXPlay
#define ONLY_ONE_CONTROLLER

#include "MSXlib.h"

#ifdef AUTOMUSIC
	#include "PSGlib.h"
#endif

#define true 1
#define false 0
#define bool _Bool
#define __bool_true_false_are_defined 1

#define MAXSPRITES 				32

#define DISABLE_INTERRUPTS		__asm di __endasm
#define ENABLE_INTERRUPTS		__asm ei __endasm

#define WAIT_VRAM				__asm 	nop \
										nop \
										nop __endasm

/*
    MSX1 VRAM layout (Mode 2 / Graphics II - identical to SG-1000):

        $0000   +--------+
                |   PG   |  ($1800 bytes, pattern generator table)
        $1800   +--------+
                |   PN   |  ($0300 bytes, nametable)
        $1B00   +--------+
                |   SA   |  ($0080 bytes, sprite attribute table)
        $1B80   +--------+
                |        |  ($0480 bytes free)
        $2000   +--------+
                |   CT   |  ($1800 bytes, colour table)
        $3800   +--------+
                |   SG   |  ($0800 bytes, sprite generator table)
                +--------+
*/

// VDP register init values - identical to SG-1000 (TMS9918 mode 2)
const unsigned char VDPReg_init [8] = {
	0x02,	// Mode2
	0xa0,	// 16KB, screen off, VBlank IRQ, sprite 8x8, no zoom
	0x06,	// PN bits 13-10 = 0 1 1 0			(address = $1800)
	0xff,	// CT bits 13-7	= 1 x x x x x x x	(address = $2000)
	0x03,	// PG bits 13-11 = 0 x x			(address = $0000)
	0x36,	// SA bits 13-7	= 0 1 1 0 1 1 0		(address = $1B00)
	0x07,	// SG bits 13-11 = 1 1 1			(address = $3800)
	0x01	// text color (unused in Mode2) / backdrop (black)
};

// VDP registers #0 and #1 'shadow' RAM
unsigned char	VDPReg [2] = {0x02, 0xa0};

volatile bool	VDPBlank;				// used by INTerrupt
volatile bool 	PauseRequested; 		// set when F5 is pressed (replaces SG NMI)

#ifdef ONLY_ONE_CONTROLLER
	volatile unsigned char KeysStatus;
#else
	volatile unsigned int KeysStatus;
#endif

unsigned char	SpriteTable [MAXSPRITES * 4];
unsigned char   *stp;					// Pointer to spritetable

#ifdef AUTOCYCLE_SPRITES
	unsigned char   *SpriteTableEnd;	// Pointer to the end of spritetable
	unsigned char   first_sprite;		// First sprite slot for this loop
#endif

unsigned char   libgpit;
unsigned char   VDPType;

// Update list support
unsigned char   *updateList;
unsigned char   *ulp;
unsigned char   ulpMsb;

#ifdef AUTOMUSIC
	unsigned char music_on;
#endif

// VDP register / address helpers
#define MSX_write_to_VDPRegister(VDPReg,value)	{ DISABLE_INTERRUPTS; VDPControlPort = (value); VDPControlPort = (VDPReg) | 0x80; ENABLE_INTERRUPTS; }
#define MSX_set_address_VRAM(address)			{ DISABLE_INTERRUPTS; VDPControlPort = LO (address); VDPControlPort = HI (address) | 0x40; ENABLE_INTERRUPTS; }

inline void MSX_byte_to_VDP_data (unsigned char data) {
	VDPDataPort = data;
}

inline void MSX_byte_array_to_VDP_data (const unsigned char *data, unsigned int size) {
	do {
		VDPDataPort = *(data ++);
	} while (-- size);
}

inline void MSX_byte_brief_array_to_VDP_data (const unsigned char *data, unsigned char size) {
	do {
		VDPDataPort = *(data ++);
	} while (-- size);
}

inline void MSX_word_to_VDP_data (unsigned int data) {
	VDPDataPort = LO (data);
	WAIT_VRAM;
	VDPDataPort = HI (data);
}

void MSX_setReg (unsigned char reg, unsigned char v) {
	VDPReg [reg] = v;
	MSX_write_to_VDPRegister (reg, v);
}

void MSX_VDPturnOnFeature (unsigned int feature) {
	VDPReg [HI (feature)] |= LO (feature);
	MSX_write_to_VDPRegister (HI (feature), VDPReg [HI (feature)]);
}

void MSX_VDPturnOffFeature (unsigned int feature) {
	VDPReg [HI (feature)] &= ~LO (feature);
	MSX_write_to_VDPRegister (HI (feature), VDPReg [HI (feature)]);
}

void MSX_init (void) {
	for (libgpit = 0; libgpit < 8; libgpit++)
		MSX_write_to_VDPRegister (libgpit, VDPReg_init [libgpit]);

	// AY R7 must have bit 6 = 0 (port A input) on MSX or the joystick port
	// is shorted. Force a safe value: portB out, portA in, all chans off.
	PSGAddrPort = 7; PSGDataPort = 0xBF;
	// Channel volumes to 0 (silence).
	PSGAddrPort = 8;  PSGDataPort = 0;
	PSGAddrPort = 9;  PSGDataPort = 0;
	PSGAddrPort = 10; PSGDataPort = 0;

	#ifdef AUTOCYCLE_SPRITES
		first_sprite = 0;
		SpriteTableEnd = SpriteTable + 128;
	#endif

	MSX_initSprites ();
	MSX_finalizeSprites ();
	MSX_copySpritestoSAT ();

	#ifdef AUTOMUSIC
		music_on = 1;
	#endif
}

void MSX_setBackdropColor (unsigned char entry) {
	MSX_write_to_VDPRegister (0x07, entry & 0x0f);
}

void MSX_setSpriteMode (unsigned char mode) {
	if (mode & MSX_SPRITEMODE_LARGE) {
		MSX_VDPturnOnFeature (MSX_VDPFEATURE_USELARGESPRITES);
	} else {
		MSX_VDPturnOffFeature (MSX_VDPFEATURE_USELARGESPRITES);
	}
	if (mode & MSX_SPRITEMODE_ZOOMED) {
		MSX_VDPturnOnFeature (MSX_VDPFEATURE_ZOOMSPRITES);
	} else {
		MSX_VDPturnOffFeature (MSX_VDPFEATURE_ZOOMSPRITES);
	}
}


void MSX_loadTilePatterns (void *src, unsigned int tilefrom, unsigned int size) {
	MSX_set_address_VRAM (PGTADDRESS + (tilefrom << 3));
	MSX_byte_array_to_VDP_data (src, size);
}

void MSX_loadTileColours (void *src, unsigned int tilefrom, unsigned int size) {
	MSX_set_address_VRAM (CGTADDRESS + (tilefrom << 3));
	MSX_byte_array_to_VDP_data (src, size);
}

void MSX_loadSpritePatterns (void *src, unsigned int tilefrom, unsigned int size) {
	MSX_set_address_VRAM (SGTADDRESS + (tilefrom << 3));
	MSX_byte_array_to_VDP_data (src, size);
}

void MSX_setTileatXY (unsigned char x, unsigned char y, unsigned char tile) {
	MSX_set_address_VRAM (PNTADDRESS + (y << 5) + x);
	MSX_byte_to_VDP_data (tile);
}

void MSX_setNextTileatXY (unsigned char x, unsigned char y) {
	MSX_set_address_VRAM (PNTADDRESS + (y << 5) + x);
}

void MSX_setTile (unsigned char tile) {
	MSX_byte_to_VDP_data (tile);
}

void MSX_fillTile (unsigned char tile, unsigned int count) {
	while (count --) MSX_byte_to_VDP_data (tile);
}

void MSX_loadTileMapArea (unsigned char x, unsigned char y, void *src, unsigned char width, unsigned char height) {
	for (libgpit = y; libgpit < (y + height); libgpit ++) {
		MSX_set_address_VRAM (PNTADDRESS+ (libgpit << 5) + x);
		MSX_byte_brief_array_to_VDP_data (src, width);
		src = (unsigned char *) src + width;
	}
}

#ifdef AUTOCYCLE_SPRITES
	void MSX_initSprites (void) {
		__asm
			ld hl, #_SpriteTable
			ld de, #_SpriteTable
			ld (hl), #0xc0
			inc de
			ld bc, #128
			ldir
		__endasm;
		stp = SpriteTable + (first_sprite << 2);
		first_sprite = (first_sprite + AUTOCYCLE_INIT_PRIME) & 31;
	}

	inline void nextSprite (void) {
		stp += 4 * (AUTOCYCLE_PRIME-1); if (stp >= SpriteTableEnd) stp -= 128;
	}

	void MSX_addSprite (unsigned char x, unsigned char y, unsigned char tile, unsigned char attr) {
		*stp ++ = y;
		*stp ++ = x;
		*stp ++ = tile;
		*stp ++ = attr;
		nextSprite ();
	}

	void MSX_addMetaSprite1x1 (unsigned char x, unsigned char y, const unsigned char *mt) {
		mt += 2;
		*stp ++ = y;
		*stp ++ = x;
		*stp ++ = *mt ++;
		*stp ++ = *mt ++;
		nextSprite ();
		#ifdef AUTODETECT_ONE_COLOUR
			if (*mt == 0x80) return;
		#endif
		mt += 2;
		*stp ++ = y;
		*stp ++ = x;
		*stp ++ = *mt ++;
		*stp ++ = *mt ++;
		nextSprite ();
	}

	void MSX_addMetaSprite (unsigned char x, unsigned char y, const unsigned char *mt) {
		while (0x80 != (libgpit = *mt ++)) {
			*stp ++ = y + libgpit;
			*stp ++ = x + *mt++;
			*stp ++ = *mt ++;
			*stp ++ = *mt ++;
			nextSprite ();
		}
	}

	void MSX_finalizeSprites (void) {
		// NOP
	}

	unsigned char *MSX_getStp (void) {
		return stp;
	}

	void MSX_setStp (unsigned char *s) {
		stp = s;
	}
#else
	void MSX_initSprites (void) {
		stp = SpriteTable;
	}

	void MSX_addSprite (unsigned char x, unsigned char y, unsigned char tile, unsigned char attr) {
		*stp ++ = y;
		*stp ++ = x;
		*stp ++ = tile;
		*stp ++ = attr;
	}

	void MSX_addMetaSprite1x1 (unsigned char x, unsigned char y, const unsigned char *mt) {
		mt += 2;
		*stp ++ = y;
		*stp ++ = x;
		*stp ++ = *mt ++;
		*stp ++ = *mt ++;
		mt += 2;
		*stp ++ = y;
		*stp ++ = x;
		*stp ++ = *mt ++;
		*stp ++ = *mt ++;
	}

	void MSX_addMetaSprite (unsigned char x, unsigned char y, const unsigned char *mt) {
		while (0x80 != (libgpit = *mt ++)) {
			*stp ++ = y + libgpit;
			*stp ++ = x + *mt++;
			*stp ++ = *mt ++;
			*stp ++ = *mt ++;
		}
	}

	void MSX_finalizeSprites (void) {
		*stp = 0xd0;
	}

	unsigned char *MSX_getStp (void) {
		return stp;
	}

	void MSX_setStp (unsigned char *s) {
		stp = s;
	}
#endif

void MSX_waitForVBlank (void) {
	VDPBlank = false;
	while (!VDPBlank);
}

#ifdef ONLY_ONE_CONTROLLER
	unsigned char MSX_getKeysStatus (void) {
		return (KeysStatus);
	}
#else
	unsigned int MSX_getKeysStatus (void) {
		return (KeysStatus);
	}
#endif

_Bool MSX_queryPauseRequested (void) {
	return (PauseRequested);
}

void MSX_resetPauseRequest (void) {
	PauseRequested = false;
}

void MSX_VRAMmemset (unsigned int dst, unsigned char value, unsigned int size) {
	MSX_set_address_VRAM (dst);
	while (size>0) {
		MSX_byte_to_VDP_data (value);
		size--;
	}
}

#pragma save
#pragma disable_warning 85

// Call those during vBlank!

// __naked: keep the inline asm verbatim. With normal SDCC the peep
// optimizer will see "ld c,...; ld hl,...; jp ..." as dead loads and
// strip them, breaking the OUTI block call.
void MSX_copySpritestoSAT (void) __naked {
	__asm
		di
		xor a, a
		out (#0x99), a		; SAT addr low ($1B00)
		ld a, #0x5B
		out (#0x99), a		; SAT addr high | $40 (write mode)
		ei
		ld c, #0x98			; VDP data port
		ld hl, #_SpriteTable
		jp _outi_block-32*4*2	; tail-call: 128 OUTI then ret
	__endasm;
}

void MSX_VRAMmemcpy128 (unsigned int dst, void *src) {
	MSX_set_address_VRAM (dst);
	__asm
		ld c,#_VDPDataPort
		ld l, 2 (iy)
		ld h, 3 (iy)
		call _outi_block-128*2
	__endasm;
}

// Update list functions
void MSX_setUpdateList (unsigned char *ul) {
	updateList = ul;
}

// Hand-rolled to keep `ulp` in HL during the whole loop (the C version
// has SDCC reload `ulp` from memory 6 times per iteration -> 4x slower).
void MSX_doUpdateList (void) __naked {
	__asm
		ld		hl, (_updateList)
1$:
		ld		a, (hl)
		inc		hl
		cp		#0xff
		ret		z
		ld		d, a			; D = ulpMsb
		ld		a, (hl)			; ulpLsb (low addr byte)
		inc		hl
		out		(#0x99), a		; VDP ctrl: low addr
		ld		a, d
		or		a, #0x40		; high | 0x40 (write mode)
		out		(#0x99), a		; VDP ctrl: high addr
		ld		a, (hl)			; data byte
		inc		hl
		out		(#0x98), a		; VDP data
		jr		1$
	__endasm;
}

#pragma restore

// ====================================================================
// Joystick reading - MSX style
// On MSX, joystick lines are read through PSG register 14 (0x0E),
// while bit 6 of PSG register 15 (0x0F) selects which port (0=A, 1=B).
// Bit layout in register 14 (active LOW):
//   bit 0: UP, bit 1: DOWN, bit 2: LEFT, bit 3: RIGHT
//   bit 4: TRG_A (button 1), bit 5: TRG_B (button 2)
// We translate to the same bitfield used by SGlib so the engine
// reads it identically.
// ====================================================================

inline unsigned char MSX_readJoystick (unsigned char port) {
	unsigned char raw;
	// Select port via PSG register 15, bit 6 (0=port A, 1=port B)
	PSGAddrPort = 15;
	raw = PSGReadPort;
	PSGAddrPort = 15;
	PSGDataPort = (raw & 0xBF) | (port ? 0x40 : 0x00);
	// Read joystick lines from PSG register 14
	PSGAddrPort = 14;
	raw = PSGReadPort;
	// Active LOW; lower 6 bits are joystick lines, mask & invert
	return (~raw) & 0x3f;
}

// ====================================================================
// Keyboard scan for F5 (used as Pause replacement for SG NMI button)
// MSX keyboard matrix: row select via PPI port C low nibble ($AA),
// row data read via PPI port B ($A9). F5 is at row 7 bit 5.
// ====================================================================

inline _Bool MSX_isF5Pressed (void) {
	unsigned char old_c, row;
	old_c = PPIPortC;
	PPIPortC = (old_c & 0xF0) | 7;	// select row 7
	row = PPIPortB;
	PPIPortC = old_c;
	return (row & 0x20) == 0;		// bit 5 active LOW
}

// ====================================================================
// Interrupt Service Routine
// MSX VBlank IRQ vectors at $0038 (mode 1). The crt0 jumps here.
// We acknowledge by reading the VDP status port, then update keys,
// pump music, and check F5 for the pause request.
// ====================================================================

// __naked __interrupt: SDCC's regular __interrupt prologue emits an
// `ei` BEFORE pushing registers, which lets a second VBlank reenter
// before the state is saved -> stack blows up. Hand-rolled prologue
// avoids that.
void MSX_isr (void) __naked __interrupt {
__asm
	push	af
	push	bc
	push	de
	push	hl
	push	iy
	; In MSX1 the only IRQ source is the VDP VBlank, so we just ack and
	; set the flag unconditionally. The `rlca; jr nc` test we used before
	; sometimes saw bit 7 = 0 (still investigating why) and dropped frames.
	in		a, (#0x99)		; ack VDP (clears IRQ flag regardless of bit 7)
	ld		hl, #_VDPBlank
	ld		(hl), #1
	; Joystick port A via AY reg 14 (sel via reg 15 b6=0)
	ld		a, #15
	out		(#0xa0), a
	in		a, (#0xa2)
	and		a, #0xbf
	out		(#0xa1), a
	ld		a, #14
	out		(#0xa0), a
	in		a, (#0xa2)
	cpl
	and		a, #0x3f
	ld		c, a			; C = joystick bits
	; Keyboard row 8: cursors + SPACE
	in		a, (#0xaa)
	ld		b, a
	and		a, #0xf0
	or		a, #8
	out		(#0xaa), a
	in		a, (#0xa9)		; row 8 raw, active LOW
	ld		e, a
	; Row 4: M
	ld		a, b
	and		a, #0xf0
	or		a, #4
	out		(#0xaa), a
	in		a, (#0xa9)
	ld		d, a
	; Row 7: F5
	ld		a, b
	and		a, #0xf0
	or		a, #7
	out		(#0xaa), a
	in		a, (#0xa9)
	push	af
	; Restore PPI C
	ld		a, b
	out		(#0xaa), a
	pop		af
	; F5 detect
	bit		5, a
	jr		nz, _no_f5
	ld		hl, #_PauseRequested
	ld		(hl), #1
_no_f5:
	; Build keyboard bitfield in B (engine layout)
	ld		b, #0
	bit		5, e
	jr		nz, _no_up
	set		0, b
_no_up:
	bit		6, e
	jr		nz, _no_down
	set		1, b
_no_down:
	bit		4, e
	jr		nz, _no_left
	set		2, b
_no_left:
	bit		7, e
	jr		nz, _no_right
	set		3, b
_no_right:
	bit		0, e
	jr		nz, _no_space
	set		4, b
_no_space:
	bit		2, d
	jr		nz, _no_m
	set		5, b
_no_m:
	; Combine: KeysStatus = joystick (C) | keyboard (B)
	ld		a, c
	or		a, b
	ld		(#_KeysStatus), a

	; --- Tick the PSG/SFX players (AUTOMUSIC) ---
	; PSGSFXFrame first so SFX can preempt music on shared channels.
	call	_PSGSFXFrame
	ld		a, (#_music_on)
	or		a, a
	jr		z, _no_music
	call	_PSGFrame
_no_music:

	pop		iy
	pop		hl
	pop		de
	pop		bc
	pop		af
	ei
	reti
__endasm;
}

// MSX has no NMI button. Kept for API parity (never invoked).
void MSX_nmi_isr (void) __critical __interrupt {
	// no-op
}

#ifdef AUTOMUSIC
	void music_pause (unsigned char p) {
		if (p) PSGSilence ();
		music_on = !p;
	}
#endif
