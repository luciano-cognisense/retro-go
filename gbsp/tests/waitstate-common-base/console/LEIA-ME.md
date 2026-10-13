# A/B: base comum das tabelas de waitstate

Ordem: controle e depois candidata. Ambos usam o perfil performance v7 integral; a única diferença é `CONFIG_GBSP_WAITSTATE_COMMON_BASE=y` na candidata.

## Gravação

Controle:

```bash
source ~/HandsOnLab/gbsp-env.sh
esptool.py --chip esp32s3 --port /dev/cu.usbmodem5C4C1302571 --baud 921600 write_flash 0x3e0000 ~/HandsOnLab/outputs/gbsp-waitstate-common-base/console/controle/gbsp.bin
```

Candidata:

```bash
source ~/HandsOnLab/gbsp-env.sh
esptool.py --chip esp32s3 --port /dev/cu.usbmodem5C4C1302571 --baud 921600 write_flash 0x3e0000 ~/HandsOnLab/outputs/gbsp-waitstate-common-base/console/candidata/gbsp.bin
```

## Protocolo

- OC 0 e frameskip 0.
- Mario e Fire Red, usando os mesmos saves.
- Em cada jogo: 30 segundos parado na cena habitual, depois 60–90 segundos em movimento normal.
- Faça save state, continue por alguns segundos e faça load state em cada firmware.
- Compare principalmente `emu` e `total`; `render` e `fora` servem para identificar ruído externo.
- Não é necessário reproduzir exatamente cada movimento; mantenha o tipo e a duração do percurso semelhantes.

Salve os logs como `gbsp-waitstate-common-base-controle.txt` e `gbsp-waitstate-common-base-candidata.txt` em `outputs/`.
