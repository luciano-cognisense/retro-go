#ifndef GBSP_CPSR_SYNC_H
#define GBSP_CPSR_SYNC_H

// Include after common.h. Only selects WHERE the flags are synchronised;
// it never changes how a flag is computed.
//
// Inside execute_arm() the authoritative copy of N/Z/C/V lives in the locals
// n_flag/z_flag/c_flag/v_flag. reg[REG_CPSR] bits 28-31 are a cache of them:
// collapse_flags() refreshes the cache from the locals, extract_flags()
// reloads the locals from the cache.
//
// Default policy (eager): the cache is refreshed once per interpreted
// instruction, at the top of arm_loop and thumb_loop. Nothing below is
// compiled in and the token stream at every affected site is the original one.
//
// GBSP_CPSR_CONSUMER_SYNC: every site that READS reg[REG_CPSR] bits 28-31, or
// read-modify-writes reg[REG_CPSR] while having to keep the flags, collapses
// for itself. Complete list of such sites, from a full sweep of the component:
//   MRS      arm_psr_read                  already collapses (unconditional)
//   MSR      arm_psr_store_cpsr            collapse added here
//   SWI ARM  case 0xF0 ... 0xFF            already collapses (unconditional)
//   SWI Thmb case 0xDF                     already collapses (unconditional)
//   BX  T->A case 0x47                     already collapses (unconditional)
//   BX  A->T case 0x12                     collapse added here
//   update_gba() call boundaries           already collapse (CPSR -> SPSR_irq
//                                          in check_and_raise_interrupts)
//   alert: label (halt / IRQ exit)         already collapses
// Adding these is idempotent while the eager collapse is still in place, so
// this switch alone must not change behaviour. That is what makes it testable
// on its own, before anything is removed.
//
// GBSP_CPSR_LAZY_DISPATCH: drops the per-instruction collapse at the top of
// arm_loop/thumb_loop, leaving the consumer collapses as the only ones. It
// requires GBSP_CPSR_CONSUMER_SYNC and is rejected without it.
//
// Everything else in the component that touches reg[REG_CPSR] reads only bits
// 0-7, which collapse_flags() preserves: bit 7 in check_interrupt() and
// check_for_interrupts(), bit 5 in arm_spsr_restore(), in the loop entry test
// and in the misaligned-read macros of gba_memory.c, and the mode bits in
// exec_arm_block_mem() and cpu_modes[]. process_cheats() does not read reg[]
// at all. Every return from execute_arm() is preceded by one of the boundaries
// listed above, so the frontend and cpu_write_savestate() always observe a
// coherent CPSR.

#if defined(RETRO_GO) && defined(CONFIG_GBSP_CPSR_LAZY_DISPATCH) && \
    CONFIG_GBSP_CPSR_LAZY_DISPATCH && !defined(HAVE_DYNAREC)
  #if !defined(CONFIG_GBSP_CPSR_CONSUMER_SYNC) || !CONFIG_GBSP_CPSR_CONSUMER_SYNC
    #error "GBSP_CPSR_LAZY_DISPATCH requires GBSP_CPSR_CONSUMER_SYNC"
  #endif
  #define GBSP_CPSR_LAZY_ACTIVE 1
  #define collapse_flags_dispatch()
#else
  #define GBSP_CPSR_LAZY_ACTIVE 0
  #define collapse_flags_dispatch() collapse_flags()
#endif

#if defined(RETRO_GO) && defined(CONFIG_GBSP_CPSR_CONSUMER_SYNC) && \
    CONFIG_GBSP_CPSR_CONSUMER_SYNC && !defined(HAVE_DYNAREC)
  #define GBSP_CPSR_CONSUMER_ACTIVE 1
  #define collapse_flags_consumer() collapse_flags()
#else
  #define GBSP_CPSR_CONSUMER_ACTIVE 0
  #define collapse_flags_consumer()
#endif

#define GBSP_CPSR_SYNC_IMPL                                                   \
  (GBSP_CPSR_LAZY_ACTIVE ? "lazy" :                                           \
   GBSP_CPSR_CONSUMER_ACTIVE ? "eager+consumer" : "eager")

#endif
