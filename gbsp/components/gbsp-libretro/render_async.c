#include "common.h"
#include "render_async.h"

#if GBSP_RENDER_ASYNC_ACTIVE

// See render_async.h for the ownership rule. Synchronisation is a single
// "pending" flag plus a "main is waiting" flag (Dekker style, so a wakeup can
// never be lost), and one notification per direction. No allocation happens
// after gbsp_render_async_start(); no lock is taken on the hot path.

u16 *gbsp_video_io = io_registers;
volatile u32 gbsp_render_pending;

static u16 ra_io_copy[GBSP_RENDER_IO_HALFWORDS];
static volatile u32 ra_main_waiting;
static volatile u32 ra_stop;
static volatile u32 ra_stopped;
static bool ra_running;

#if GBSP_RENDER_ASYNC_STATS
static u32 ra_jobs, ra_waits, ra_waits_submit, ra_wait_ticks;   // core 0 only
static volatile u32 ra_worker_ticks;           // core 1 only, monotonic
static u32 ra_worker_ticks_seen;
#endif

#ifdef ESP_PLATFORM
// ---------------------------------------------------------------- device --
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_cpu.h>

// Internal SRAM, no data cache in between: aligned 32-bit loads and stores
// are atomic, and MEMW orders them across the two cores.
#define RA_LOAD(v)      (v)
#define RA_STORE(v, x)  ((v) = (x))
#define RA_FENCE()      __asm__ __volatile__("memw" ::: "memory")
#define RA_TICKS()      esp_cpu_get_cycle_count()

// Core 1 already runs rg_display and rg_input at priority 6 and may run
// rg_sysmon (priority 5, no affinity). The worker sits below all of them: it
// can lose time to the display, never take it.
#define RA_CORE         1
#define RA_PRIORITY     4
#define RA_STACK_BYTES  4096
#define RA_WAIT_TICKS   pdMS_TO_TICKS(100)

static TaskHandle_t ra_worker_task;
static TaskHandle_t ra_main_task;

static void ra_wake_worker(void) { xTaskNotifyGive(ra_worker_task); }
static void ra_wake_main(void)   { xTaskNotifyGive(ra_main_task); }
static void ra_block_worker(void) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }
static void ra_block_main(void)   { ulTaskNotifyTake(pdTRUE, RA_WAIT_TICKS); }
static void ra_sleep_short(void)  { vTaskDelay(1); }

#else
// ------------------------------------------------------------------ host --
// Only for the host test harness (ThreadSanitizer, determinism tests).
#include <pthread.h>
#include <sched.h>

#define RA_LOAD(v)      __atomic_load_n(&(v), __ATOMIC_SEQ_CST)
#define RA_STORE(v, x)  __atomic_store_n(&(v), (x), __ATOMIC_SEQ_CST)
#define RA_FENCE()      __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define RA_TICKS()      0u

typedef struct { pthread_mutex_t m; pthread_cond_t c; int token; } ra_token_t;
static ra_token_t ra_tok_worker = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0 };
static ra_token_t ra_tok_main = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0 };
static pthread_t ra_thread;

static void ra_give(ra_token_t *t)
{
  pthread_mutex_lock(&t->m); t->token = 1; pthread_cond_signal(&t->c); pthread_mutex_unlock(&t->m);
}
static void ra_take(ra_token_t *t)
{
  pthread_mutex_lock(&t->m);
  while (!t->token) pthread_cond_wait(&t->c, &t->m);
  t->token = 0;
  pthread_mutex_unlock(&t->m);
}
static void ra_wake_worker(void) { ra_give(&ra_tok_worker); }
static void ra_wake_main(void)   { ra_give(&ra_tok_main); }
static void ra_block_worker(void) { ra_take(&ra_tok_worker); }
static void ra_block_main(void)   { ra_take(&ra_tok_main); }
static void ra_sleep_short(void)  { sched_yield(); }

// Host-only test hook: extra delay inside each job, to widen race windows.
void (*gbsp_render_test_job_hook)(void);
#endif

static void ra_worker_loop(void)
{
  for (;;)
  {
    ra_block_worker();
    if (RA_LOAD(ra_stop))
      break;
    if (!RA_LOAD(gbsp_render_pending))
      continue;                       // stale notification

#if GBSP_RENDER_ASYNC_STATS
    u32 t0 = RA_TICKS();
#endif
    gbsp_video_io = ra_io_copy;
#ifndef ESP_PLATFORM
    if (gbsp_render_test_job_hook)
      gbsp_render_test_job_hook();
#endif
    update_scanline();
    gbsp_video_io = io_registers;
#if GBSP_RENDER_ASYNC_STATS
    ra_worker_ticks += RA_TICKS() - t0;
#endif

    RA_FENCE();
    RA_STORE(gbsp_render_pending, 0);
    RA_FENCE();
    if (RA_LOAD(ra_main_waiting))
      ra_wake_main();
  }
  RA_STORE(ra_stopped, 1);
}

#ifdef ESP_PLATFORM
static void ra_worker_entry(void *arg)
{
  (void)arg;
  ra_worker_loop();
  vTaskDelete(NULL);
}
#else
static void *ra_worker_entry(void *arg)
{
  (void)arg;
  ra_worker_loop();
  return NULL;
}
#endif

bool gbsp_render_async_start(void)
{
  if (ra_running)
    return true;
  RA_STORE(gbsp_render_pending, 0);
  RA_STORE(ra_main_waiting, 0);
  RA_STORE(ra_stop, 0);
  RA_STORE(ra_stopped, 0);
  gbsp_video_io = io_registers;
#ifdef ESP_PLATFORM
  ra_main_task = xTaskGetCurrentTaskHandle();
  if (xTaskCreatePinnedToCore(ra_worker_entry, "gbsp_render", RA_STACK_BYTES, NULL,
                              RA_PRIORITY, &ra_worker_task, RA_CORE) != pdPASS)
  {
    ra_worker_task = NULL;
    return false;                     // caller keeps the synchronous path
  }
#else
  if (pthread_create(&ra_thread, NULL, ra_worker_entry, NULL) != 0)
    return false;
#endif
  ra_running = true;
  return true;
}

void gbsp_render_async_stop(void)
{
  if (!ra_running)
    return;
  gbsp_render_barrier();              // never leave a job half done
  RA_STORE(ra_stop, 1);
  RA_FENCE();
  ra_wake_worker();
#ifdef ESP_PLATFORM
  while (!RA_LOAD(ra_stopped))
    ra_sleep_short();
  ra_worker_task = NULL;
#else
  pthread_join(ra_thread, NULL);
#endif
  ra_running = false;
  gbsp_video_io = io_registers;
}

bool gbsp_render_async_running(void)
{
  return ra_running;
}

void gbsp_render_async_submit(void)
{
  if (!ra_running)
  {
    update_scanline();
    return;
  }
  // update_scanline() returns before touching anything on a skipped frame.
  if (skip_next_frame)
    return;

#if GBSP_RENDER_ASYNC_STATS
  if (RA_LOAD(gbsp_render_pending))
    ra_waits_submit++;
#endif
  gbsp_render_barrier();              // depth 1: the previous line is done
  for (unsigned i = 0; i < GBSP_RENDER_IO_HALFWORDS; i++)
    ra_io_copy[i] = io_registers[i];
#if GBSP_RENDER_ASYNC_STATS
  ra_jobs++;
#endif
  RA_FENCE();
  RA_STORE(gbsp_render_pending, 1);
  RA_FENCE();
  ra_wake_worker();
}

void gbsp_render_async_wait(void)
{
#if GBSP_RENDER_ASYNC_STATS
  u32 t0 = RA_TICKS();
  ra_waits++;
#endif
  RA_STORE(ra_main_waiting, 1);
  RA_FENCE();
  while (RA_LOAD(gbsp_render_pending))
    ra_block_main();                  // bounded timeout on device, then re-check
  RA_STORE(ra_main_waiting, 0);
  RA_FENCE();
#if GBSP_RENDER_ASYNC_STATS
  ra_wait_ticks += RA_TICKS() - t0;
#endif
}

#if GBSP_RENDER_ASYNC_STATS
void gbsp_render_stats_take(gbsp_render_stats_t *out)
{
  out->jobs = ra_jobs;
  out->waits = ra_waits;
  out->waits_submit = ra_waits_submit;
  out->wait_ticks = ra_wait_ticks;
  u32 now = ra_worker_ticks;            // written by core 1 only: aligned, tear-free
  out->worker_ticks = now - ra_worker_ticks_seen;
  ra_worker_ticks_seen = now;
#ifdef ESP_PLATFORM
  out->stack_free = ra_worker_task ? uxTaskGetStackHighWaterMark(ra_worker_task) : 0;
#else
  out->stack_free = 0;
#endif
  ra_jobs = ra_waits = ra_waits_submit = ra_wait_ticks = 0;
}
#endif

#endif
