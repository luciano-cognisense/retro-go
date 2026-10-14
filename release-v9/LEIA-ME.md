# v9: gbsp + launcher

Gerados do commit `9a26df0a` (`performance/gbsp-v8`): gbsp com o perfil
performance v9 (`gbsp/profiles/build-performance-v9.sh`) e o launcher com o
carrossel final (`61317ef2`), `./rg_tool.py build launcher
--target=esp32-s3-devkit --no-networking`.

Endereços pela tabela de partições do aparelho: launcher `0x10000`
(1216K), gbsp `0x3e0000` (832K).

```
curl.exe -L -o launcher.bin https://github.com/luciano-cognisense/retro-go/raw/release/v9/release-v9/launcher.bin
curl.exe -L -o gbsp-v9.bin https://github.com/luciano-cognisense/retro-go/raw/release/v9/release-v9/gbsp.bin
certutil -hashfile launcher.bin SHA256
certutil -hashfile gbsp-v9.bin SHA256
esptool.py --chip esp32s3 --port COM13 --baud 921600 write_flash 0x10000 launcher.bin 0x3e0000 gbsp-v9.bin
```
