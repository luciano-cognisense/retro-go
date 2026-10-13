# Pacote de validação — base comum das tabelas de waitstate (v8)

Cópia versionada de `HandsOnLab/outputs/gbsp-waitstate-common-base/` (backup de
2026-10-12), sem binários de firmware, objetos e cópias de fonte. A decisão
está em `RESULTADO.md` (offline) e `RESULTADO-CONSOLE.md` (aparelho); o perfil
resultante é `gbsp/profiles/performance-v8.sdkconfig`.

Candidata: `afeda48a` (`CONFIG_GBSP_WAITSTATE_COMMON_BASE`) sobre a v7 `9427b3b5`.

## Conteúdo

- `RESULTADO.md`, `RESULTADO-CONSOLE.md`: análise e decisão.
- `console/`: protocolo de A/B (`LEIA-ME.md`) e o `sdkconfig`/HEAD de cada
  build. `SHA256SUMS` lista também os `gbsp.bin`/`.elf`/`.map` originais, que
  não estão aqui.
- `verify-console.py`, `verification-console.json`: 22 critérios sobre os
  builds de console (tamanhos, folga da partição, diferenças de sdkconfig).
- `assembly-analysis.json`, `thumb-reweight-current.json`,
  `reweight-current.py`, `arm-condition-profile.txt`: custo por instrução
  ARM/Thumb ponderado pelo perfil real de Mario e Fire Red.
- `analysis/`: scripts de análise do assembly Xtensa (caminhos, spills, slots
  DWARF). Os `.o`/`.asm` gerados não foram copiados.
- `harness/`: harness diferencial no host que roda o `execute_arm()` real e
  compara variantes quadro a quadro (hash de estado, rastreio por instrução,
  alertas). `setup-src.sh` copia o componente de um checkout e injeta os
  ganchos de teste; `build.sh` compila (`SAN=1` liga ASan/UBSan);
  `regressao.py` e `rewind.py` rodam os cenários. `rom/` tem as ROMs sintéticas
  de estímulo (WAITCNT, alertas, estresse) e `resultados/` os JSON da rodada
  aceita (33/33 idênticos, 35/35 save/load).

## Observações

- Alguns scripts têm caminhos da máquina original (`~/HandsOnLab/...`, a
  toolchain em `~/.espressif` em `verify-console.py`); ajuste antes de rodar.
- `rom/gen-roms.sh` espera um `waitcnt.s` que não estava no backup; as ROMs
  `waitcnt-*.gba` geradas estão incluídas.
- `regressao.py` compara os binários `bin/sim-base` e `bin/sim-fix`, que o
  `build.sh` gera a partir de `src-base/` e `src-fix/`.
