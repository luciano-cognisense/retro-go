#pragma once

// Display task activity, for callers that need to know whether the display
// task may still be using the CPU for a frame, not only whether a frame is
// queued (see rg_display_is_busy(), whose "queue not empty" contract several
// cores rely on for frameskip and which is left unchanged).
//
// Two counters, each written by one side only:
//   submitted: frames handed to the display task. Incremented by the
//              submitter before the message is queued.
//   completed: frames the display task has finished. Incremented by the
//              display task only after write_update(), removing the message
//              and lcd_sync() have all returned.
// active == (completed != submitted). A frame therefore counts as active from
// before it is queued until after lcd_sync(): there is no window, between the
// queue and the task, in which a frame in progress reads as idle.
//
// Reading is one aligned 32-bit load of `completed` (written by the display
// core) and a compare: no FreeRTOS call. The cost to everyone else is one
// increment per submitted frame and one store per displayed frame.

#include <stdbool.h>
#include <stdint.h>

// Compiled in only when an application asks for it (CONFIG_RG_DISPLAY_ACTIVITY,
// selected for instance by gbsp's hybrid render). Without it rg_display.c has
// no counters, no stores and no barriers for this, and rg_display_is_active()
// is not defined.
#if defined(ESP_PLATFORM)
#include <sdkconfig.h>
#endif
#if defined(CONFIG_RG_DISPLAY_ACTIVITY) && CONFIG_RG_DISPLAY_ACTIVITY
#define RG_DISPLAY_ACTIVITY_ENABLED 1
#else
#define RG_DISPLAY_ACTIVITY_ENABLED 0
#endif

typedef struct
{
    volatile uint32_t submitted;
    volatile uint32_t completed;
} rg_display_activity_t;

#if defined(ESP_PLATFORM)
// Internal SRAM, no data cache in between: aligned 32-bit accesses are atomic
// and MEMW orders them across the two cores.
#define RG_DISPLAY_ACTIVITY_FENCE() __asm__ __volatile__("memw" ::: "memory")
#define RG_DISPLAY_ACTIVITY_LOAD(v) (v)
#define RG_DISPLAY_ACTIVITY_STORE(v, x) ((v) = (x))
#else
#define RG_DISPLAY_ACTIVITY_FENCE() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define RG_DISPLAY_ACTIVITY_LOAD(v) __atomic_load_n(&(v), __ATOMIC_SEQ_CST)
#define RG_DISPLAY_ACTIVITY_STORE(v, x) __atomic_store_n(&(v), (x), __ATOMIC_SEQ_CST)
#endif

// Submitter side, before the frame message is queued. Frames are submitted by
// one task at a time (the application's main loop), so this is not a
// concurrent read-modify-write.
static inline void rg_display_activity_submitted(rg_display_activity_t *a)
{
    RG_DISPLAY_ACTIVITY_STORE(a->submitted, RG_DISPLAY_ACTIVITY_LOAD(a->submitted) + 1);
    RG_DISPLAY_ACTIVITY_FENCE();
}

// Display task side, after lcd_sync() for that frame.
static inline void rg_display_activity_completed(rg_display_activity_t *a)
{
    RG_DISPLAY_ACTIVITY_FENCE();
    RG_DISPLAY_ACTIVITY_STORE(a->completed, RG_DISPLAY_ACTIVITY_LOAD(a->completed) + 1);
}

static inline bool rg_display_activity_is_active(rg_display_activity_t *a)
{
    RG_DISPLAY_ACTIVITY_FENCE();
    return RG_DISPLAY_ACTIVITY_LOAD(a->completed) != RG_DISPLAY_ACTIVITY_LOAD(a->submitted);
}
