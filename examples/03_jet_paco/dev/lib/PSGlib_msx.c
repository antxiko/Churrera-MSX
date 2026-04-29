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
// Tone period for AY ch A/B/C: 12-bit value split in 2 regs.
// SN period is 10 bits but we just store the same number; AY ignores
// the upper 2 bits beyond what we set.
static unsigned char ay_tone_lo[3];	// low 4 bits of SN period (tone latch)
static unsigned char ay_tone_hi[3];	// high 6 bits of SN period (tone-high data)
static unsigned char ay_vol[3];		// SN attenuation 0..15
static unsigned char noise_period;	// AY R6
static unsigned char noise_vol;		// AY ch C amplitude when noise active
static unsigned char mixer;			// AY R7 cache

// last latch (per stream), to remember what the next data byte modifies
static unsigned char music_last_latch;
static unsigned char sfx_last_latch;

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

// ---- Low-level AY write helper ----
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

// Update AY amplitude for channel `ch` from cached SN attenuation.
static void ay_update_vol (unsigned char ch) {
	unsigned char v = 15 - (ay_vol[ch] & 0x0F);
	// Whenever a channel has volume, enable its tone in the mixer.
	if (v) {
		mixer &= ~(1 << ch);	// enable tone bit
	} else {
		mixer |= (1 << ch);	// disable tone bit
	}
	ay_write (7, mixer);
	ay_write (8 + ch, v & 0x0F);
}

// Process a single SN76489 byte (anywhere from $40 to $FF) and update AY.
// `last_latch` is updated when a latch byte arrives; for a data byte we
// use *last_latch_p to know what register to write.
static void psg_process_byte (unsigned char byte, unsigned char *last_latch_p) {
	if (byte & 0x80) {
		// Latch byte: 1cccTdddd
		*last_latch_p = byte;
		unsigned char ch = (byte >> 5) & 0x03;
		if (byte & 0x10) {
			// Volume latch (4-bit attenuation in dddd)
			unsigned char attn = byte & 0x0F;
			if (ch == 3) {
				// Noise volume -> AY ch C amplitude with noise enabled
				noise_vol = attn;
				unsigned char v = 15 - attn;
				if (v) {
					mixer &= ~0x20;		// enable noise on ch C
				} else {
					mixer |= 0x20;
				}
				ay_write (7, mixer);
				ay_write (10, v);
			} else {
				ay_vol[ch] = attn;
				ay_update_vol (ch);
			}
		} else {
			// Tone/noise latch (4 LSBs in dddd)
			if (ch == 3) {
				// Noise: only 5 bits used by AY, taken from the 3 LSBs
				// (matches the SN noise rate selection).
				noise_period = byte & 0x07;
				ay_write (6, noise_period);
			} else {
				ay_tone_lo[ch] = byte & 0x0F;
				ay_update_tone (ch);
			}
		}
	} else {
		// Data byte: 6 MSBs for the channel/type stored in *last_latch_p
		unsigned char latch = *last_latch_p;
		unsigned char ch = (latch >> 5) & 0x03;
		if (latch & 0x10) {
			// Volume update (rare for data byte; treat 4 LSBs as new attn)
			unsigned char attn = byte & 0x0F;
			if (ch == 3) {
				noise_vol = attn;
				unsigned char v = 15 - attn;
				if (v) mixer &= ~0x20; else mixer |= 0x20;
				ay_write (7, mixer);
				ay_write (10, v);
			} else {
				ay_vol[ch] = attn;
				ay_update_vol (ch);
			}
		} else {
			// Tone-high: 6 MSBs of period
			if (ch == 3) {
				// Noise period only uses 3 LSBs
				noise_period = byte & 0x07;
				ay_write (6, noise_period);
			} else {
				ay_tone_hi[ch] = byte & 0x3F;
				ay_update_tone (ch);
			}
		}
	}
}

// ---- Public API: silence/control ----

void PSGSilence (void) {
	ay_init_safe ();
}

void PSGStop (void) {
	if (music_status) {
		music_status = PSG_STOPPED;
		// Silence music channels (0, 1, and 2 if SFX not on it)
		ay_vol[0] = 15; ay_update_vol (0);
		ay_vol[1] = 15; ay_update_vol (1);
		if (!sfx_chan2_active) {
			ay_vol[2] = 15; ay_update_vol (2);
		}
		if (!sfx_chan3_active) {
			noise_vol = 15;
			mixer |= 0x20;	// noise off on ch C
			ay_write (7, mixer);
			ay_write (10, 0);
		}
	}
}

void PSGPlay (void *song) {
	PSGStop ();
	music_loop_flag = 1;
	music_start = (const unsigned char *)song;
	music_ptr = music_start;
	music_loop_pt = music_start;
	music_skip = 0;
	music_subst_len = 0;
	music_last_latch = 0x9F;	// ch 0 vol silent
	music_status = PSG_PLAYING;
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
	if (sfx_status) {
		// Free up the channels SFX was using.
		if (sfx_chan2_active) {
			if (music_status == PSG_STOPPED) {
				ay_vol[2] = 15;
				ay_update_vol (2);
			}
			// If music is playing, channel 2 will be retaken by the music
			// player on the next frame's data byte. We don't have to do
			// anything special here.
			sfx_chan2_active = 0;
		}
		if (sfx_chan3_active) {
			if (music_status == PSG_STOPPED) {
				noise_vol = 15;
				mixer |= 0x20;
				ay_write (7, mixer);
				ay_write (10, 0);
			}
			sfx_chan3_active = 0;
		}
		sfx_status = PSG_STOPPED;
	}
}

void PSGSFXPlay (void *sfx, unsigned char channels) {
	PSGSFXStop ();
	sfx_loop_flag = 0;
	sfx_start = (const unsigned char *)sfx;
	sfx_ptr = sfx_start;
	sfx_loop_pt = sfx_start;
	sfx_skip = 0;
	sfx_subst_len = 0;
	sfx_last_latch = 0xDF;	// ch 2 vol silent
	// SFX_CHANNEL2 = 0x01, SFX_CHANNEL3 = 0x02 in PSGlib.h
	sfx_chan2_active = (channels & 0x01) ? 1 : 0;
	sfx_chan3_active = (channels & 0x02) ? 1 : 0;
	sfx_status = PSG_PLAYING;
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
			// Music must check whether the channel implied by the latch
			// is currently owned by SFX, and skip the write if so.
			unsigned char latch = b & 0x80 ? b : *last_latch_p;
			unsigned char ch = (latch >> 5) & 0x03;
			unsigned char skip_write = 0;
			if (check_chan_lock) {
				if ((ch == 2 && sfx_chan2_active) ||
				    (ch == 3 && sfx_chan3_active)) {
					skip_write = 1;
				}
			}
			if (!skip_write) {
				psg_process_byte (b, last_latch_p);
			} else if (b & 0x80) {
				// still update the music's latch tracking even if we
				// don't write to the chip
				*last_latch_p = b;
			}
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
	psg_frame_advance (&music_ptr, &music_start, &music_loop_pt,
	                   &music_skip, &music_subst_len, &music_subst_ret,
	                   &music_loop_flag, &music_status,
	                   &music_last_latch, 1);
	if (music_status == PSG_STOPPED) {
		// Silence music channels that are not held by SFX.
		ay_vol[0] = 15; ay_update_vol (0);
		ay_vol[1] = 15; ay_update_vol (1);
	}
}

void PSGSFXFrame (void) {
	psg_frame_advance (&sfx_ptr, &sfx_start, &sfx_loop_pt,
	                   &sfx_skip, &sfx_subst_len, &sfx_subst_ret,
	                   &sfx_loop_flag, &sfx_status,
	                   &sfx_last_latch, 0);
	if (sfx_status == PSG_STOPPED) {
		PSGSFXStop ();
	}
}
