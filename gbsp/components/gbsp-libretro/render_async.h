#ifndef GBSP_RENDER_ASYNC_H
#define GBSP_RENDER_ASYNC_H

// Include after common.h (u16/u32 and io_registers).
//
// Experimental: render one scanline on core 1 while core 0 keeps emulating
// (CONFIG_GBSP_RENDER_CORE1, default n).
//
// Ownership rule. At most one scanline job is pending. While it is pending,
// the worker owns everything update_scanline() reads or writes:
//   - VRAM, OAM, palette RAM and palette_ram_converted (read);
//   - affine_reference_x/y, reg[OAM_UPDATED], the OBJ/layer ordering state
//     (read and written);
//   - the destination framebuffer row (written).
// The video I/O registers (0x00-0x5F) are NOT shared: the job carries a copy,
// so the CPU may keep writing scroll/window/blend registers and update_gba()
// may advance VCOUNT. Every other writer of the state above must call
// GBSP_RENDER_BARRIER() first, which waits for the pending job:
//   write_memory*() to palette/VRAM/OAM, write_io_register16() to DISPCNT and
//   BG2X/BG2Y/BG3X/BG3Y, dma_transfer() towards palette/VRAM/OAM,
//   video_reload_counters(), the end of a frame, reset_gba(), save/load state.
// Because the end of every frame drains the job, nothing is pending outside
// execute_arm(): the frontend (display submit, menus, screenshots, shutdown)
// never sees a job in flight.

#if defined(RETRO_GO) && defined(CONFIG_GBSP_RENDER_CORE1) && \
    CONFIG_GBSP_RENDER_CORE1 && !defined(HAVE_DYNAREC)
  #define GBSP_RENDER_ASYNC_ACTIVE 1
#else
  #define GBSP_RENDER_ASYNC_ACTIVE 0
#endif

#if GBSP_RENDER_ASYNC_ACTIVE && defined(CONFIG_GBSP_RENDER_CORE1_STATS) && \
    CONFIG_GBSP_RENDER_CORE1_STATS
  #define GBSP_RENDER_ASYNC_STATS 1
#else
  #define GBSP_RENDER_ASYNC_STATS 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if GBSP_RENDER_ASYNC_ACTIVE

// Video registers update_scanline() reads: 0x00-0x5F (highest is REG_BLDY).
#define GBSP_RENDER_IO_HALFWORDS 0x30

// Pointer video.cpp reads its I/O registers through: io_registers on the
// synchronous path, the job's copy while the worker renders.
extern u16 *gbsp_video_io;
extern volatile u32 gbsp_render_pending;

bool gbsp_render_async_start(void);   // false: stays synchronous
void gbsp_render_async_stop(void);
bool gbsp_render_async_running(void);
void gbsp_render_async_submit(void);  // replaces update_scanline() in update_gba()
void gbsp_render_async_wait(void);    // slow path of the barrier

static inline void gbsp_render_barrier(void)
{
#ifdef ESP_PLATFORM
  if (gbsp_render_pending)
#else
  if (__atomic_load_n(&gbsp_render_pending, __ATOMIC_SEQ_CST))  // host tests (TSan)
#endif
    gbsp_render_async_wait();
}

#if GBSP_RENDER_ASYNC_STATS
typedef struct
{
  u32 jobs;          // scanlines handed to the worker
  u32 waits;         // barriers that found a job still pending
  u32 waits_submit;  // ...of which at submit: worker slower than one line
  u32 wait_ticks;    // core 0 CCOUNT ticks spent blocked in those barriers
  u32 worker_ticks;  // core 1 CCOUNT ticks spent inside update_scanline()
  u32 stack_free;    // worker stack high-water mark, bytes never used
} gbsp_render_stats_t;
void gbsp_render_stats_take(gbsp_render_stats_t *out);  // read and reset
#endif

#define GBSP_RENDER_SCANLINE() gbsp_render_async_submit()
#define GBSP_RENDER_BARRIER()  gbsp_render_barrier()

#else

#define GBSP_RENDER_SCANLINE() update_scanline()
#define GBSP_RENDER_BARRIER()  do { } while (0)

#endif

#ifdef __cplusplus
}
#endif

#endif
