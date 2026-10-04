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

## performance-v2

```sh
gbsp/profiles/build-performance-v2.sh
```

Same mechanism and checks as v1. The profile is v1 plus the render on core 1:

- `CONFIG_GBSP_RENDER_CORE1`: scanlines rendered by a worker on core 1
  (priority 4, below the display and input tasks; 4 KB stack; at most one
  pending line), with barriers before every write the renderer depends on.
- `CONFIG_GBSP_RENDER_CORE1_HYBRID`: while the display task is busy with the
  previous frame, lines stay on core 0. Selects `CONFIG_RG_DISPLAY_ACTIVITY`
  (retro-go), which adds `rg_display_is_active()`; nothing else uses it.
- `CONFIG_GBSP_RENDER_CORE1_ADAPTIVE`: per frame, lines go back to core 0
  when handing them to the worker costs core 0 at least 7/8 of rendering them
  itself.
- `CONFIG_GBSP_RENDER_CORE1_STATS` stays off.

All four options keep `default n`; only this profile turns them on. Cheats
are unavailable, as in v1. Measured against the cache64 profile on the target
hardware: Mario emu -10.90% / total -9.78%; Pokémon Fire Red neutral (emu
+0.48%, total +0.04%); 108 KB internal RAM free at boot, largest block 48 KB.
