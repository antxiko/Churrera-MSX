# Makefile - dual target: SG-1000 (cart.sg) or MSX1 (cart.rom)
#
# Usage:
#   make                    -> SG-1000 (default, cart.sg, original flow)
#   make TARGET=sg1000      -> SG-1000 explicit
#   make TARGET=msx         -> MSX1 48 KB ROM (cart.rom)
#   make clean              -> wipe both
#
# The MSX flow uses 'sdcc' + 'makebin' (no .exe), suitable for macOS/Linux.
# The SG-1000 flow keeps the original Mojon Twins toolchain (sdcc.exe + ihx2sms).

# default target must come before any auto-generated rules (COMPILEC eval)
.PHONY: all
all:

SRCDIR  = .
OBJDIR  = rel
TARGET ?= sg1000

# ---------------------- per-target settings ----------------------
ifeq ($(TARGET),msx)
    CC       = sdcc
    AS       = sdasz80
    DEFS     = -DMSX
    LIB_SRC  = lib/MSXlib.c lib/PSGlib_msx.c lib/aPLib_msx.c
    CRT0_SRC = lib/crt0_msx.s
    CRT0_REL = lib/crt0_msx.rel
    LIB_REL  = lib/MSXlib.rel lib/PSGlib_msx.rel lib/aPLib_msx.rel
    # MSX 48 KB plain ROM: vectors page 0, header at $4000, code in pg 1+2.
    # Assets go in _DATA_BANK at $0080 (page 0, ~16 KB) so total code+data
    # fits in 48 KB without a mapper.
    LFLAGS   = -mz80 --no-std-crt0 --code-loc 0x4000 --data-loc 0xC000 -Wl-b_DATA_BANK=0x0100
    OUTPUT   = cart.rom
else
    CC       = sdcc
    AS       = sdasz80
    DEFS     =
    LIB_SRC  = lib/PSGlib.c lib/SGlib.c lib/aPLib.c
    CRT0_SRC = lib/crt0_sg.s
    CRT0_REL = lib/crt0_sg.rel
    LIB_REL  = lib/PSGlib.rel lib/SGlib.rel lib/aPLib.rel
    LFLAGS   = -mz80 --no-std-crt0 --data-loc 0xC000
    OUTPUT   = cart.sg
endif

CFLAGS = -mz80 --peep-file lib/peep-rules.txt --peep-asm $(DEFS)

# ---------------------- source discovery ----------------------
# C sources from the project (excluding lib/, which is per-target).
SRC := $(wildcard $(SRCDIR)/*.c) \
       $(wildcard $(SRCDIR)/assets/*.c) \
       $(wildcard $(SRCDIR)/engine/*.c) \
       $(wildcard $(SRCDIR)/utils/*.c)
OBJ := $(patsubst $(SRCDIR)%, $(OBJDIR)%, $(SRC:%.c=%.rel))
DEP := $(patsubst $(SRCDIR)%, $(OBJDIR)%, $(SRC:%.c=%.d))

# ---------------------- per-source compile rule ----------------------
# For MSX target, assets/*.c go in a separate code segment (_DATA_BANK)
# placed at $0080 by the linker, so they live in ROM page 0.
ifeq ($(TARGET),msx)
ASSETS_FLAGS = --codeseg DATA_BANK
else
ASSETS_FLAGS =
endif

define COMPILEC
$(1): $(2)
	mkdir -p $$(dir $(1))
	$(CC) $(CFLAGS) -M $(2) -o $(3)
	sed -i.bak -E "s/(.+)\.rel/$$(subst /,\/,$$(dir $(1)))\1.rel/" $(3) && rm -f $(3).bak
	$(CC) $(CFLAGS) $(if $(filter assets/map% assets/tileset% assets/spriteset% assets/enems0% assets/enems1%,$(patsubst ./%,%,$(2))),$(ASSETS_FLAGS)) -c $(2) -o $(1)
endef

$(foreach OBJF, $(OBJ), $(eval $(call COMPILEC, $(OBJF), $(patsubst $(OBJDIR)%, $(SRCDIR)%, $(OBJF:%.rel=%.c)), $(OBJF:%.rel=%.d))))

-include $(DEP)

# ---------------------- top-level targets ----------------------
.PHONY: clean msx sg1000

all: $(OUTPUT)

msx:
	$(MAKE) TARGET=msx

sg1000:
	$(MAKE) TARGET=sg1000

# ---------------------- linking ----------------------
$(CRT0_REL): $(CRT0_SRC)
	$(AS) -g -o $@ $<

lib/MSXlib.rel: lib/MSXlib.c lib/MSXlib.h lib/PSGlib.h
	$(CC) $(CFLAGS) -c lib/MSXlib.c -o lib/MSXlib.rel

lib/PSGlib_msx.rel: lib/PSGlib_msx.c lib/PSGlib.h lib/MSXlib.h
	$(CC) $(CFLAGS) -c lib/PSGlib_msx.c -o lib/PSGlib_msx.rel

lib/SGlib.rel: lib/SGlib.c lib/SGlib.h lib/PSGlib.h
	$(CC) $(CFLAGS) -c lib/SGlib.c -o lib/SGlib.rel

lib/PSGlib.rel: lib/PSGlib.c lib/PSGlib.h
	$(CC) $(CFLAGS) -c lib/PSGlib.c -o lib/PSGlib.rel

lib/aPLib.rel: lib/aPLib.c lib/aPLib.h
	$(CC) $(CFLAGS) -c lib/aPLib.c -o lib/aPLib.rel

lib/aPLib_msx.rel: lib/aPLib_msx.c lib/aPLib.h
	$(CC) -mz80 --peep-file lib/peep-rules.txt $(DEFS) -c lib/aPLib_msx.c -o lib/aPLib_msx.rel

cart.ihx: $(OBJ) $(CRT0_REL) $(LIB_REL)
	$(CC) -o cart.ihx $(LFLAGS) $(CRT0_REL) $(OBJ) $(LIB_REL)

# ---------------------- post-processing ----------------------
ifeq ($(TARGET),msx)
BUILD_PREFIX ?= CHERI

cart.rom: cart.ihx
	makebin -s 65536 cart.ihx cart.rom
	@mkdir -p build
	@N=$$(ls build/$(BUILD_PREFIX)*.rom 2>/dev/null | sed -E "s/.*$(BUILD_PREFIX)([0-9]+)\.rom/\1/" | sort -n | tail -1); \
	 N=$${N:-0}; \
	 N=$$((10#$$N + 1)); \
	 NAME=$$(printf "build/$(BUILD_PREFIX)%02d.rom" $$N); \
	 cp cart.rom $$NAME; \
	 echo ">>> built $$NAME (md5 $$(md5 -q cart.rom 2>/dev/null || md5sum cart.rom | cut -d' ' -f1))"
	@rm -f cart.lk cart.noi cart.ihx
else
cart.sg: cart.ihx
	../../../src/utils/ihx2sms.exe cart.ihx cart.sg
	@rm -f cart.lk cart.noi cart.map cart.ihx
endif

# ---------------------- cleanup ----------------------
clean:
	@rm -rf $(OBJDIR) cart.sg cart.rom cart.ihx cart.lk cart.noi cart.map
	@rm -f lib/*.rel
