#!/usr/bin/env bash
# Fetch and build the as31 8051 assembler (Ken Stauffer's original) into
# tools/as31, with one fix: its "location counter overlaps" bitfield used an
# int shift that breaks on 64-bit hosts. Result: tools/as31/as31
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p tools
if [ ! -d tools/as31 ]; then
  git clone -q --depth 1 https://github.com/kjs452/as31 tools/as31
fi
cd tools/as31
sed -i 's/( 1 << ((a)%(sizeof(long)\*8)) )/( 1UL << ((a)%(sizeof(long)*8)) )/' as31.y as31.c
touch as31.c   # the generated parser is checked in; do not require yacc
make CFLAGS="-O2 -w -std=gnu89 -fcommon" >/dev/null
echo "built $(pwd)/as31"
