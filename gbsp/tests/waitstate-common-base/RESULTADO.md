# Base comum das tabelas de waitstate — resultado offline

Branch `experiment/gbsp-waitstate-common-base`, commit `afeda48a` sobre a performance v7 `9427b3b5`. A opção `CONFIG_GBSP_WAITSTATE_COMMON_BASE` é `default n`.

A candidata coloca `ws_cyc_seq` e `ws_cyc_nseq` em uma estrutura contígua, permitindo ao GCC manter uma base comum no laço Thumb. A opção desligada reproduz os objetos de CPU e memória da v7 após remover debug.

## Assembly da v7

- Thumb, ponderado pelo perfil real de 985.538.694 instruções:
  - Fire Red: −1,1485 instrução, +0,0770 salto, −0,9769 acesso à pilha e −0,2236 `l32r` por instrução Thumb; cobertura 99,9848%.
  - Mario: −1,1692 instrução, +0,1121 salto, −0,9509 acesso à pilha e −0,2309 `l32r`; cobertura 99,9230%.
- ARM: Mario +0,16 instrução, −0,15 salto e +0,11 acesso à pilha; Fire Red +0,17, −0,18 e +0,12.
- Frame de `execute_arm`: 96 → 112 B. IRAM do objeto CPU: +92 B; IRAM final do ELF: +88 B.

O saldo não pode ser decidido só pelo assembly: Thumb melhora bastante, mas ARM sofre uma pequena troca e o frame cresce. A candidata merece A/B no aparelho.

## Semântica

- Save/load com WAITCNT variável: controle 35/35 e candidata 35/35.
- Regressão diferencial do núcleo real: 33/33 idênticos, incluindo 6.000 quadros, reset, alertas e save/load.
- ASan/UBSan: 33/33 idênticos; sem ASan e apenas os mesmos UBs antigos nas duas variantes.
- A correção de reload de waitstate no savestate permanece ativa.

## Pacote de console

Builds limpos e contemporâneos, no mesmo HEAD. Configurações diferem apenas em `CONFIG_GBSP_WAITSTATE_COMMON_BASE`.

| | controle | candidata |
|---|---:|---:|
| bin | 665.360 B | 665.440 B |
| folga em 0xd0000 | 186.608 B | 186.528 B |
| .iram0.text | 117.199 B | 117.287 B |
| .dram0.data | 13.244 B | 13.244 B |
| .dram0.bss | 166.520 B | 166.520 B |
| SHA-256 | `739f3b0423a6405500fe8d1b7b6262a882b31a62e19340e002b538b25de17d73` | `1c81eeeb82e70989e6df7eb2964ec89e49c09961fa818a4a792df08e0f67a2a6` |

`verification-console.json`: todos os 22 critérios passam.
