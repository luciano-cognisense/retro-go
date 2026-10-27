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

## performance-v3

```sh
gbsp/profiles/build-performance-v3.sh
```

Same mechanism and checks as v2. The profile is v2 plus one option:

- `CONFIG_GBSP_AUDIO_CLOCK_COMP` (ESP32-S3 only): on the S3 the I2S clock
  is BBPLL/3 and the retro-go overclock raises the BBPLL, so without this
  option a programmed 32768 Hz plays ~38230 samples/s at oc 2. With it,
  audio sync programs its rate scaled by nominal/actual CPU clock, measured
  once per overclock change on the way out of the menu (CCOUNT against the
  crystal-fed `esp_timer`). Nothing changes at oc 0, an implausible
  measurement falls back to the uncompensated rate, and it applies with Audio
  sync on and off. No per-frame work.

Everything in v2 stays as it was: render on core 1 (hybrid, adaptive),
FAST_RAM_WRITES, CPSR consumer sync + lazy dispatch, FETCH_PC,
NO_CHEAT_HOOK, CPU_RODATA_DRAM, 64 KB data cache, PSRAM 80 MHz.

- Overclock and frameskip remain configurable in the menus.
- **Cheats are unavailable**, as in v1 and v2 (NO_CHEAT_HOOK).
- Not part of this firmware: the audio pacing statistics, the I2S event
  counting and the transition guard explored afterwards. Those options do not
  exist in this tree.
- Measured on the target hardware at oc 2 (with the statistics build of the
  same code): 32768 Hz requested -> 28087 programmed, ~32768 samples/s
  actually consumed; at most 60 fps sustained (61-69 fps before); audio judged
  more natural.
- Scenes that run below 100% still slow the sound down with them, and when the
  load changes the rate can take a few seconds to settle; the compensation does
  not remove those artifacts.

The option keeps `default n`; only this profile turns it on.

## performance-v4

```sh
gbsp/profiles/build-performance-v4.sh
```

Same mechanism and checks as v3. The profile is v3 plus one option:

- `CONFIG_GBSP_CPU_ALERT_PER_INSTRUCTION`: `execute_arm()` resets
  `cpu_alert` at the start of every interpreted ARM and Thumb instruction.
  Only the instruction that sets HALT/IRQ can make the end-of-instruction
  test fire, so this is equivalent, and it lets the compiler drop the
  `cpu_alert` stack slot and that test wherever a handler cannot set it.
  In the measured build: `execute_arm` frame 112 -> 96 B, IRAM -1924 B.

Everything in v3 stays as it was: I2S clock compensation, render on core 1
(hybrid, adaptive), FAST_RAM_WRITES, CPSR consumer sync + lazy dispatch,
FETCH_PC, NO_CHEAT_HOOK, CPU_RODATA_DRAM, 64 KB data cache, PSRAM 80 MHz.

- Overclock and frameskip remain configurable in the menus.
- **Cheats are unavailable**, as in v1-v3 (NO_CHEAT_HOOK).
- Measured on the target hardware (Mario and Pokémon Fire Red, oc 0,
  frameskip 0): emu time went down in both games and nothing regressed
  functionally (boot, play, save/load). There is no official percentage:
  the moving scenes of the two runs were similar but not identical.
- 110 KB internal RAM free at boot, largest block 50 KB (108/48 KB before).

The option keeps `default n`; only this profile turns it on.

## performance-v4.1

```sh
gbsp/profiles/build-performance-v4.1.sh
```

Maintenance release of v4 with the same effective sdkconfig and performance
options. It adds the functional savestate fix that rebuilds
`ws_cyc_seq`/`ws_cyc_nseq` from the restored `WAITCNT` after loading a state.
The savestate format is unchanged, and normal execution outside load state is
identical to v4. Performance is therefore expected to be identical to v4.

## performance-v5

```sh
gbsp/profiles/build-performance-v5.sh
```

Same mechanism and checks as v4.1. The profile is v4.1 plus one option:

- `CONFIG_GBSP_THUMB_RESIDENT_PC`: in the Thumb loop of `execute_arm()` the
  PC stays in a register from one instruction to the next. Every PC write
  still goes to `reg[REG_PC]` (helpers and `Rs = PC` read it there), so only
  the two PC loads per Thumb instruction go away; the copy is reloaded on
  every jump into the loop and after the block transfer helpers. The ARM
  loop is unchanged. In the measured build: IRAM +588 B, same `execute_arm`
  frame (96 B), no new stack spill.

Everything in v4.1 stays as it was, including the savestate waitstate fix.

- Overclock and frameskip remain configurable in the menus.
- **Cheats are unavailable**, as in v1-v4.1 (NO_CHEAT_HOOK).
- Measured on the target hardware on still scenes (oc 0, frameskip 0):
  Pokémon Fire Red emu -1.0%, CPU-side time -1.3%, total -1.0%; Mario emu
  neutral, CPU-side time -0.5%. Save/load and play worked in both games.

The option keeps `default n`; only this profile turns it on.

## performance-v6

```sh
gbsp/profiles/build-performance-v6.sh
```

Same mechanism and checks as v5. The profile is v5 plus one option:

- `CONFIG_GBSP_ROM_SECOND_CHANCE`: the ROM page cache (32 KB pages in 1 MB
  blocks; 6 MB on the device) is a circular FIFO that only skips the pages
  marked sticky in the current frame, so a page whose code runs often is
  still evicted in its turn. With the option, the sticky bits are ORed once
  per frame into a 128-byte reference bitmap, and the eviction sends a
  non-sticky page with its reference bit to the tail once, clearing the bit.
  The bitmap (and the previous game's sticky bits) is cleared on every ROM
  load. The hit path, `touch_gamepak_page()` and the CPU loop are unchanged.
  In the measured build: BSS +128 B, flash +88 B, IRAM and `execute_arm`
  frame unchanged; about 230 instructions per frame for the bitmap update.

Everything in v5 stays as it was, including the resident Thumb PC.

- Overclock and frameskip remain configurable in the menus.
- **Cheats are unavailable**, as in v1-v5 (NO_CHEAT_HOOK).
- Measured on the target hardware (oc 0, frameskip 0): Pokémon Fire Red with
  map transitions, battles, buildings and save/load had 40% fewer ROM page
  faults and 40% less ROM read time (38.7% fewer faults per 1000 frames),
  with the cost per fault unchanged (~58 ms per 32 KB page). Mario fits the
  cache and has no page faults. No cache size change and no prefetch.

The option keeps `default n`; only this profile turns it on.

## performance-v7

```sh
gbsp/profiles/build-performance-v7.sh
```

Same mechanism and checks as v6. The profile is v6 plus one option:

- `CONFIG_GBSP_AUDIO_LIVE_RATE`: keeps the accepted K audio-rate policy, but
  applies each decision by updating the ESP32-S3 external-I2S TX clock divider
  while the channel remains active. This preserves the DMA descriptor position
  and queued samples instead of stopping and restarting the channel. Other
  targets and audio devices retain the legacy operation.

Everything in v6 stays as it was, including ROM-cache second chance. Diagnostic
transition telemetry remains disabled in this profile.

- Overclock and frameskip remain configurable in the menus.
- **Cheats are unavailable**, as in v1-v6 (`NO_CHEAT_HOOK`).
- Measured on the target hardware: all sampled rate changes completed with zero
  DMA restarts. In Fire Red, transition windows had about 27% fewer underruns
  and 18% less `i2s_write` time in non-identical sessions, while stable-window
  underruns stayed essentially unchanged. The sound was judged more natural.

The option keeps `default n`; only this profile turns it on.

## performance-v8

```sh
gbsp/profiles/build-performance-v8.sh
```

Same mechanism and checks as v7. The profile is v7 plus one option:

- `CONFIG_GBSP_WAITSTATE_COMMON_BASE`: stores the sequential and
  non-sequential waitstate tables in one contiguous object. GCC then keeps a
  common base across the Thumb dispatch tails, removing about one stack access
  and 1.15 Xtensa instructions per measured Thumb instruction. The ARM path
  has a small instruction/stack tradeoff. In the final build, the
  `execute_arm` frame grows from 96 to 112 B and IRAM grows by 88 B; DRAM is
  unchanged.

Everything in v7 stays as it was, including live I2S rate changes and ROM-cache
second chance. Diagnostic telemetry remains disabled.

- Overclock and frameskip remain configurable in the menus.
- **Cheats are unavailable**, as in v1-v7 (`NO_CHEAT_HOOK`).
- Measured on the target hardware (oc 0, frameskip 0), in stable scenes:
  CPU-side time fell 1.21% in Pokémon Fire Red and 1.07% in Mario. Emulation
  time fell 0.97% and 0.60%, respectively. Save/load and normal play worked in
  both games, with no panic or watchdog.

The option keeps `default n`; only this profile turns it on.

## performance-v9

```sh
gbsp/profiles/build-performance-v9.sh
```

Same mechanism and checks as v8. The profile is v8 plus one option:

- `CONFIG_GBSP_ROM_BOUNCE_READ` (8 KB buffer, `CONFIG_GBSP_ROM_BOUNCE_KB`):
  the ROM page cache lives in PSRAM, and on the ESP32-S3 the SD/MMC layer
  cannot DMA into PSRAM, so `sdmmc_read_sectors()` split every 32 KB page into
  64 single-block commands. The ROM file is now read with `read()` into an
  internal DMA-capable buffer and copied to PSRAM, one multi-block command per
  8 KB. This covers page faults and the initial load of the cache. Without the
  buffer (allocation failure) the stdio path is used as before. The CPU loop is
  unchanged (`execute_arm` identical to v8); 8 KB of internal RAM.

Everything in v8 stays as it was. The profile log header now also records the
boot ROM load time and the bounce buffer size (in every build, option or not).

- Overclock and frameskip remain configurable in the menus.
- **Cheats are unavailable**, as in v1-v8 (`NO_CHEAT_HOOK`).
- Measured on the target hardware (oc 0, frameskip 0, Pokémon Fire Red with map
  transitions, buildings and battles): cost per ROM page fault 58.3 -> 22.8 ms
  (40 and 36 faults, every window between 22.5 and 23.4 ms), ROM read time per
  fault -61%; boot load of the 6 MB cache 10.9 -> 4.3 s. The game ran through
  both collections; save/load was not checked separately.

The option keeps `default n`; only this profile turns it on.

## performance-v10

```sh
gbsp/profiles/build-performance-v10.sh
```

Same mechanism and checks as v9. The profile changes only:

- `CONFIG_GBSP_ROM_BOUNCE_KB=16`: each 32 KB ROM-cache page is read through two
  16 KB DMA-capable internal-buffer operations instead of four 8 KB operations.
  SD remains at the 20 MHz default, CS behavior is unchanged, and the ROM-cache
  replacement policy is unchanged. This costs 8 KB more dynamic internal heap
  while a game is open; static RAM, IRAM and the CPU loop are unchanged.

Measured on the target hardware at OC 0 and frameskip 0: Fire Red initial load
4347 -> 4101 ms (-5.66%), Mario 2905 -> 2737 ms (-5.78%), and Fire Red ROM-page
fault cost 22.800 -> 21.503 ms (-5.69%, 40 candidate faults). No storage or
runtime error appeared in the log.

Everything else in v9 stays unchanged. Overclock and frameskip remain
configurable, cheats remain unavailable, and diagnostic telemetry remains off.

## performance-v11

```sh
gbsp/profiles/build-performance-v11.sh
```

Same mechanism and checks as v10. The profile adds only:

- `CONFIG_GBSP_ROM_CACHE_512K_TAIL`: after the normal sequence of 1 MB PSRAM
  allocations stops, the core tries one final 512 KB block. The ROM page size
  remains 32 KB, and the cache-hit path is unchanged. The variable-block lookup
  runs only on a page fault.

Measured on the target hardware at OC 0 and frameskip 0, the allocation
succeeded: exact cache size 6656 KB, including a 512 KB tail. A 22,803-frame
Fire Red session completed without panic or storage errors, with 65 faults at
21.48 ms/fault. Its fault rate was 10.26/min, 22.6% below the previous
non-identical v10 reference. Loading the extra cache data added about 342 ms to
startup, so the main benefit is fewer transition stalls in longer sessions.

Everything else in v10 stays unchanged. Overclock and frameskip remain
configurable, cheats remain unavailable, and diagnostic telemetry remains off.
