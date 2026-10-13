#!/bin/bash
# Gera as ROMs de estimulo de WAITCNT (tres sementes). As de alerta/estresse vem copiadas da frente de spills.
set -e; cd "$(dirname "$0")"
source ~/HandsOnLab/gbsp-env.sh >/dev/null 2>&1; export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
for seed in 1 0x2B 0x9D; do
  sed "s/        mov     r5, #1                  @ semente (o gerador troca)/        mov     r5, #$seed/" waitcnt.s > _w.s
  grep -q "mov     r5, #$seed" _w.s
  python mklink.py _w.s waitcnt-$seed.gba > waitcnt-$seed.sym
done
rm -f _w.s; ls *.gba
