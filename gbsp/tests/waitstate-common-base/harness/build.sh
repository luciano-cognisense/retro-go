#!/bin/bash
# build.sh <nome> <diretorio-de-fontes> [defines/flags extras...]  ->  bin/sim-<nome>
# Opcoes do perfil v10 que afetam o interpretador; SAN=1 liga ASan+UBSan.
set -e; H=$(cd "$(dirname "$0")" && pwd)
NAME=$1; SRCDIR=$2; shift 2
OUT=$H/obj/$NAME; rm -rf "$OUT"; mkdir -p "$OUT" "$H/bin"
SANF=""; [ "${SAN:-0}" = 1 ] && SANF="-fsanitize=address,undefined -fno-omit-frame-pointer"
COMMON="-O2 -g -fno-strict-aliasing -I$SRCDIR -I$H/shim -DRETRO_GO=1 -DTRACE_INSTRUCTIONS=1 \
  -DCONFIG_GBSP_FAST_RAM_WRITES=1 -DCONFIG_GBSP_CPSR_CONSUMER_SYNC=1 -DCONFIG_GBSP_CPSR_LAZY_DISPATCH=1 \
  -DCONFIG_GBSP_FETCH_PC=1 -DCONFIG_GBSP_NO_CHEAT_HOOK=1 \
  -DCONFIG_GBSP_CPU_ALERT_PER_INSTRUCTION=1 -DCONFIG_GBSP_THUMB_RESIDENT_PC=1 \
  -DCONFIG_GBSP_WAITSTATE_COMMON_BASE=1 -Wno-unused-value -Wno-unused-but-set-variable \
  -Wno-unused-variable -Wno-sign-compare -Wno-writable-strings -Wno-deprecated $SANF $*"
for f in main.c gba_memory.c savestate.c input.c sound.c cheats.c memmap.c serial.c gbp.c rfu.c gba_cc_lut.c; do
  cc $COMMON -std=gnu99 -c "$SRCDIR/$f" -o "$OUT/${f%.c}.o" 2>>"$OUT/compile.log"; done
for f in cpu.cpp video.cpp; do c++ $COMMON -std=gnu++14 -c "$SRCDIR/$f" -o "$OUT/${f%.cpp}.o" 2>>"$OUT/compile.log"; done
cc $COMMON -std=gnu99 -c "$H/harness.c" -o "$OUT/harness.o" 2>>"$OUT/compile.log"
c++ $COMMON "$OUT"/*.o -o "$H/bin/sim-$NAME" -lm 2>>"$OUT/compile.log"
echo "ok: bin/sim-$NAME"
