# Leitura da ROM por buffer interno — aceita (perfil v9)

Opção `CONFIG_GBSP_ROM_BOUNCE_READ` (buffer de 8 KB), commit `76fdb487` sobre a
v8. Pacote de A/B no branch `ab/gbsp-rom-bounce-read`.

## Causa

O cache de ROM fica em PSRAM. No ESP32-S3, `sdmmc_read_sectors()` (ESP-IDF
5.3.1, `components/sdmmc/sdmmc_cmd.c`) não faz DMA para PSRAM
(`SOC_SDMMC_PSRAM_DMA_CAPABLE` não existe no S3) e divide a leitura em comandos
de um bloco de 512 B, com um buffer temporário. Cada página de 32 KB virava 64
comandos. Com a opção, `gamepak_read()` lê com `read()` em blocos de 8 KB para
um buffer interno com DMA e copia para a PSRAM.

## Builds

Mesmo HEAD; só a opção difere. `execute_arm` idêntico ao da v8 nos dois
(`paths_elf.py`). IRAM, DRAM e BSS iguais; o buffer é alocado no heap interno.

## Console (2026-10-14)

`console-controle.txt` e `console-candidata.txt`, Fire Red, OC 0, frameskip 0,
transições de mapa, prédios e batalhas. A coleta foi prejudicada por um defeito
intermitente do botão DOWN (GPIO45), atribuído a mau contato na PCB: o roteiro
não é idêntico entre os dois, mas o custo por falta não depende dele.

| | faltas | tempo de ROM | por falta | carga da ROM no boot |
|---|---:|---:|---:|---:|
| controle | 40 | 2.332 ms | **58,3 ms** | 10.920 ms |
| candidata | 36 | 820 ms | **22,8 ms** | 4.345 ms |

Por janela, o controle fica entre 57,3 e 60,0 ms por falta e a candidata entre
22,5 e 23,4 ms. O jogo rodou até o fim da coleta nos dois firmwares; save/load
não foi verificado separadamente.

## Decisão

**Aceita como performance v9** (`gbsp/profiles/performance-v9.sdkconfig`). Cada
falta de página custa 61% menos (cerca de 1,4 quadro em vez de 3,5 a 60 fps), e a
abertura do jogo fica 6,6 s mais rápida.

Próximos passos possíveis no mesmo caminho: manter o CS do SD selecionado
(`RG_STORAGE_SDSPI_HOLD_CS`, issue esp-idf#10493; o SD não divide o barramento
com o display neste alvo) e testar o SD a 40 MHz.
