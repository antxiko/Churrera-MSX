# Mojon Twins loves the SG-1000 (now also MSX1!)

This is a port of [MTE MK1 NES / AGNES](https://github.com/mojontwins/MK1_NES) to the **SEGA SG-1000** (and, therefore, the **SEGA Master System**) console.

> ✨ **MSX1 fork (2026):** all four reference games (Cheril Perils Classic, Sgt Helmet Training Day, Jet Paco, Che Man) also build and run as 64 KB plain MSX1 ROMs.
> See **[docs/MSX_PORT.md](docs/MSX_PORT.md)** for the technical writeup of the port and **[docs/MSX_NEW_GAME.md](docs/MSX_NEW_GAME.md)** for a recipe to bootstrap a new MSX1 game with this SDK.
> Quick build: `cd examples/01_cheril_perils_classic/dev && make TARGET=msx`. Quick run: `openmsx -machine C-BIOS_MSX1 -cart build/CHERI*.rom -romtype Plain -command "plug joyporta msxjoystick1"`.

Albeit there are [some differencies](https://github.com/mojontwins/loves_the_sg1000/blob/master/docs/AGSG1000_requirements.md), the code is almost verbatim - the main difference is that this version is divided in several modules and the NES version is monolithic.

Credits
=======

**MT Engine MK1 SG1000** was designed and developed by **The Mojon Twins**

This engine uses **aPLib**, **PSGlib** and a heavily modified version of **SGlib** from [DevKitSMS](https://github.com/sverx/devkitSMS) by sverx.

License
=======

**MT Engine MK1 NES** is copyleft The Mojon Twins and is distributed under a [LGPL license](https://github.com/mojontwins/loves_the_sg1000/blob/master/LICENSE).

If you like this, you can [buy me a coffee](https://ko-fi.com/I2I0JUJ9).

Have fun.
