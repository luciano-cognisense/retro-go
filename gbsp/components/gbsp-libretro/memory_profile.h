#ifndef GBSP_MEMORY_PROFILE_H
#define GBSP_MEMORY_PROFILE_H

// Include after common.h: u32 must match the core type exactly on Xtensa.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(RETRO_GO) && defined(CONFIG_GBSP_MEMORY_PROFILE) && \
    CONFIG_GBSP_MEMORY_PROFILE && !defined(HAVE_DYNAREC)
#define GBSP_MEMORY_PROFILE_ACTIVE 1
#ifndef MEMORY_STATS_ANALYZE
#define MEMORY_STATS_ANALYZE
#endif

// Defined by cpu_instrument.h in cpu.cpp. All access, reset and output happen
// on the emulator task; no cross-core snapshots or locks are needed.
extern u32 memory_region_access_read_u8[16];
extern u32 memory_region_access_read_s8[16];
extern u32 memory_region_access_read_u16[16];
extern u32 memory_region_access_read_s16[16];
extern u32 memory_region_access_read_u32[16];
extern u32 memory_region_access_write_u8[16];
extern u32 memory_region_access_write_u16[16];
extern u32 memory_region_access_write_u32[16];

static inline void gbsp_memory_profile_reset(void)
{
    memset(memory_region_access_read_u8, 0, sizeof(memory_region_access_read_u8));
    memset(memory_region_access_read_s8, 0, sizeof(memory_region_access_read_s8));
    memset(memory_region_access_read_u16, 0, sizeof(memory_region_access_read_u16));
    memset(memory_region_access_read_s16, 0, sizeof(memory_region_access_read_s16));
    memset(memory_region_access_read_u32, 0, sizeof(memory_region_access_read_u32));
    memset(memory_region_access_write_u8, 0, sizeof(memory_region_access_write_u8));
    memset(memory_region_access_write_u16, 0, sizeof(memory_region_access_write_u16));
    memset(memory_region_access_write_u32, 0, sizeof(memory_region_access_write_u32));
}

static inline void gbsp_memory_profile_write(FILE *fp, unsigned frames)
{
    // Raw totals for the preceding frame window, not per-frame averages.
    // Keep signed loads separate: s8/s16 are instruction semantics, not bytes.
    // These are interpreter DATA operations, never instruction fetch or DMA.
    fprintf(fp, "mem_window frames=%u scope=interpreter_data timing=diagnostic\n", frames);
    for (unsigned region = 0; region < 16; ++region)
        fprintf(fp, "mem region=%02x r8=%u rs8=%u r16=%u rs16=%u r32=%u w8=%u w16=%u w32=%u\n",
                region,
                (unsigned)memory_region_access_read_u8[region],
                (unsigned)memory_region_access_read_s8[region],
                (unsigned)memory_region_access_read_u16[region],
                (unsigned)memory_region_access_read_s16[region],
                (unsigned)memory_region_access_read_u32[region],
                (unsigned)memory_region_access_write_u8[region],
                (unsigned)memory_region_access_write_u16[region],
                (unsigned)memory_region_access_write_u32[region]);
}
#else
#define GBSP_MEMORY_PROFILE_ACTIVE 0
static inline void gbsp_memory_profile_reset(void) {}
static inline void gbsp_memory_profile_write(FILE *fp, unsigned frames)
{
    (void)fp;
    (void)frames;
}
#endif
#endif
