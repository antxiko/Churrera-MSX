// SG-1000 MK1 v0.4
// Copyleft Mojon Twins 2013, 2015, 2017, 2018

// memfill.c
//
// SDCC __sdcccall(1) (default in 4.x) passes:
//   1st 16-bit arg (ptr)      -> HL
//   2nd 8-bit arg  (value)    -> stack[+2]   (after retaddr)
//   3rd 16-bit arg (length)   -> stack[+3..+4]
//
// The original asm here was written for the legacy convention
// (everything on stack) and read garbage from beyond the stack frame,
// resulting in BC = random length and an LDIR iterating thousands of
// random bytes per call. With memfill called many times per frame
// (clear_update_list, etc.), the game ran ~10x slower than it should.

#pragma save
#pragma disable_warning 85
void memfill (void *ptr, unsigned char value, unsigned int length) {
	if (length) {
		__asm
			; HL already = ptr (from sdcccall(1) calling convention).
			; Read value and length from stack (after the 2-byte retaddr).
			ld		c, l			; save ptr LSB
			ld		b, h			; save ptr MSB
			ld		hl, #2
			add		hl, sp			; HL = SP + 2
			ld		a, (hl)			; A = value
			inc		hl
			ld		e, (hl)			; E = length LSB
			inc		hl
			ld		d, (hl)			; D = length MSB
			ld		l, c			; restore ptr in HL
			ld		h, b
			; Standard fill-with-LDIR pattern:
			;   *HL = value; copy HL -> HL+1, count = length-1.
			ld		(hl), a
			ld		c, e
			ld		b, d
			dec		bc				; one byte already written
			ld		a, b
			or		a, c
			ret		z				; length was 1: done
			ld		e, l
			ld		d, h
			inc		de				; DE = HL + 1
			ldir
		__endasm;
	}
}
#pragma restore
