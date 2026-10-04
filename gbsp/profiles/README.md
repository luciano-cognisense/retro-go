# gbsp build profiles

`./rg_tool.py clean` deletes `gbsp/sdkconfig`, after which the target's global
sdkconfig provides the defaults again (32 KB data cache, experimental options
off). A profile here is a complete, versioned sdkconfig for the gbsp app that
its build script copies over `gbsp/sdkconfig` before every build, so the result
is reproducible after a clean. Only gbsp is affected; the launcher and the
other cores keep the target defaults.

## performance-v1

```sh
gbsp/profiles/build-performance-v1.sh
```

Needs an exported ESP-IDF 5.3.1 environment, like `./rg_tool.py`. The script
checks the required options in the profile, in `gbsp/sdkconfig` and in the
generated `sdkconfig.h`, builds with
`./rg_tool.py build gbsp --target=esp32-s3-devkit --no-networking`, checks the
partition size (0xd0000), and stops at the first error.

Enabled: fast RAM writes, CPSR consumer sync + lazy dispatch, FETCH_PC,
NO_CHEAT_HOOK, CPU_RODATA_DRAM, 64 KB data cache (32 B line, 8 ways), PSRAM
80 MHz. Disabled: memory profile, compact reads, sparse registers.

- **Cheats are unavailable** in this variant (NO_CHEAT_HOOK).
- 64 KB data cache applies to gbsp only.
- Measured on the target hardware: ~108 KB internal RAM free at boot,
  largest block 48 KB.

Both Kconfig options keep `default n`; this profile turns them on explicitly.
