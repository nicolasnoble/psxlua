#!/bin/sh
#
# Compiles tests/sample.lua with luac.ps-exe under an emulator and checks the
# bytecode it produced is the target's, not the host's.
#
#   make -C src psx-luac NUGGET=/path/to/nugget
#   tests/run-luac.sh /path/to/pcsx-redux
#
# The emulator has to serve two things the executable depends on: PCDRV for
# file I/O, and the unmapped read at 0x40000000 that args.lua answers with the
# argument buffer.

set -e

EMU=${1:?usage: $0 <pcsx-redux binary>}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EXE=$ROOT/src/luac.ps-exe

if [ ! -f "$EXE" ]; then
    echo "$EXE is missing; build it with make -C src psx-luac NUGGET=..." >&2
    exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cp "$EXE" "$ROOT/tests/sample.lua" "$ROOT/src/args.lua" "$WORK/"
echo "createArgsBuffer('-o', 'sample.luac', 'sample.lua')" >> "$WORK/args.lua"

"$EMU" -testmode -run -stdout -lua_stdout -pcdrv -pcdrvbase "$WORK" \
       -dofile "$WORK/args.lua" -loadexe "$WORK/luac.ps-exe"

if [ ! -s "$WORK/sample.luac" ]; then
    echo "luac.ps-exe produced no bytecode" >&2
    exit 1
fi

# Lua 5.2 signature, little endian, then sizeof int, size_t, Instruction and
# lua_Number, then the integral-number flag. Compiling on the target is what
# makes these the target's by construction.
EXPECTED=1b4c75615200010404040401
GOT=$(od -An -tx1 -N12 "$WORK/sample.luac" | tr -d ' \n')

if [ "$GOT" != "$EXPECTED" ]; then
    echo "bytecode header is $GOT, expected $EXPECTED" >&2
    exit 1
fi

echo "sample.luac: $(wc -c < "$WORK/sample.luac") bytes, header $GOT"
