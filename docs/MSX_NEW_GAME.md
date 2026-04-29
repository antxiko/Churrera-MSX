# Starting a new MSX1 game with the MTE MK1 SDK

Quick recipe to bootstrap a new game using the MSX-enabled SDK.

## Prerequisites

* **SDCC** 4.x for Z80 (tested with 4.5.0).
* **GNU make**, **bash**, **python3** (used by `port_to_msx.sh`).
* **openMSX** (or any MSX1 emulator) for testing.
* On macOS: `brew install sdcc openmsx`.
* On Linux: typical package names `sdcc`, `openmsx`.

## 1. Lay out the project

The simplest way is to start from one of the four reference games as
a template (smallest is `04_che_man`). Copy the directory and rename:

```
cp -R examples/04_che_man my_new_game
```

Inside `my_new_game/dev` you have:

* `game.c`       — entry point and game-specific glue
* `murcia.c`/`.h`— PSG (music + SFX) data tables
* `assets/*.c`   — tilesets, spritesets, maps, enemy tables, …
* `engine/`      — engine modules (player, enemies, bullets, …)
* `mainloop/`    — fragments of the main loop, included via `mainloop.h`
* `my/`          — per-game overrides (effects, custom_flickscreen, …)
* `lib/`         — platform layer (already includes the MSX files)
* `utils/`       — small helpers (delay, rand, memfill, …)

## 2. Adjust the build prefix

Open `Makefile` and set:

```make
BUILD_PREFIX ?= MYGAME
```

This makes successful builds land at `build/MYGAME01.rom`,
`MYGAME02.rom`, … with auto-incrementing numbers.

## 3. Pick / write your music data

The PSG bytecode in `murcia.c` is targeted at the SG-1000's SN76489
chip. Our `PSGlib_msx.c` translates it on the fly, so any existing
`.psg` file from the Mojon Twins toolchain plays straight away.

* If `murcia.c` only contains `m_p_*` (PAL) symbols, leave the
  ` #ifdef MSX  #define PAL  #endif ` block in `game.c` (the
  `port_to_msx.sh` helper inserts it automatically). Music will run
  ~20% fast on a 60 Hz machine; for an NTSC-tempo build, regenerate
  the data with `build_assets.bat` (Windows-only, in the original
  toolchain).
* If you have *both* `m_n_*` and `m_p_*` data, leave the `#ifdef MSX
  #define PAL #endif` out and `config.h` will pick the right set.

## 4. Build

```
cd my_new_game/dev
make TARGET=msx
```

The output is `build/MYGAME01.rom` (65 536 bytes, plain ROM). Subsequent
builds increment the number, so you always know which file the emulator
is loading.

## 5. Run

```
openmsx -machine C-BIOS_MSX1 \
        -cart build/MYGAME01.rom -romtype Plain \
        -command "plug joyporta msxjoystick1"
```

* `-machine C-BIOS_MSX1` chooses an MSX1 machine with the open-source
  C-BIOS. Any other MSX1 machine works too (e.g. `Philips_VG-8020`,
  `Toshiba_HX-10` if you have the BIOS roms).
* `-romtype Plain` tells openMSX the ROM is unmapped (no megaROM
  paging).
* `plug joyporta msxjoystick1` maps the keyboard arrows + space to the
  virtual joystick on port A. You can also use a real joystick.

Default in-game controls (you can extend them in `MSXlib.c`'s ISR):

| Key         | Action      |
|-------------|-------------|
| Cursors     | move        |
| Space       | button 1    |
| M           | button 2    |
| F5          | pause       |

## 6. Iterate

Workflow is fast: edit `.c`, `make TARGET=msx`, the ROM gets a fresh
number under `build/`, run again. The build chain prints the MD5 of
each ROM, so you can be sure your edits made it into the binary.

## 7. ROM size budget

The 64 KB plain ROM is laid out as:

```
ROM page 0   $0000-$3FFF   16 KB   vectors + assets in _DATA_BANK
ROM page 1+2 $4000-$BFFF   32 KB   header + _CODE
```

**Effective code budget = 32 KB**. Asset budget = 16 KB. Total = 48 KB.
The remaining 16 KB ($C000-$FFFF in the file) is left as padding so
the ROM image is exactly 64 KB.

If you outgrow this, options are:

* Move more `assets/*.c` to `_DATA_BANK` (edit the Makefile filter).
* Compress more aggressively (the engine already uses aPLib + RLE).
* Switch to a megaROM mapper (ASCII-8, Konami, Konami SCC). This is
  not supported out of the box.

## 8. Things to know

* **Page 0 is your cart, not BIOS.** After the slot switch in
  `crt0_msx.s`, the BIOS is no longer accessible. You don't need it
  for VBlank, joystick, keyboard, or sound — `MSXlib.c` and friends
  read the hardware directly.
* **Globals are zeroed at boot.** The crt0 wipes `$C000-$F37F` so any
  uninitialised global starts at zero.
* **Stack lives at `$F380` downwards**, BIOS workspace is preserved
  above it.
* **Don't write `0xFF` to AY R7.** Always keep bit 6 = 0 (port A
  input). The `MSX_init` and `PSGSilence` paths already do this; if
  you need to touch R7 yourself, OR / AND with `0xBF`.
