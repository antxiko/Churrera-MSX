#!/usr/bin/env bash
# Port one of the loves_the_sg1000 examples to MSX1.
# Idempotent: re-running on an already-ported tree is safe.
#
# Usage: port_to_msx.sh <path-to-example/dev>

set -e

if [ -z "$1" ] || [ ! -d "$1" ]; then
    echo "usage: $0 <path-to-example/dev>"
    exit 1
fi

DEV="$1"
SRC=/Users/fx-media/Documents/ChurreraMSX/loves_the_sg1000/examples/01_cheril_perils_classic/dev

echo "=== Porting $DEV to MSX ==="

# 1) Copy MSX support files (over-write OK; these are the canonical SDK).
cp "$SRC/lib/MSXlib.c"       "$DEV/lib/MSXlib.c"
cp "$SRC/lib/MSXlib.h"       "$DEV/lib/MSXlib.h"
cp "$SRC/lib/aPLib_msx.c"    "$DEV/lib/aPLib_msx.c"
cp "$SRC/lib/PSGlib_msx.c"   "$DEV/lib/PSGlib_msx.c"
cp "$SRC/lib/crt0_msx.s"     "$DEV/lib/crt0_msx.s"
cp "$SRC/hw_msx.h"           "$DEV/hw_msx.h"
cp "$SRC/utils/memfill.c"    "$DEV/utils/memfill.c"

# 2) Patch lib/aPLib.h (header declaration unchanged for sdcccall(1)).
# (Already matches; nothing to do.)

# 3) Patch utils/rand.c: 'rnd:' -> 'rnd::' (export label for srand).
if grep -q '^[[:space:]]*rnd:$' "$DEV/utils/rand.c"; then
    sed -i.bak 's/^\([[:space:]]*\)rnd:$/\1rnd::/' "$DEV/utils/rand.c"
    rm -f "$DEV/utils/rand.c.bak"
    echo "  patched utils/rand.c (rnd:: global label)"
fi

# 4) Patch source files that include hw_sg1000.h to also handle MSX.
patch_branch () {
    local f="$1"
    [ ! -f "$f" ] && return
    # Skip if already patched
    grep -q 'hw_msx.h' "$f" 2>/dev/null && return
    # Two patterns to detect:
    #   (a) "#ifdef SMS"-style branch (engine/utils/.h)  -> insert #ifdef MSX above
    #   (b) plain include of hw_sg1000.h with no SMS branch -> wrap with #ifdef MSX
    if grep -q '^#ifdef SMS' "$f"; then
        # Add MSX branch before SMS branch.
        # Pattern: replace the whole #ifdef SMS .. #else .. #include "...hw_sg1000.h" ... #endif
        # block, prepending a MSX branch.
        python3 <<EOF
import re, io
with open("$f","r") as fp:
    src = fp.read()
pattern = re.compile(
    r'#ifdef\\s+SMS\\s*\\n'
    r'(?P<sms>(?:.*\\n)*?)'
    r'#else\\s*\\n'
    r'(?P<sg>(?:.*\\n)*?)'
    r'#endif',
    re.MULTILINE)
m = pattern.search(src)
if m:
    sms_body = m.group('sms')
    sg_body  = m.group('sg')
    # detect relative path used (../lib/SGlib.h vs lib/SGlib.h)
    rel = '../' if '../hw_sg1000.h' in sg_body or '../lib/SGlib.h' in sg_body else ''
    msx_body = (
        '\\t#include "%shw_msx.h"\\n' % rel +
        '\\t#include "%slib/MSXlib.h"\\n' % rel
    )
    repl = (
        '#ifdef MSX\\n' + msx_body +
        '#elif defined(SMS)\\n' + sms_body +
        '#else\\n' + sg_body +
        '#endif'
    )
    src = src[:m.start()] + repl + src[m.end():]
    with open("$f","w") as fp:
        fp.write(src)
EOF
        echo "  patched (SMS-branch) $f"
    elif grep -q '#include[[:space:]]*"\(\\.\\./\\)*hw_sg1000.h"' "$f"; then
        python3 <<EOF
import re
with open("$f","r") as fp:
    src = fp.read()
# Find an include of hw_sg1000.h and the line above it that includes SGlib.h
m = re.search(r'(#include\\s*"((?:\\.\\./)*)hw_sg1000\\.h"\\s*\\n#include\\s*"((?:\\.\\./)*)lib/SGlib\\.h"\\s*\\n)', src)
if m:
    rel = m.group(2)
    block = m.group(1)
    repl = (
        '#ifdef MSX\\n'
        '\\t#include "%shw_msx.h"\\n' % rel +
        '\\t#include "%slib/MSXlib.h"\\n' % rel +
        '#else\\n' +
        block.replace('#include','\\t#include').rstrip() + '\\n' +
        '#endif\\n'
    )
    src = src[:m.start()] + repl + src[m.end():]
    with open("$f","w") as fp:
        fp.write(src)
EOF
        echo "  patched (plain-include) $f"
    fi
}

# Apply to all C/H files that mention hw_sg1000
while IFS= read -r f; do
    patch_branch "$f"
done < <(grep -rl 'hw_sg1000' "$DEV" 2>/dev/null | grep -v '\.bak$')

# 5) Replace the Makefile and patch the BUILD_PREFIX based on game name.
cp "$SRC/Makefile" "$DEV/Makefile"
# Derive a sensible 4-letter prefix from the directory name.
GAME_DIR=$(basename "$(dirname "$DEV")")
case "$GAME_DIR" in
    *cheril*)  PREFIX=CHERI ;;
    *helmet*)  PREFIX=SGT ;;
    *paco*)    PREFIX=JPACO ;;
    *che_man*) PREFIX=CHEMAN ;;
    *)         PREFIX=$(echo "$GAME_DIR" | tr -d '0-9_' | tr '[:lower:]' '[:upper:]' | head -c 5) ;;
esac
sed -i.bak "s/BUILD_PREFIX ?= GAME/BUILD_PREFIX ?= $PREFIX/" "$DEV/Makefile"
rm -f "$DEV/Makefile.bak"
echo "  Makefile installed (BUILD_PREFIX=$PREFIX)"

# 6) Apply MSX-specific defines (e.g., force PAL when only m_p_* PSG data is available).
if grep -q '^//#define PAL' "$DEV/game.c" 2>/dev/null; then
    if grep -q 'm_p_title_psg\|m_p_stagea_psg' "$DEV"/*.c 2>/dev/null && \
       ! grep -q 'm_n_title_psg' "$DEV"/*.c 2>/dev/null; then
        # Only m_p_* exists -> add #ifdef MSX #define PAL block if not already there.
        if ! grep -q '^#ifdef MSX$' "$DEV/game.c"; then
            sed -i.bak '/^\/\/#define PAL/a\
\
#ifdef MSX\
#define PAL\
#endif\
' "$DEV/game.c"
            rm -f "$DEV/game.c.bak"
            echo "  game.c: forced #define PAL when MSX (m_n_* PSG data missing)"
        fi
    fi
fi

# 6) Make sure build/ exists.
mkdir -p "$DEV/build"

echo "=== Done. Try: cd $DEV && make TARGET=msx ==="
