# A/B: leitura da ROM por buffer interno (faltas de página)

Os dois builds usam o perfil performance v8 completo, no mesmo commit
(`76fdb487`). A única diferença é `CONFIG_GBSP_ROM_BOUNCE_READ=y` (buffer de
8 KB) na candidata. O `execute_arm` é idêntico nos dois.

| | bin | SHA-256 |
|---|---:|---|
| controle (v8) | 665.472 B | `99661086c0b433027523618631740c59ff5c4c25216d201d901e168a1758272b` |
| candidata | 665.792 B | `67b60ed57ebf1860023650a883fdc69f10b2e74aa311ee9d2520e97a0d207826` |

O que muda: no ESP32-S3 o driver do SD não faz DMA para a PSRAM, então cada
página de 32 KB lida para o cache de ROM vira 64 comandos de um setor. A
candidata lê em blocos de 8 KB para RAM interna e copia para a PSRAM.

## Download e gravação (Windows, terminal do ESP-IDF)

```
curl.exe -L -o rb-controle.bin https://github.com/luciano-cognisense/retro-go/raw/ab/gbsp-rom-bounce-read/ab-rom-bounce-read/controle/gbsp.bin
curl.exe -L -o rb-candidata.bin https://github.com/luciano-cognisense/retro-go/raw/ab/gbsp-rom-bounce-read/ab-rom-bounce-read/candidata/gbsp.bin
certutil -hashfile rb-controle.bin SHA256
certutil -hashfile rb-candidata.bin SHA256
esptool.py --chip esp32s3 --port COM13 --baud 921600 write_flash 0x3e0000 rb-controle.bin
```

## Protocolo (só Fire Red)

O que interessa são as faltas de página, que aparecem nas transições:

- OC 0 e frameskip 0; mesmo save.
- Abrir o Fire Red a partir do launcher (o cabeçalho do log registra o tempo
  de carga da ROM).
- 2–3 minutos andando: entrar e sair de prédios, trocar de mapa/rota, entrar
  em uma batalha e sair, abrir o menu. Repetir o mesmo roteiro nos dois
  firmwares, na medida do possível.
- Save state e load state uma vez.

Comparar principalmente `rom X em N faults` (custo por falta = X × quadros / N)
e `carga da ROM` no cabeçalho.

Salve como `gbsp-rom-bounce-controle.txt` e `gbsp-rom-bounce-candidata.txt`.
