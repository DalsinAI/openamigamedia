#!/bin/sh
# openmedia.library (bare: no startup code or C library; the RomTag's stub
# comes first) and the C:OpenMediaInfo command, for the 68040, with the
# os32-gcc16 stove (OpenAmigaGCC 16.2, OpenNDK's headers).
#   library/build.sh [OUT_DIR]     (default build/)
# The headers in include/clib, include/inline, include/proto,
# include/pragmas and fd/ are made from library/openmedia_lib.sfd by
# OpenNDK's tools/ndkgen.py.
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
OUT=${1:-$ROOT/build}
mkdir -p "$OUT"
# -fno-delete-null-pointer-checks: address 0 is memory on an Amiga.
# -fno-tree-loop-distribute-patterns: no loop becomes a call to memset or memcpy.
# -fstack-usage: the frames are checked below, as the library runs on its caller's stack.
# -Os: reading a few headers needs no speed, and it is 40 KB rather than 64.
"$CC" -m68040 -Os -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin \
    -fno-tree-loop-distribute-patterns -fstack-usage -Wall -Wextra -Werror -Wno-unused-parameter \
    -nostartfiles -nostdlib -I"$ROOT/include" -o "$OUT/openmedia.library" \
    "$HERE/openmedia_lib.c" "$HERE/om_probe.c" "$HERE/string.c" -lgcc
echo "$OUT/openmedia.library ($(wc -c < "$OUT/openmedia.library") bytes)"
# The largest stack frames (bytes): the deepest chain is OM_ObtainMediaInfoA, om_probe, a
# parser and a reader; recursion is the ISO box walk and Matroska's levels, each a small frame.
cat "$OUT"/openmedia.library-*.su | sed 's/.*://' | sort -k2 -n -r | head -5
rm -f "$OUT"/openmedia.library-*.su
"$CC" -m68040 -O2 -noixemul -fno-delete-null-pointer-checks -Wall -Werror -Wno-pointer-sign -I"$ROOT/include" \
    -o "$OUT/OpenMediaInfo" "$ROOT/tools/openmediainfo.c"
echo "$OUT/OpenMediaInfo ($(wc -c < "$OUT/OpenMediaInfo") bytes)"
