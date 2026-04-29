# MSX1 port of MTE MK1 (loves_the_sg1000)

This document describes the port of the **MT Engine MK1** (originally for
SG-1000) to the **MSX1** family. The port targets a 64 KB plain ROM cart,
runs bare-metal (no BIOS dependency at runtime), and reuses the original
`.psg` PSG bytecode by translating it on the fly to AY-3-8910 register
writes.

The four reference games ship working out of the box:

| # | Game                       | MSX ROM                              |
|---|----------------------------|--------------------------------------|
| 1 | Cheril Perils Classic      | `examples/01_cheril_perils_classic/dev/build/CHERI*.rom`  |
| 2 | Sgt Helmet Training Day    | `examples/02_sgt_helmet_training_day/dev/build/SGT*.rom`   |
| 3 | Jet Paco                   | `examples/03_jet_paco/dev/build/JPACO*.rom`               |
| 4 | Che Man                    | `examples/04_che_man/dev/build/CHEMAN*.rom`               |

All produced as **65 536-byte plain ROMs**, no mapper, runnable on any
MSX1-compatible emulator (tested on openMSX 21 with `C-BIOS_MSX1`,
`Philips_NMS_8255`).

---

## 1. Hardware overview: SG-1000 vs MSX1

Both consoles share the **Z80** CPU and the **TMS9918A** VDP, which is
why the port is feasible without rewriting graphics logic. The
significant differences are:

| Subsystem | SG-1000              | MSX1                                  |
|-----------|----------------------|----------------------------------------|
| VDP I/O   | data $BE / ctrl $BF  | data $98 / ctrl $99                   |
| PSG       | SN76489 @ port $7F   | AY-3-8910 @ $A0/$A1/$A2 (addr/wr/rd)  |
| Joystick  | direct I/O at $DC/$DD| AY register 14 (port A is GPIO input) |
| Keyboard  | n/a                  | PPI 8255 matrix (rows via $AA, read $A9)|
| Slots     | none (flat 32 KB ROM)| primary slot register $A8, optional sub-slots at $FFFF |
| RAM       | 1 KB at $C000-$C3FF  | 16+ KB, mapped in pg 3 by BIOS at boot|
| BIOS      | none                 | present in slot 0 (we ignore it)      |
| Pause btn | hardware NMI         | does not exist                        |

The port keeps the engine, mainloop, and game logic untouched and
provides MSX-specific replacements for the platform layer:

```
src/dev/lib/MSXlib.c        VDP/sprites layer (mirrors SGlib's API)
src/dev/lib/MSXlib.h        same, with MSX I/O port definitions
src/dev/lib/PSGlib_msx.c    SN76489 -> AY-3-8910 bytecode translator
src/dev/lib/aPLib_msx.c     aPLib decompressor with MSX VDP ports
src/dev/lib/crt0_msx.s      bare-metal crt0 with cart "AB" header,
                            slot switch and IRQ vector at $0038
src/dev/hw_msx.h            HW_* -> MSX_* mapping (engine still uses
                            the abstract HW_* names) + SG_* aliases
                            for code that calls SGlib symbols directly
```

The original `lib/SGlib.c`, `lib/PSGlib.c`, `lib/aPLib.c`, `lib/crt0_sg.s`
remain untouched; the build chain picks one set or the other based on
`TARGET=msx|sg1000`.

---

## 2. Memory map (MSX1, 64 KB plain ROM)

```
$0000-$007F   ROM   reset + RST + IRQ vector ($0038 -> _MSX_isr) + NMI
$0080-$00FF   ROM   reserved padding (reachable as ROM page 0)
$0100-$3FFF   ROM   _DATA_BANK area (compressed assets: maps,
                   tilesets, spritesets, music) ~ 16 KB
$4000-$400F   ROM   "AB" cartridge header + INIT pointer
$4010-$BFFF   ROM   _CODE (engine, mainloop, game.c, MSXlib, ...) ~ 32 KB
$C000-$F37F   RAM   game globals (_DATA), BSS, heap (zeroed at boot)
$F380-$FFFF   RAM   stack + BIOS workspace (untouched)
```

The cart slot is replicated into pages 0 + 1 + 2 (plus page 3 stays
mapped to the system RAM provided by another slot) by the slot-switch
sequence in `crt0_msx.s`. After boot the BIOS is no longer visible
to the game and is never called.

The 48 KB ROM available for code+data is the same a 48 KB Plain SG-1000
cart provides, so all four reference games fit. The remaining 16 KB
of the 64 KB ROM image (positions $C000-$FFFF in the file) is unused
padding (`makebin -s 65536`).

---

## 3. Boot sequence

1. **MSX BIOS** scans every slot for a cartridge with the bytes
   `41 42` at $4000. The cartridge header tells the BIOS where its
   INIT routine is.
2. The BIOS calls **INIT** (our `crt0_init` in `crt0_msx.s`).
3. We push the cart slot from page 1 into pages 0 and 2 (replicating
   the primary-slot bits in `$A8` and the secondary-slot bits in
   `$FFFF` in case of expanded slots), preserving the page-3 RAM slot.
4. Stack pointer is set from `(HIMEM)` ($FC4A).
5. A `LDIR` zero-fills `$C000-$F37F`. SDCC z80 places uninitialised
   globals in `_DATA` (not `_BSS`), so the entire RAM range must be
   cleared, not just BSS.
6. `gsinit` copies `_INITIALIZER` to `_INITIALIZED`.
7. `MSX_init` programs the 8 VDP registers in mode 2, sets AY R7 to a
   safe value (port A input), and initialises the sprite table.
8. `EI` ; `call _main`. From here on, the game runs.

The IRQ vector at `$0038` is `jp _MSX_isr`. The CPU is in interrupt
mode 1 (set by the BIOS, not changed) so VDP VBlank IRQs land here.

---

## 4. ISR

```
_MSX_isr is __naked __interrupt:
    push regs (af bc de hl iy)
    in   a, ($99)            ; ack VDP, clears IRQ-pending flag
    set  _VDPBlank = 1
    read joystick port A via AY register 14 (selector at R15 bit 6 = 0)
    scan keyboard rows 8 and 4 for cursors+SPACE+M, OR-merge into KeysStatus
    scan keyboard row 7 for F5 -> _PauseRequested = 1
    call _PSGSFXFrame                ; tick SFX player
    if music_on call _PSGFrame       ; tick music player
    pop regs
    ei
    reti
```

Three subtle points:

1. **`__naked __interrupt`**: SDCC's plain `__interrupt` emits an `ei`
   *before* the register pushes, which lets a second VBlank pre-empt
   the first one before the state is saved and quickly blows the
   stack. We write the prologue/epilogue by hand to avoid that.
2. **No `bit 7` test**: in MSX1 the only interrupt source is the VDP
   VBlank, so we skip the test and unconditionally treat every IRQ
   as a frame tick. This avoids occasional dropped frames.
3. **PSG/SFX ticks** must happen here, not in the main loop, so music
   keeps a stable tempo even when the game logic stalls (e.g. screen
   load, decompression bursts).

---

## 5. PSGlib_msx (SN76489 → AY-3-8910 translation)

The Mojon Twins / devkitSMS toolchain emits `.psg` byte streams
encoded for the SN76489 chip. Each `data byte` is a literal SN command:

```
1cccTdddd   latch byte: ccc=channel (0..3), T=type (0=tone, 1=vol),
                        dddd=4 LSBs of value
0xHHHHHH    tone-high  data byte: 6 MSBs of period for the channel
                        whose latch was written last
1ccc1_dddd  volume:    4-bit attenuation (0=loud .. 15=silent)
```

Plus the surrounding wait/loop/substring bytecode is interpreted
identically to the original PSGlib parser.

`PSGlib_msx.c` re-implements the same parser, but instead of writing
each byte to the SN76489 port it keeps a per-channel mirror state
(tone period, volume, mixer/noise) and converts the writes to AY
register pairs:

| SN channel | AY mapping                                 |
|------------|--------------------------------------------|
| Ch 0       | AY ch A: R0/R1 (tone), R8 (amplitude)     |
| Ch 1       | AY ch B: R2/R3 (tone), R9 (amplitude)     |
| Ch 2       | AY ch C: R4/R5 (tone), R10 (amplitude)    |
| Ch 3 (noise) | AY R6 (noise period), mixer R7 enables noise on the same channel C |

Conversions:

* **Volume**: SN attenuation is 0=loud, 15=silent; AY amplitude is
  inverted (0=silent, 15=loud) → `ay = 15 - sn`.
* **Tone period**: SN is 10 bits and AY is 12 bits, but at MSX1's
  AY clock (3.58 MHz / 2) and SG-1000's SN clock (3.58 MHz / 1) the
  per-period frequency formulas yield the **same Hz for the same
  period number**, so the two LSB+MSB writes go to AY directly.
* **Noise**: SN noise period uses the lower 3 bits of the value; we
  put those bits into AY R6.

The mixer R7 is updated whenever a channel changes amplitude: if the
new amplitude is non-zero we enable the corresponding tone bit, if
zero we mute it. R7 bit 6 is held to **0 (port A input)** at all
times — failing to do this short-circuits the joystick port on real
hardware and triggers an "unsafe PSG port directions" warning in
openMSX.

Music and SFX streams are interleaved correctly: SFX gets priority
on its allocated channels (2 and/or 3) while music keeps playing on
the others.

---

## 6. Input

`MSX_isr` reads the digital joystick on port A by selecting it via AY
R15 bit 6 (must be **0**) and reading R14:

```
bit 0 = up    bit 1 = down  bit 2 = left  bit 3 = right
bit 4 = trigger A          bit 5 = trigger B
```

In addition we OR-merge the keyboard-as-joystick mapping:

| Tecla MSX | bit       |
|-----------|-----------|
| ↑         | UP   (0)  |
| ↓         | DOWN (1)  |
| ←         | LEFT (2)  |
| →         | RIGHT(3)  |
| SPACE     | TRG_A(4)  |
| M         | TRG_B(5)  |

Keyboard is scanned by writing the row index (0..10) to PPI port C
($AA, lower nibble) and reading port B ($A9). The four cursor keys
and SPACE are all in row 8; M is in row 4 bit 2; F5 (used as the
pause replacement for the SG-1000 hardware Pause button) is in row 7
bit 5. The PPI port C upper nibble (motor / caps / kana / cassette)
is preserved on every scan.

---

## 7. Build chain

The Makefile is dual-target. Key variables:

```
TARGET=sg1000   (default)  -> produces cart.sg via ihx2sms.exe
TARGET=msx                 -> produces cart.rom via makebin
BUILD_PREFIX=GAMENAME      -> build/GAMENAME01.rom, GAMENAME02.rom, ...
```

The MSX flow uses **plain `sdcc`** (not `sdcc.exe`) and `makebin -s 65536`,
both standard in any SDCC install on macOS / Linux / Windows. The SG-1000
flow keeps the original Mojon Twins toolchain (sdcc.exe + ihx2sms.exe).

`assets/map*.c`, `assets/tileset.c` and `assets/spriteset.c` are
compiled with `--codeseg DATA_BANK` so they end up in page 0 of the
ROM ($0100-$3FFF). Smaller asset files (`enems*.c`) stay in `_CODE`.
The linker is told to base `_DATA_BANK` at `$0100` via
`-Wl-b_DATA_BANK=0x0100`.

Each successful build is copied to `build/<PREFIX><NN>.rom` with an
auto-incrementing two-digit number, so it is always unambiguous which
ROM the emulator is loading. The MD5 of every fresh build is also
printed so the user can confirm they are not running a stale cache.

---

## 8. Bugs found and fixed during the port

The port surfaced a number of bugs both in the pre-existing engine
code and in newly written MSXlib. Documenting them here so anybody
porting *another* MTE MK1 game (or migrating the SG-1000 build to a
newer SDCC) does not waste time stepping on the same rakes.

| # | Bug                                  | Fix |
|---|--------------------------------------|-----|
| 1 | `utils/rand.c` label `rnd:` not exported by SDCC 4.x → linker error in both SG and MSX builds | Use double colon: `rnd::` (also works on SDCC 3.x). |
| 2 | `utils/memfill.c` reads its args from stack (legacy `__sdcccall(0)`) but SDCC 4.x default is `__sdcccall(1)` (1st 16-bit arg in HL). Result: BC was random garbage and `LDIR` copied thousands of random bytes per call. The ~10× overall slowdown in our MSX build was caused by this. | Rewrite the asm to read `value` and `length` from the correct stack offset *and* take `ptr` from HL. Same fix would apply to a re-port of the SG-1000 build with modern SDCC. |
| 3 | `aPLib_depack_VRAM` had the same legacy-vs-new calling-convention mismatch. `dest` was treated as if it were on stack but it actually arrives in HL; `src` arrives in DE. | Replace the original `pop bc; pop de; pop hl; push hl; push de; push bc; jp vram_depack` with a one-line `ex de, hl; jp vram_depack` in `aPLib_msx.c`. |
| 4 | The SDCC peephole optimizer (`--peep-asm`) was eliminating `LD C, ...; LD HL, ...; jp _outi_block-...` from `MSX_copySpritestoSAT` because it could not see the registers being used by the OUTI block ahead. | Mark the function `__naked` so the optimizer leaves the inline asm verbatim. Same applied to the asm wrappers in `aPLib_msx.c`. |
| 5 | `MSX_doUpdateList` written in C reloaded the `ulp` global from memory six times per iteration (≈220 cycles), instead of keeping it in HL. | Rewrite as `__naked` asm with `ulp` in HL throughout (≈90 cycles per iteration). |
| 6 | SDCC's `__interrupt` keyword emits an `EI` *before* the register-saving prologue, so a second VBlank could re-enter the ISR before the first had pushed its state. | Use `__naked __interrupt` and write the push/pop and `ei; reti` by hand. |
| 7 | `PSGSilence` originally wrote `0xFF` to AY R7, which sets bit 6 = 1 → port A becomes an output. On real hardware this short-circuits the joystick line; on openMSX it triggers a warning. | Always use `0xBF` (port B output, port A input, all channels muted). The same value is forced once in `MSX_init` so even if `PSGSilence` is never called, the AY starts in a safe state. |
| 8 | The cart code expected globals to be zero at boot (the SG-1000 crt0 zeroes its 1 KB of RAM); skipping that on MSX left `game_over = 0xFF` from the moment `_main` was entered, so every game returned to the title screen one frame after starting. | `crt0_msx.s` zero-fills the entire user-RAM range ($C000-$F37F). BIOS workspace at $F380-$FFFF is left intact. |
| 9 | The SG-1000 build of Cheril Perils ships with `m_n_*` (NTSC) PSG data. The SG-1000 builds of the other three games ship with `m_p_*` (PAL) data only; `build_assets.bat` regenerates the alternate set but it is a Windows batch. | When `MSX` is defined and only `m_p_*` symbols exist, force `#define PAL`. The music plays slightly faster than designed on a 60 Hz machine but at least everything resolves. The `port_to_msx.sh` helper does this automatically. |

---

## 9. Caveats & known limitations

* **Music tempo on NTSC**: when only the PAL `.psg` data is available
  (`#define PAL` forced) and we run on a 60 Hz machine, music plays
  ~20% faster than originally intended.
* **No envelope generator support**: the AY's envelope generator is
  not used; SN76489's percussive volume changes are emulated frame
  by frame instead, which sounds slightly different from a real AY
  composition.
* **No expanded sub-slot support beyond cart's primary slot**: the
  slot switch in `crt0_msx.s` does handle SLTSL ($FFFF), but only for
  the slot the cart is in. A machine with the cart in a deeply
  expanded sub-slot configuration may still confuse it.
* **Default machine should be MSX1**: openMSX with `C-BIOS_MSX2` works
  but emits a warning about the cart targeting a older system.

---

## 10. Reproducing the port on a new game

If you have a fresh MTE-MK1 SG-1000 game tree (the SDK plus your own
`game.c`, `murcia.c`, `assets/`, `engine/` overrides, etc.):

1. From the repository root, run

   ```
   ./port_to_msx.sh path/to/your_game/dev
   ```

   This copies the MSX support files, applies the `#ifdef MSX` branch
   to every source that includes `hw_sg1000.h`, drops in the patched
   `memfill.c` and `rand.c`, installs the dual-target Makefile, and
   guesses a 4-letter `BUILD_PREFIX` from the directory name.
2. Edit the new `Makefile` to set the right `BUILD_PREFIX` if the
   guess is wrong.
3. `make TARGET=msx`
4. The ROM will be at `your_game/dev/build/<PREFIX>NN.rom`.
5. Play it: `openmsx -machine C-BIOS_MSX1 -cart build/...rom -romtype Plain -command "plug joyporta msxjoystick1"`

The script is idempotent — re-running it on an already-ported tree is
safe and only updates the MSX-specific files (your game logic is left
alone).
