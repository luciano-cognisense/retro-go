# Regressão host do gbsp

Este executor compara duas árvores do retro-go usando o `execute_arm()` real e
as ROMs sintéticas determinísticas já mantidas em
`tests/waitstate-common-base/harness`. Ele é o primeiro gate para novas
otimizações: uma candidata divergente não deve gerar firmware de console.

O teste cobre, por quadro:

- estado completo do GBA;
- PC, modo, ciclos, flags e r0-r15 depois de cada instrução;
- alertas HALT/IRQ;
- execução normal, idle, save/load, reset e execução longa;
- rewind para confirmar a restauração de WAITCNT.

Uso básico, comparando uma candidata com esta própria árvore:

```sh
gbsp/tests/host-regression/run.py --candidate /caminho/do/worktree
```

Por padrão, `--baseline` é a raiz que contém este script. Para uma passada
rápida use `--quick`. Para recompilar e executar também com ASan/UBSan use
`--sanitizers`. O resultado vai para `outputs/host-regression/` na árvore de
baseline, ou para o diretório informado por `--output`.

O executor não distribui ROMs comerciais. Uma futura camada de replay com ROM
do usuário deve receber o caminho externamente e nunca copiar a ROM para o Git.
Áudio real, I2S, SD, display, FreeRTOS e comportamento elétrico continuam sendo
gates de hardware; este harness elimina primeiro regressões determinísticas do
núcleo e do savestate.
