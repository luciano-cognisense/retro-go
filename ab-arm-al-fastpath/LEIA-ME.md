# A/B: caminho rápido AL na condição ARM

Os dois builds usam o perfil performance v8 completo, no mesmo commit
(`26c0668d`). A única diferença é `CONFIG_GBSP_ARM_AL_FASTPATH=y` na candidata.
A análise offline está em `gbsp/tests/arm-al-fastpath/RESULTADO.md`.

| | bin | SHA-256 |
|---|---:|---|
| controle (v8) | 665.392 B | `48088dbe1eff710532e08f9d4e4758d9239bed83c1c3dea43e0f4e609956e145` |
| candidata | 664.960 B | `e96a0f139fa4da8c374640c15b24af9272b3cf87b73238cda0f1f446b2adc35d` |

## Download

No GitHub, abra o branch `ab/gbsp-arm-al-fastpath`, entre em
`ab-arm-al-fastpath/controle/` ou `candidata/`, clique em `gbsp.bin` e use
"Download raw file". Confira o SHA-256 (`shasum -a 256 gbsp.bin`).

## Gravação

Mesmo endereço do A/B da v8 (partição do gbsp). Ajuste a porta:

```bash
source ~/HandsOnLab/gbsp-env.sh
esptool.py --chip esp32s3 --port /dev/cu.usbmodem5C4C1302571 --baud 921600 write_flash 0x3e0000 controle/gbsp.bin
```

e depois, para a candidata, `candidata/gbsp.bin` no mesmo endereço.

## Protocolo

Igual ao da v8 (`gbsp/tests/waitstate-common-base/console/LEIA-ME.md`), com o
Fire Red primeiro:

- OC 0 e frameskip 0; mesmos saves.
- Fire Red: 30 s parado na cena habitual, depois 60–90 s andando normalmente
  (mapa, prédio, uma batalha se possível). Depois, o mesmo no Mario.
- Em cada firmware: save state, alguns segundos, load state.
- Comparar principalmente `emu` e `fora`; `render` e `total` ajudam a
  identificar ruído.

Salve os logs como `gbsp-arm-al-fastpath-controle.txt` e
`gbsp-arm-al-fastpath-candidata.txt` e suba para o Drive (pasta `backup`).
