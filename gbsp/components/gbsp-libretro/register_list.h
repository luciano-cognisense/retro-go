#ifndef GBSP_REGISTER_LIST_H
#define GBSP_REGISTER_LIST_H

// Include after common.h. Ascending register order is architecturally relevant.
#if defined(RETRO_GO) && defined(CONFIG_GBSP_SPARSE_REGISTERS) && \
    CONFIG_GBSP_SPARSE_REGISTERS && !defined(HAVE_DYNAREC)
#define GBSP_SPARSE_REGISTERS_ACTIVE 1
// count is 8 or 16. Short-circuiting protects ctz from zero. Each step clears
// the lowest set bit; the expression is a local copy, not the writeback list.
#define gbsp_for_register(i, list, count)                                    \
    for (u32 gbsp_pending_##i = (list) & ((1U << (count)) - 1), i = 0;        \
         gbsp_pending_##i && ((i = __builtin_ctz(gbsp_pending_##i)), true);   \
         gbsp_pending_##i &= gbsp_pending_##i - 1)
#else
#define GBSP_SPARSE_REGISTERS_ACTIVE 0
#define gbsp_for_register(i, list, count)                                    \
    for (u32 i = 0; i < (count); ++i)                                        \
        if (((list) >> i) & 1U)
#endif
#endif
