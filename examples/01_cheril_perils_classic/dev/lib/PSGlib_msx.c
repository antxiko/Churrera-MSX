/* **************************************************
   PSGlib_msx - PSGlib clone for the MSX AY-3-8910

   The .psg bytecode produced by the Mojon Twins / devkitSMS tooling
   targets the SN76489 (SG-1000 / SMS). Each "data byte" is a literal
   chip command:

     1cccTdddd        latch byte: ccc=channel, T=type (0=tone, 1=vol),
                      dddd=4 LSBs of value
     0xHHHHHH         tone-high  data byte: 6 MSBs of period for the
                      currently latched tone channel
     1ccc1_dddd       volume:    4-bit attenuation (0=loud..15=silent)
     1110_0_dddd      noise control byte (channel 3 latch with type=0)
     1111_1_dddd      channel 3 volume

   On MSX we have an AY-3-8910 instead. We mirror the SN state per
   channel and translate writes to AY register pairs:

     SN ch 0..2  ->  AY ch A..C (R0/R1, R2/R3, R4/R5 tone, R8/R9/R10 vol)
     SN ch 3     ->  AY noise (R6) + mixer noise on AY-C
     SN attenuation -> AY amplitude = 15 - attn (silence == AY 0).

   The byte-stream parser keeps the same shape as PSGlib.c (substrings,
   wait commands, end/loop), so .psg files play unchanged.
   ************************************************** */

#include "PSGlib.h"
#include "MSXlib.h"

#define PSG_STOPPED         0
#define PSG_PLAYING         1

// ---- AY register caches (one per channel) ----
// Mirrors for the AY register state. These MUST have explicit
// initialisers — the crt0 zero-fills RAM at boot, and "all zeros" is
// catastrophic for these variables:
//   * mixer = 0   -> R7=0   -> tone AND noise enabled on every channel,
//                              port A in output mode (unsafe for the
//                              joystick on real hardware).
//   * ay_vol[ch] = 0 -> SN attenuation 0 -> LOUDEST (we want SILENT).
//   * noise_vol  = 0 -> idem on the noise channel.
// Without initialisers, the music sounds like loud unkeyed noise on
// every channel until the first PSGStop/PSGPlay cycle re-syncs the
// mirrors (which, in Cheril, only happens once the player dies and
// the game-over music starts). With initialisers, the player hears
// silence at boot and the music plays clean from frame one.
static unsigned char ay_tone_lo[3];	// fine for 0 init
static unsigned char ay_tone_hi[3];	// fine for 0 init
static unsigned char ay_vol[3]      = { 15, 15, 15 };
static unsigned char noise_period   = 0;
static unsigned char noise_vol      = 15;
static unsigned char mixer          = 0xBF;	// portB out, portA in, all off

// last latch (per stream), to remember what the next data byte modifies
static unsigned char music_last_latch;
static unsigned char sfx_last_latch;

// SFX has its own tone mirror so its direct AY writes don't smash the
// music's ay_tone_lo/hi (which are used by restore_music_channel).
static unsigned char sfx_tone_lo[3];
static unsigned char sfx_tone_hi[3];

// ---- Stream state ----
static unsigned char music_status;
static unsigned char sfx_status;
static const unsigned char *music_ptr;
static const unsigned char *music_start;
static const unsigned char *music_loop_pt;
static unsigned char music_skip;
static unsigned char music_subst_len;
static const unsigned char *music_subst_ret;
static unsigned char music_loop_flag;

static unsigned char sfx_chan2_active;	// SFX has captured ch B
static unsigned char sfx_chan3_active;	// SFX has captured ch C/noise
static const unsigned char *sfx_ptr;
static const unsigned char *sfx_start;
static const unsigned char *sfx_loop_pt;
static unsigned char sfx_skip;
static unsigned char sfx_subst_len;
static const unsigned char *sfx_subst_ret;
static unsigned char sfx_loop_flag;

// ---- SN -> AY conversion tables ----
//
// Volume: SN76489 attenuation is linear in dB (~2 dB per step), while
// the AY-3-8910 amplitude DAC is logarithmic (~3 dB per step). A
// straight `15 - attn` produces correct endpoints but the intermediate
// levels do not match. The table below comes from the common SMS->MSX
// chiptune literature (TI SN PSG datasheet × AY-3-8910 DAC table) and
// approximates equal dB attenuation.
static const unsigned char sn_attn_to_ay_amp[16] = {
	15, 14, 13, 12, 11, 10,  9,  8,
	 7,  6,  5,  4,  3,  2,  1,  0
};

// Noise needs a separate (and much lower) curve. On SN76489 the noise
// is an independent fourth channel, so games crank it on top of the
// three tonal voices. AY-3-8910 mixes noise + ch C tone into the same
// amplitude register, so when both are sounding at the same time the
// listener hears noise+tone garbled together (the "instruments sound
// like FX" symptom). We cap the noise amplitude really low so the
// percussion sits well underneath the melody and the garbling is
// barely audible. Compromise: drums are quiet, melody stays clean.
static const unsigned char sn_attn_to_ay_noise_amp[16] = {
	 4,  3,  3,  2,  2,  1,  1,  1,
	 0,  0,  0,  0,  0,  0,  0,  0
};

// SN76489 noise channel: bits 1-0 of the latch select among 3 fixed
// shift-counter rates (or "use channel 3 frequency"), bit 2 selects
// white vs periodic. The AY noise generator on the other hand uses a
// 5-bit period directly (R6). Map the 4 SN selectors to AY periods
// that produce roughly the same perceived pitch ("snare-ish",
// "lowish hihat", "rumble") instead of the literal 0..7 values.
//   SN selector 0 (~1.7 kHz on SG)  -> AY R6  3   bright hi-hat
//   SN selector 1 (~880 Hz)         -> AY R6  6
//   SN selector 2 (~440 Hz)         -> AY R6 12
//   SN selector 3 (chained to ch3)  -> AY R6 16   slowish rumble
static const unsigned char sn_noise_to_ay_period[4] = { 3, 6, 12, 16 };

// ---- Low-level AY write helper ----
// IMPORTANT: this is called both from the ISR (via PSGFrame /
// PSGSFXFrame) and from the main thread (PSGPlay, PSGSFXPlay, etc).
// We can NOT bracket the OUT pair with DI/EI here, because the EI
// would re-enable interrupts in the middle of the ISR and let the
// next VBlank pre-empt -> instant stack overflow. Instead, the public
// main-thread entry points wrap their whole bodies in DI/EI; ISR
// callers are already in DI by virtue of being inside the IRQ.
inline void ay_write (unsigned char reg, unsigned char val) {
	PSGAddrPort = reg;
	PSGDataPort = val;
}

static void ay_init_safe (void) {
	mixer = 0xBF;	// portB out, portA in, all tones+noise OFF
	ay_write (7, mixer);
	for (unsigned char i = 0; i < 3; i++) {
		ay_tone_lo[i] = 0;
		ay_tone_hi[i] = 0;
		ay_vol[i] = 15;	// SN 15 = silent
		ay_write (8 + i, 0);	// AY 0 = silent
	}
	noise_period = 0;
	noise_vol = 15;
	ay_write (6, 0);
}

// Update AY tone period for channel `ch` (0..2) from cached SN values.
static void ay_update_tone (unsigned char ch) {
	// SN period = (hi << 4) | lo  but in SN encoding lo is the 4 LSBs and
	// hi is the 6 MSBs, so total = (hi6 << 4) | lo4, a 10-bit value.
	unsigned int period = ((unsigned int)(ay_tone_hi[ch] & 0x3F) << 4)
	                    | (ay_tone_lo[ch] & 0x0F);
	ay_write (ch * 2,     period & 0xFF);
	ay_write (ch * 2 + 1, (period >> 8) & 0x0F);
}

static void ay_update_vol (unsigned char ch) {
	unsigned char v = sn_attn_to_ay_amp[ay_vol[ch] & 0x0F];
	if (v) mixer &= ~(1 << ch); else mixer |= (1 << ch);
	ay_write (7, mixer);
	ay_write (8 + ch, v);
}

// Decode a single SN76489 byte and (a) optionally update the music
// state mirrors and (b) optionally write the resulting register pair
// to the AY-3-8910.
//
// Call patterns:
//   music, channel free       -> is_music=1, do_write=1   (typical)
//   music, ch blocked by SFX  -> is_music=1, do_write=0   (mirror only)
//   SFX                       -> is_music=0, do_write=1   (write only)
//
// The mirrors (ay_tone_lo/hi, ay_vol, noise_period, noise_vol) are the
// music's authoritative state, so an SFX must never touch them — the
// original PSGlib for SG-1000 follows the same convention.
static void psg_process_byte (unsigned char byte, unsigned char *last_latch_p,
                              unsigned char is_music, unsigned char do_write) {
	unsigned char ch, type_is_vol, raw_data;
	if (byte & 0x80) {
		*last_latch_p = byte;
		ch = (byte >> 5) & 0x03;
		type_is_vol = byte & 0x10;
		raw_data = byte & 0x0F;		// 4-bit data field
	} else {
		unsigned char latch = *last_latch_p;
		ch = (latch >> 5) & 0x03;
		type_is_vol = latch & 0x10;
		raw_data = byte & 0x3F;		// 6-bit data field for tone-high
	}

	if (is_music) {
		// Update mirrors so that restore_music_channel() can bring the
		// note back when the SFX releases the channel.
		if (type_is_vol) {
			if (ch == 3) noise_vol = raw_data & 0x0F;
			else         ay_vol[ch] = raw_data & 0x0F;
		} else if (byte & 0x80) {
			// Tone-low / noise selector (latch byte form: 4 LSBs)
			if (ch == 3) noise_period = byte & 0x03;
			else         ay_tone_lo[ch] = byte & 0x0F;
		} else {
			// Tone-high (data byte form: 6 LSBs)
			if (ch == 3) noise_period = byte & 0x03;
			else         ay_tone_hi[ch] = byte & 0x3F;
		}
	}

	if (!do_write) return;

	if (type_is_vol) {
		unsigned char attn = raw_data & 0x0F;
		if (ch == 3) {
			unsigned char v = sn_attn_to_ay_noise_amp[attn];
			if (v) mixer &= ~0x20; else mixer |= 0x20;
			ay_write (7, mixer);
			ay_write (10, v);
		} else {
			if (is_music) {
				ay_update_vol (ch);
			} else {
				// SFX direct write: derive amplitude on the fly without
				// touching ay_vol[] (music state).
				unsigned char v = sn_attn_to_ay_amp[attn];
				if (v) mixer &= ~(1 << ch); else mixer |= (1 << ch);
				ay_write (7, mixer);
				ay_write (8 + ch, v);
			}
		}
	} else {
		// Tone / noise period
		if (ch == 3) {
			ay_write (6, sn_noise_to_ay_period[(byte & 0x80 ? byte : raw_data) & 0x03]);
		} else if (is_music) {
			ay_update_tone (ch);
		} else {
			// SFX direct tone update: write to the SFX's own tone
			// mirror, not the music's ay_tone_*[].
			if (byte & 0x80) sfx_tone_lo[ch] = byte & 0x0F;
			else             sfx_tone_hi[ch] = byte & 0x3F;
			unsigned int period = ((unsigned int)(sfx_tone_hi[ch] & 0x3F) << 4)
			                    | (sfx_tone_lo[ch] & 0x0F);
			ay_write (ch * 2,     period & 0xFF);
			ay_write (ch * 2 + 1, (period >> 8) & 0x0F);
		}
	}
}

// Restore the AY register set for one music channel from the current
// mirror state. Called when an SFX finishes and the music takes the
// channel back, so the listener hears the music's intended note
// immediately instead of the SFX's stale residue.
static void restore_music_channel (unsigned char ch) {
	if (ch == 2) {
		ay_update_tone (2);
		ay_update_vol  (2);	// also fixes the mixer tone bit for ch C
	} else if (ch == 3) {
		// "ch 3" in PSGlib is the noise generator on AY ch C
		ay_write (6, sn_noise_to_ay_period[noise_period & 0x03]);
		unsigned char v = sn_attn_to_ay_noise_amp[noise_vol & 0x0F];
		if (v) mixer &= ~0x20; else mixer |= 0x20;
		ay_write (7, mixer);
		ay_write (10, v);
	}
}

// ---- Public API: silence/control ----

void PSGSilence (void) {
	__asm di __endasm;
	ay_init_safe ();
	__asm ei __endasm;
}

void PSGStop (void) {
	__asm di __endasm;
	if (music_status) {
		music_status = PSG_STOPPED;
		ay_vol[0] = 15; ay_update_vol (0);
		ay_vol[1] = 15; ay_update_vol (1);
		if (!sfx_chan2_active) {
			ay_vol[2] = 15; ay_update_vol (2);
		}
		if (!sfx_chan3_active) {
			noise_vol = 15;
			mixer |= 0x20;
			ay_write (7, mixer);
			ay_write (10, 0);
		}
	}
	__asm ei __endasm;
}

void PSGPlay (void *song) {
	PSGStop ();		// PSGStop already brackets its body with DI/EI.
	__asm di __endasm;
	music_loop_flag = 1;
	music_start = (const unsigned char *)song;
	music_ptr = music_start;
	music_loop_pt = music_start;
	music_skip = 0;
	music_subst_len = 0;
	music_last_latch = 0x9F;	// ch 0 vol silent
	music_status = PSG_PLAYING;
	__asm ei __endasm;
}

void PSGCancelLoop (void) {
	music_loop_flag = 0;
}

void PSGPlayNoRepeat (void *song) {
	PSGPlay (song);
	music_loop_flag = 0;
}

unsigned char PSGGetStatus (void) {
	return music_status;
}

void PSGSFXStop (void) {
	__asm di __endasm;
	if (sfx_chan2_active) {
		sfx_chan2_active = 0;
		if (music_status == PSG_PLAYING) restore_music_channel (2);
		else { ay_vol[2] = 15; ay_update_vol (2); }
	}
	if (sfx_chan3_active) {
		sfx_chan3_active = 0;
		if (music_status == PSG_PLAYING) restore_music_channel (3);
		else {
			noise_vol = 15;
			mixer |= 0x20;
			ay_write (7, mixer);
			ay_write (10, 0);
		}
	}
	sfx_status = PSG_STOPPED;
	__asm ei __endasm;
}

void PSGSFXPlay (void *sfx, unsigned char channels) {
	PSGSFXStop ();		// brackets its body with DI/EI
	__asm di __endasm;
	sfx_loop_flag = 0;
	sfx_start = (const unsigned char *)sfx;
	sfx_ptr = sfx_start;
	sfx_loop_pt = sfx_start;
	sfx_skip = 0;
	sfx_subst_len = 0;
	sfx_last_latch = 0xDF;	// ch 2 vol silent
	sfx_chan2_active = (channels & 0x01) ? 1 : 0;
	sfx_chan3_active = (channels & 0x02) ? 1 : 0;
	sfx_status = PSG_PLAYING;
	__asm ei __endasm;
}

void PSGSFXCancelLoop (void) {
	sfx_loop_flag = 0;
}

void PSGSFXPlayLoop (void *sfx, unsigned char channels) {
	PSGSFXPlay (sfx, channels);
	sfx_loop_flag = 1;
}

unsigned char PSGSFXGetStatus (void) {
	return sfx_status;
}

// ---- Per-frame stream advance ----

#define PSG_LATCH_BIT   0x80
#define PSG_DATA_MIN    0x40
#define PSG_WAIT        0x38		// 0x38..0x3F = wait (1..8 frames)
#define PSG_SUBSTRING   0x08		// 0x08..0x37 = substring (4..51 bytes)
#define PSG_LOOP        0x01
#define PSG_END         0x00

static void psg_frame_advance (
		const unsigned char **ptr_p,
		const unsigned char **start_p,
		const unsigned char **looppt_p,
		unsigned char *skip_p,
		unsigned char *subst_len_p,
		const unsigned char **subst_ret_p,
		unsigned char *loop_flag_p,
		unsigned char *status_p,
		unsigned char *last_latch_p,
		unsigned char check_chan_lock	// 1 = music: skip writes if ch claimed by SFX
		) {

	if (*skip_p) { (*skip_p)--; return; }
	if (*status_p == PSG_STOPPED) return;

	const unsigned char *p = *ptr_p;
	for (;;) {
		unsigned char b = *p++;
		// Substring handling
		if (*subst_len_p) {
			(*subst_len_p)--;
			if (*subst_len_p == 0) {
				p = *subst_ret_p;
			}
		}
		if (b >= PSG_DATA_MIN) {
			// `check_chan_lock` doubles as "this stream is the music".
			// Music updates the mirror state always and writes to AY
			// only when the channel isn't blocked by an SFX. SFX writes
			// directly to AY and never touches the music mirrors.
			unsigned char is_music = check_chan_lock;
			unsigned char do_write = 1;
			if (is_music) {
				unsigned char latch = b & 0x80 ? b : *last_latch_p;
				unsigned char ch = (latch >> 5) & 0x03;
				if ((ch == 2 && sfx_chan2_active) ||
				    (ch == 3 && sfx_chan3_active)) {
					do_write = 0;
				}
			}
			psg_process_byte (b, last_latch_p, is_music, do_write);
			continue;
		}
		// Command byte (b < 0x40)
		if (b == PSG_END) {
			if (*loop_flag_p) {
				p = *looppt_p;
				continue;
			}
			*status_p = PSG_STOPPED;
			return;
		}
		if (b == PSG_LOOP) {
			*looppt_p = p;
			continue;
		}
		if (b >= PSG_WAIT) {
			// 0x38..0x3F: skip (b & 7) extra frames after this one
			*skip_p = b & 0x07;
			*ptr_p = p;
			return;
		}
		if (b >= PSG_SUBSTRING) {
			// 0x08..0x37: substring with len = b - 4, fetch 16-bit offset
			*subst_len_p = b - 4;
			unsigned char lo = *p++;
			unsigned char hi = *p++;
			*subst_ret_p = p;
			p = *start_p + ((unsigned int)hi << 8 | lo);
			continue;
		}
		// Unknown command; bail out (corrupt stream)
		return;
	}
}

void PSGFrame (void) {
	if (music_status == PSG_STOPPED) return;
	psg_frame_advance (&music_ptr, &music_start, &music_loop_pt,
	                   &music_skip, &music_subst_len, &music_subst_ret,
	                   &music_loop_flag, &music_status,
	                   &music_last_latch, 1);
	// Note: we used to force-silence ch0/ch1 here when the stream
	// reached PSG_END without a loop. That was wrong - it kept
	// silencing them on every later frame, so as soon as anything
	// glitched the music_status to STOPPED the tonal voices vanished
	// and only the noise channel survived. PSGStop()/PSGPlay() are the
	// authoritative places that silence channels; PSGFrame should just
	// advance the stream and bail out if the stream is over.
}

void PSGSFXFrame (void) {
	if (sfx_status == PSG_STOPPED) return;
	psg_frame_advance (&sfx_ptr, &sfx_start, &sfx_loop_pt,
	                   &sfx_skip, &sfx_subst_len, &sfx_subst_ret,
	                   &sfx_loop_flag, &sfx_status,
	                   &sfx_last_latch, 0);
	if (sfx_status == PSG_STOPPED) {
		// SFX stream just finished. Release the channels it had
		// reserved and, if the music is still playing, immediately
		// restore the channel from the music mirrors so the listener
		// doesn't hear the SFX's last register values lingering until
		// the music sends its next note for that channel.
		if (sfx_chan2_active) {
			sfx_chan2_active = 0;
			if (music_status == PSG_PLAYING) restore_music_channel (2);
			else { ay_vol[2] = 15; ay_update_vol (2); }
		}
		if (sfx_chan3_active) {
			sfx_chan3_active = 0;
			if (music_status == PSG_PLAYING) restore_music_channel (3);
			else {
				noise_vol = 15;
				mixer |= 0x20;
				ay_write (7, mixer);
				ay_write (10, 0);
			}
		}
	}
}
