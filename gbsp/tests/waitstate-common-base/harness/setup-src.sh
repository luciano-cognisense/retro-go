#!/bin/bash
# Prepara src-<nome>/ = copia fiel do componente gbsp-libretro de um checkout + ganchos de TESTE no cpu.cpp copiado
# (iguais aos da frente de spills): harness_trace por instrucao e harness_alert no rotulo alert.
# Uso: setup-src.sh <nome> <checkout>   ex.: setup-src.sh base ~/HandsOnLab/rg-gbsp-performance-v4
set -e; H=$(cd "$(dirname "$0")" && pwd); N=$1; W=$2/gbsp/components/gbsp-libretro; D=$H/src-$N
rm -rf "$D"; mkdir -p "$D/bios"
cp "$W"/*.c "$W"/*.cpp "$W"/*.h "$D/"; cp "$W"/bios/open_gba_bios.h "$D/bios/"
for f in libretro.h boolean.h retro_common_api.h retro_environment.h retro_inline.h retro_miscellaneous.h; do
  cp "$W/libretro/libretro-common/include/$f" "$D/"; done
chmod -R u+w "$D"
python3 - "$D/cpu.cpp" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
def one(s, a, b):
    assert s.count(a) == 1, a[:60]; return s.replace(a, b)
s = one(s, 'interp_trace_instruction(PCR, 1);', 'harness_trace(reg[REG_PC], 1, cycles_remaining, n_flag, z_flag, c_flag, v_flag);')
s = one(s, 'interp_trace_instruction(PCR, 0);', 'harness_trace(reg[REG_PC], 0, cycles_remaining, n_flag, z_flag, c_flag, v_flag);')
s = one(s, '    alert:\n', '    alert:\n      harness_alert(cpu_alert & (CPU_ALERT_HALT | CPU_ALERT_IRQ), reg[REG_PC]);\n')
s = one(s, '#include "cpu_instrument.h"', '#include "cpu_instrument.h"\nextern "C" void harness_trace(u32, u32, s32, u32, u32, u32, u32);\nextern "C" void harness_alert(u32, u32);')
open(p, 'w').write(s)
PY
echo "ok src-$N <- $(git -C "$2" rev-parse --short HEAD)"
