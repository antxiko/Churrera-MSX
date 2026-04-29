# Churrera-MSX

**MSX1 fork of [Mojon Twins' MTE MK1 SG-1000 engine](https://github.com/mojontwins/loves_the_sg1000)** (a.k.a. *loves_the_sg1000*), the SG-1000 port of [MTE MK1 NES / AGNES](https://github.com/mojontwins/MK1_NES).

> **Status:** all four reference games — *Cheril Perils Classic*, *Sgt Helmet Training Day*, *Jet Paco* and *Che Man* — boot, run and produce sound on a real MSX1 (tested in openMSX 21 / `C-BIOS_MSX1`). Each one builds as a 65 536-byte plain ROM, no mapper, no BIOS dependency at runtime.

## What this fork adds on top of `loves_the_sg1000`

* **MSX1 platform layer** (`lib/MSXlib.{c,h}`, `lib/PSGlib_msx.c`, `lib/aPLib_msx.c`, `lib/crt0_msx.s`, `hw_msx.h`) — same public API as the original SGlib so the engine and game logic compile unchanged.
* **Bare-metal 64 KB plain ROM** with `"AB"` cart header at `$4000`, primary + secondary slot switch, RAM zero-fill at boot. No BIOS calls during gameplay.
* **`.psg` runtime translator**: the SN76489 bytecode produced by the Mojon Twins toolchain plays straight on the AY-3-8910 by translating each command on the fly, so existing music / SFX data is reused as-is.
* **Input via joystick + keyboard**: cursors / space / M / F5 are OR-merged with the physical joystick on port A.
* **Dual-target Makefile**: `make TARGET=sg1000` (default, untouched original flow) or `make TARGET=msx`. Numbered builds at `build/<PREFIX><NN>.rom` so you always know which ROM the emulator is loading.
* **`port_to_msx.sh`**: idempotent helper to bootstrap a fresh MTE-MK1 game tree to MSX in one command.
* **Bug fixes that affect both targets**:
  - `utils/rand.c`: `rnd:` → `rnd::` (SDCC 4.x export).
  - `utils/memfill.c`: rewritten for SDCC's `__sdcccall(1)` default. The legacy version was reading garbage from the stack and producing huge `LDIR` runs — fixing this single bug recovered a ~10× speed regression.

## Quick start

### Prerequisites

* SDCC 4.x for Z80 (`brew install sdcc` on macOS, package `sdcc` on most Linuxes).
* GNU make, bash, python3.
* [openMSX](https://openmsx.org/) for testing.

### Build & run

```bash
cd examples/01_cheril_perils_classic/dev
make TARGET=msx
openmsx -machine C-BIOS_MSX1 \
        -cart build/CHERI01.rom -romtype Plain \
        -command "plug joyporta msxjoystick1"
```

Default in-game controls (also honoured by the joystick port):

| Key       | Action     |
|-----------|------------|
| Cursors   | move       |
| Space     | button 1   |
| M         | button 2   |
| F5        | pause      |

### The four reference games

| # | Game                          | Build prefix |
|---|-------------------------------|--------------|
| 1 | Cheril Perils Classic         | `CHERI`      |
| 2 | Sgt Helmet Training Day       | `SGT`        |
| 3 | Jet Paco                      | `JPACO`      |
| 4 | Che Man                       | `CHEMAN`     |

`cd examples/<game>/dev && make TARGET=msx` and the MSX ROM appears under `build/`.

## Documentation

* **[docs/MSX_PORT.md](docs/MSX_PORT.md)** — full technical writeup of the port. Memory map, boot sequence, ISR, PSGlib_msx translation logic, *and* a list of the 9 bugs we hit during the port and how each one was fixed. Recommended reading if you ever need to port another MTE-MK1 game (or migrate the SG-1000 build to a newer SDCC).
* **[docs/MSX_NEW_GAME.md](docs/MSX_NEW_GAME.md)** — recipe to bootstrap a brand-new MSX1 game on top of this SDK.
* **[docs/AGSG1000_requirements.md](docs/AGSG1000_requirements.md)** — original Mojon Twins notes on AGSG1000 vs AGNES (kept verbatim).

## Cross-platform notes

* The **SG-1000 build (`make TARGET=sg1000`) is preserved unchanged** — the `lib/SGlib.c`, `lib/PSGlib.c`, `lib/aPLib.c` and `lib/crt0_sg.s` are still there and are still selected by default.
* All MSX-specific files live alongside the originals (`lib/MSXlib.c`, `lib/PSGlib_msx.c`, `lib/aPLib_msx.c`, `lib/crt0_msx.s`, `hw_msx.h`); the `Makefile` picks one set or the other based on `TARGET`.
* Engine/utils sources got an extra `#ifdef MSX` branch alongside the existing `#ifdef SMS` one — no SG-1000 code path was removed.

## Credits

* **MTE MK1 NES / AGNES** and **MTE MK1 SG-1000 (loves_the_sg1000)**: designed and developed by [The Mojon Twins](https://github.com/mojontwins).
* **MSX1 port (this fork)**: [@antxiko](https://github.com/antxiko), 2026.
* SGlib / PSGlib / aPLib originally from [devkitSMS](https://github.com/sverx/devkitSMS) by sverx; modified versions retained from the upstream `loves_the_sg1000`.

## License

Same as upstream: **LGPL**, see [LICENSE](LICENSE).

If you like this, [buy the original Mojon Twins a coffee](https://ko-fi.com/I2I0JUJ9). Have fun.
