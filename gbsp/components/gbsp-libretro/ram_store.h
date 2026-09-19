#ifndef GBSP_RAM_STORE_H
#define GBSP_RAM_STORE_H

// Include after common.h. Opt-in experiment: no extra RAM or memory map.
// Never bypass the original handlers when code translation/SMC is enabled.
#if defined(RETRO_GO) && defined(CONFIG_GBSP_FAST_RAM_WRITES) && \
    CONFIG_GBSP_FAST_RAM_WRITES && !defined(HAVE_DYNAREC) && !SMC_DETECTION
#define GBSP_FAST_RAM_WRITES_ACTIVE 1
#define GBSP_FAST_RAM_WRITES_IMPL "mapped-v2"
// 0x02000000..0x03ffffff only. init_memory maps every work-RAM mirror
// to writable pages in the existing read map; this is NOT a general write map.
// Leave I/O, ROM, palette, OAM and VRAM on their original handlers.
#define gbsp_store_memory(size, addr, value) do {                            \
  const u32 ram_store_address = (addr);                                      \
  if ((ram_store_address >> 25) == 1) {                                     \
    u8 *ram_store_page = memory_map_read[ram_store_address >> 15];            \
    address##size(ram_store_page, ram_store_address & 0x7FFF) =              \
        eswap##size((u##size)(value));                                       \
  } else {                                                                  \
    cpu_alert |= write_memory##size(ram_store_address, (value));             \
  }                                                                         \
} while (0)
#else
#define GBSP_FAST_RAM_WRITES_ACTIVE 0
#define GBSP_FAST_RAM_WRITES_IMPL "original"
#define gbsp_store_memory(size, addr, value) \
  do { cpu_alert |= write_memory##size((addr), (value)); } while (0)
#endif

#endif
