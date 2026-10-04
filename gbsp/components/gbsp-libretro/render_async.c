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
static u32 ra_sync_lines, ra_drawn_frames, ra_display_returns, ra_decision_ticks;
static u16 ra_first_async_hist[161];           // per window, index 160 = no async line
static int ra_frame_first_async = -1, ra_frame_lines;
static bool ra_prev_async;
#if GBSP_RENDER_ADAPTIVE
static u32 ra_sync_adaptive_lines, ra_frames_abandoned;
static u16 ra_abandon_hist[161];               // per window: line at which a frame gave up the worker
static int ra_frame_abandon_line = -1;
static u32 ra_sync_cost_sum, ra_sync_cost_n, ra_job_cost_sum, ra_job_cost_n;
static int64_t ra_est_gain_ticks;
#endif
#endif

#if GBSP_RENDER_ADAPTIVE
// Per-frame cost controller (core 0 only). Unit: core 0 ticks per rendered
// line -- CCOUNT on the device, so u32 differences survive the wrap.
//   sync: ticks inside update_scanline() for lines rendered here while the
//         display task was active (the frame's own reference);
//   job:  what core 0 paid for one line handed to the worker -- the display
//         query and the copy/submit of that line, plus every wait paid while
//         that job was still pending (next submit, conflict barriers, drain).
//         A job's cost is settled only once it is no longer pending, so a
//         wait is charged to the job that caused it, not to the next line;
//         it is capped at three times the frame's synchronous line cost.
// Once there are RA_AD_MIN_SYNC sync lines and RA_AD_MIN_JOBS settled jobs,
// the frame gives up the worker when avg(job) >= (1 - 1/8) * avg(sync):
// the pending job is drained and the rest of the frame renders here. Nothing
// carries over to the next frame; reset/load/start/stop clear it too.
#define RA_AD_MIN_SYNC 4
#define RA_AD_MIN_JOBS 8
#define RA_AD_JOB_CAP  3   // one job counts as at most this many synchronous lines
typedef struct
{
  u32 sync_sum, sync_n;
  u32 job_sum, job_n;
  u32 out_cost;                                // cost so far of the job not yet settled
  bool out;
  bool abandoned;
} ra_adaptive_t;
static ra_adaptive_t ra_ad;

static void ra_ad_settle(void)
{
  if (ra_ad.out)
  {
    // A job that cost core 0 more than three synchronous lines has already
    // said all it can ("worse than rendering here"): cap it there, so that one
    // stalled wakeup moves an 8-job average by at most 3/8 of a line and
    // cannot decide the frame on its own.
    u32 cost = ra_ad.out_cost;
    if (ra_ad.sync_n && cost > RA_AD_JOB_CAP * (ra_ad.sync_sum / ra_ad.sync_n))
      cost = RA_AD_JOB_CAP * (ra_ad.sync_sum / ra_ad.sync_n);
    ra_ad.job_sum += cost;
    ra_ad.job_n++;
    ra_ad.out = false;
    ra_ad.out_cost = 0;
  }
}

static bool ra_ad_should_abandon(void)
{
  if (ra_ad.sync_n < RA_AD_MIN_SYNC || ra_ad.job_n < RA_AD_MIN_JOBS)
    return false;
  return (uint64_t)ra_ad.job_sum * ra_ad.sync_n * 8 >= (uint64_t)ra_ad.sync_sum * ra_ad.job_n * 7;
}
#endif

#if GBSP_RENDER_HYBRID
bool rg_display_is_active(void);               // rg_display.c (host: the test harness)
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
#include <time.h>
// Host ticks: monotonic nanoseconds, truncated to 32 bits like CCOUNT.
static u32 RA_TICKS(void)
{
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (u32)((uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec);
}

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

// Host-only test hooks: extra delay inside each job (widens race windows and
// emulates slow workers), before each synchronous render (emulates the
// device's render cost) and at each scanline (emulates emulation time).
void (*gbsp_render_test_job_hook)(void);
void (*gbsp_render_test_sync_hook)(void);
void (*gbsp_render_test_line_hook)(void);
#define RA_TEST_HOOK(h) do { if (h) h(); } while (0)
#endif
#ifdef ESP_PLATFORM
#define RA_TEST_HOOK(h) do { } while (0)
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
  gbsp_render_state_reset();
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
  gbsp_render_state_reset();
}

bool gbsp_render_async_running(void)
{
  return ra_running;
}

// Synchronous render on core 0 (the test hook only exists on the host).
#define RA_SYNC_RENDER() do { RA_TEST_HOOK(gbsp_render_test_sync_hook); update_scanline(); } while (0)

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
#ifndef ESP_PLATFORM
  RA_TEST_HOOK(gbsp_render_test_line_hook);
#endif
#if GBSP_RENDER_ASYNC_STATS
  ra_frame_lines++;
#endif

#if GBSP_RENDER_ADAPTIVE
  if (ra_ad.abandoned)
  {
    // This frame gave up the worker; its last job was drained then.
#if GBSP_RENDER_ASYNC_STATS
    ra_sync_adaptive_lines++;
#endif
    RA_SYNC_RENDER();
    return;
  }
#endif

#if GBSP_RENDER_HYBRID
  u32 decision_ticks = 0;
  {
#if GBSP_RENDER_ASYNC_STATS || GBSP_RENDER_ADAPTIVE
    u32 d0 = RA_TICKS();
#endif
    bool display_active = rg_display_is_active();
#if GBSP_RENDER_ASYNC_STATS || GBSP_RENDER_ADAPTIVE
    decision_ticks = RA_TICKS() - d0;
#endif
#if GBSP_RENDER_ASYNC_STATS
    ra_decision_ticks += decision_ticks;
#endif
    if (display_active)
    {
      // Core 1 is the display's. Render this line here, after the previous
      // job: lines stay in order and the shared state is ours again.
      gbsp_render_barrier();
#if GBSP_RENDER_ADAPTIVE
      ra_ad_settle();
#endif
#if GBSP_RENDER_ASYNC_STATS
      ra_sync_lines++;
      if (ra_prev_async)
        ra_display_returns++;
      ra_prev_async = false;
#endif
#if GBSP_RENDER_ADAPTIVE
      u32 s0 = RA_TICKS();
      RA_SYNC_RENDER();
      ra_ad.sync_sum += RA_TICKS() - s0;
      ra_ad.sync_n++;
#else
      RA_SYNC_RENDER();
#endif
      return;
    }
  }
#endif

#if GBSP_RENDER_ADAPTIVE
  if (ra_ad_should_abandon())
  {
    // The worker costs core 0 at least as much as rendering here: drain the
    // pending job (its wait settles it) and keep the rest of the frame here.
    ra_ad.abandoned = true;
    gbsp_render_barrier();
    ra_ad_settle();
#if GBSP_RENDER_ASYNC_STATS
    ra_frame_abandon_line = read_ioreg(REG_VCOUNT);
    ra_sync_adaptive_lines++;
    ra_prev_async = false;
#endif
    RA_SYNC_RENDER();
    return;
  }
#endif

#if GBSP_RENDER_ASYNC_STATS
  if (RA_LOAD(gbsp_render_pending))
    ra_waits_submit++;
#endif
  gbsp_render_barrier();              // depth 1: the previous line is done
#if GBSP_RENDER_ADAPTIVE
  ra_ad_settle();                     // its waits were charged to it in wait()
  u32 t1 = RA_TICKS();
#endif
  for (unsigned i = 0; i < GBSP_RENDER_IO_HALFWORDS; i++)
    ra_io_copy[i] = io_registers[i];
#if GBSP_RENDER_ASYNC_STATS
  ra_jobs++;
  if (ra_frame_first_async < 0)
    ra_frame_first_async = read_ioreg(REG_VCOUNT);
  ra_prev_async = true;
#endif
  RA_FENCE();
  RA_STORE(gbsp_render_pending, 1);
  RA_FENCE();
  ra_wake_worker();
#if GBSP_RENDER_ADAPTIVE
  ra_ad.out = true;
  ra_ad.out_cost = (RA_TICKS() - t1) + decision_ticks;
#endif
}

void gbsp_render_async_wait(void)
{
#if GBSP_RENDER_ASYNC_STATS || GBSP_RENDER_ADAPTIVE
  u32 t0 = RA_TICKS();
#endif
#if GBSP_RENDER_ASYNC_STATS
  ra_waits++;
#endif
  RA_STORE(ra_main_waiting, 1);
  RA_FENCE();
  while (RA_LOAD(gbsp_render_pending))
    ra_block_main();                  // bounded timeout on device, then re-check
  RA_STORE(ra_main_waiting, 0);
  RA_FENCE();
#if GBSP_RENDER_ASYNC_STATS || GBSP_RENDER_ADAPTIVE
  u32 waited = RA_TICKS() - t0;
#endif
#if GBSP_RENDER_ASYNC_STATS
  ra_wait_ticks += waited;
#endif
#if GBSP_RENDER_ADAPTIVE
  if (ra_ad.out)
    ra_ad.out_cost += waited;         // charged to the job that was pending
#endif
}

void gbsp_render_state_reset(void)
{
#if GBSP_RENDER_ADAPTIVE
  memset(&ra_ad, 0, sizeof(ra_ad));
#endif
}

#if !defined(ESP_PLATFORM) && GBSP_RENDER_ADAPTIVE
// Host tests only: true when no controller state is held.
bool gbsp_render_test_adaptive_clean(void)
{
  static const ra_adaptive_t zero;
  return memcmp(&ra_ad, &zero, sizeof(zero)) == 0;
}
#endif

#if GBSP_RENDER_ASYNC_STATS || GBSP_RENDER_ADAPTIVE
void gbsp_render_frame_end(void)
{
#if GBSP_RENDER_ADAPTIVE
  ra_ad_settle();                     // the end-of-frame barrier drained it
#endif
#if GBSP_RENDER_ASYNC_STATS
  if (ra_frame_lines > 0)
  {
    ra_drawn_frames++;
    ra_first_async_hist[ra_frame_first_async < 0 ? 160 : ra_frame_first_async]++;
#if GBSP_RENDER_ADAPTIVE
    if (ra_frame_abandon_line >= 0)
    {
      ra_frames_abandoned++;
      ra_abandon_hist[ra_frame_abandon_line > 160 ? 160 : ra_frame_abandon_line]++;
    }
    ra_sync_cost_sum += ra_ad.sync_sum;
    ra_sync_cost_n += ra_ad.sync_n;
    ra_job_cost_sum += ra_ad.job_sum;
    ra_job_cost_n += ra_ad.job_n;
    if (ra_ad.sync_n)                 // what the worker saved (or cost) core 0 this frame
      ra_est_gain_ticks += (int64_t)ra_ad.sync_sum * ra_ad.job_n / ra_ad.sync_n - ra_ad.job_sum;
#endif
  }
  ra_frame_lines = 0;
  ra_frame_first_async = -1;
  ra_prev_async = false;
#if GBSP_RENDER_ADAPTIVE
  ra_frame_abandon_line = -1;
#endif
#endif
  gbsp_render_state_reset();          // no controller state crosses a frame
}
#endif

#if GBSP_RENDER_ASYNC_STATS
static u32 ra_hist_median(u16 *hist, u32 *min, u32 *max)
{
  u32 n = 0, seen = 0, median = 0;
  bool have_min = false, have_median = false;
  *min = *max = 0;
  for (unsigned i = 0; i <= 160; i++)
    n += hist[i];
  for (unsigned i = 0; i <= 160; i++)
  {
    if (!hist[i])
      continue;
    if (!have_min)
      *min = i, have_min = true;
    seen += hist[i];
    if (!have_median && 2 * seen >= n)
      median = i, have_median = true;
    *max = i;
  }
  memset(hist, 0, 161 * sizeof(*hist));
  return median;
}

void gbsp_render_stats_take(gbsp_render_stats_t *out)
{
  // Per-frame first async line: min, median (lower) and max over the window.
  out->first_async_median = ra_hist_median(ra_first_async_hist, &out->first_async_min, &out->first_async_max);
#if GBSP_RENDER_ADAPTIVE
  {
    u32 mn, mx;
    out->abandon_line_median = ra_hist_median(ra_abandon_hist, &mn, &mx);
  }
  out->sync_adaptive_lines = ra_sync_adaptive_lines;
  out->frames_abandoned = ra_frames_abandoned;
  out->sync_cost_sum = ra_sync_cost_sum; out->sync_cost_n = ra_sync_cost_n;
  out->job_cost_sum = ra_job_cost_sum; out->job_cost_n = ra_job_cost_n;
  out->est_gain_ticks = ra_est_gain_ticks;
  ra_sync_adaptive_lines = ra_frames_abandoned = 0;
  ra_sync_cost_sum = ra_sync_cost_n = ra_job_cost_sum = ra_job_cost_n = 0;
  ra_est_gain_ticks = 0;
#endif
  out->sync_lines = ra_sync_lines;
  out->drawn_frames = ra_drawn_frames;
  out->display_returns = ra_display_returns;
  out->decision_ticks = ra_decision_ticks;
  ra_sync_lines = ra_drawn_frames = ra_display_returns = ra_decision_ticks = 0;
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
