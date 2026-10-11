#ifndef GBSP_READ_PATH_H
#define GBSP_READ_PATH_H

// Include after common.h. Only the interpreter's scalar data loads change.
#if defined(RETRO_GO) && defined(CONFIG_GBSP_COMPACT_READS) && \
    CONFIG_GBSP_COMPACT_READS && !defined(HAVE_DYNAREC)
#define GBSP_COMPACT_READS_ACTIVE 1
// One range guard protects BOTH timing-table and map indexing. The low-bit
// alignment test no longer repeats the high-address rejection. Keep each
// fallback's original handler, including signed unaligned halfword semantics.
#define gbsp_compact_read(size, type, addr, dest, readfn)                      \
{                                                                            \
    do {                                                                    \
    const u32 _address = (addr);                                             \
    if (_address < 0x10000000) {                                             \
        const u8 region = _address >> 24;                                    \
        cycles_remaining -= WS_NSEQ(region, (size - 8) / 16);             \
        STATS_MEMORY_ACCESS(read, type, region);                             \
        u8 *map;                                                            \
        if (!(_address & (aligned_address_mask##size & 3)) &&                \
            !(region == 0 && reg[REG_PC] >= 0x4000) &&                        \
            (map = memory_map_read[_address >> 15])) {                       \
            dest = (type)readaddress##size(map, _address & 0x7FFF);           \
            break;                                                          \
        }                                                                   \
    }                                                                       \
    dest = (type)(readfn)(_address);                                         \
    } while (0);                                                            \
}
#else
#define GBSP_COMPACT_READS_ACTIVE 0
#endif
#endif
