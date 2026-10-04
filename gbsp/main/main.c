#include <rg_system.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>

#include "../components/gbsp-libretro/common.h"
#include "../components/gbsp-libretro/memmap.h"
#include "../components/gbsp-libretro/sound.h"
#include "../components/gbsp-libretro/gba_memory.h"
#include "../components/gbsp-libretro/gba_cc_lut.h"
#include "../components/gbsp-libretro/ram_store.h"
#include "../components/gbsp-libretro/memory_profile.h"
#include "../components/gbsp-libretro/read_path.h"
#include "../components/gbsp-libretro/register_list.h"
#include "../components/gbsp-libretro/render_async.h"

// Profile header field, present only when the option is compiled in, so the
// header of a build without it stays byte-for-byte what it was.
#if GBSP_RENDER_ASYNC_ACTIVE
#define RENDER_CORE1_FMT " | render_core1 %d"
#define RENDER_CORE1_ARG , (int)gbsp_render_async_running()
#else
#define RENDER_CORE1_FMT ""
#define RENDER_CORE1_ARG
#endif

#if defined(CONFIG_GBSP_CPU_RODATA_DRAM) && CONFIG_GBSP_CPU_RODATA_DRAM
#define GBSP_CPU_RODATA_DRAM_ACTIVE 1
#else
#define GBSP_CPU_RODATA_DRAM_ACTIVE 0
#endif

// Build configuration only: these values do not verify hardware clocks.
// Zero means unavailable, rather than guessing a configuration.
#ifdef CONFIG_ESP32S3_DATA_CACHE_SIZE
#define RG_DCACHE_KB (CONFIG_ESP32S3_DATA_CACHE_SIZE / 1024)
#else
#define RG_DCACHE_KB 0
#endif
#ifdef CONFIG_SPIRAM_SPEED
#define RG_PSRAM_MHZ CONFIG_SPIRAM_SPEED
#else
#define RG_PSRAM_MHZ 0
#endif

#define AUDIO_SAMPLE_RATE (GBA_SOUND_FREQUENCY)
// A GBA frame is 280896 CPU ticks and the core emits one sample every 512 of
// them (sound.c: buffer_ticks = tick_delta * sound_frequency / GBC_BASE_RATE),
// so a frame carries 548.6 samples -- not the 547 this used to ask for. Asking
// for fewer than the core produces leaves a backlog growing by ~1.6 samples per
// frame inside its 1024-frame ring, and that ring has no overflow check: once
// the write index laps the read index, (index - base) & MASK wraps to near zero
// and the next read returns almost nothing. Asking for more costs nothing --
// sound_read_samples() clamps to what is actually available -- so ask for a
// frame's worth at 50fps and let the clamp decide.
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 50)

u32 idle_loop_target_pc = 0xFFFFFFFF;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 translation_gate_targets = 0;
boot_mode selected_boot_mode = boot_game;

u32 skip_next_frame = 0;
int sprite_limit = 1;

gbsp_memory_t *gbsp_memory;

static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;
static rg_app_t *app;

static char *sramFile;
// Seconds of quiet (no cartridge writes) before the backup is flushed to the
// .sram file. The kit is normally switched off at the power rail, which never
// reaches RG_EVENT_SHUTDOWN, so this is the main safety net and it is on by
// default. 0 disables it.
static int autoSaveSRAM = 3;
// Deadline in microseconds (0 = disarmed). Wall clock, not frames: gbsp runs
// well below 60fps, so a frame counter would stretch the delay unpredictably.
static int64_t autoSaveSRAM_Timer = 0;
static u32 lastBackupWrites = 0;
static int sramSaveCount = 0;

// Reserved at boot, before init_gamepak_buffer() eats the heap. See app_main().
static void *stateBuffer = NULL;

static const char *SETTING_SOUND_EMULATION = "sound";
static const char *SETTING_SAVESRAM = "SaveSRAM";
static const char *SETTING_PROFILE = "Profile";
static const char *SETTING_AUDIO_SYNC = "AudioSync";
static const char *SETTING_FRAMESKIP = "Frameskip";

// -1 = leave rg_system_tick()'s automatic frameskip alone, 0..5 = pin it.
//
// The automatic one (rg_system.c, "Auto frameskip") moves between 1 and 5 on its
// own, so a measurement can never be attributed to a frameskip the tester chose
// -- every "skip N" in the logs so far was the system's decision, changing
// underneath whatever else was being compared. It also floors at 1, so skip 0 is
// not reachable at all, and it decides using speedPercent, which retro-go scales
// by overclockMhz/240 and therefore overstates on the S3.
//
// Pinning it is what makes the rendering cost separable: per-frame cost is
// E + R/(N+1), so the same scene at two known values of N gives E and R.
static int forcedFrameskip = -1;

// Performance log, same idea as the a26 core's a26-perfil.txt: one line every
// PROFILE_FRAMES frames with where the time actually went. Reading SPEED% off
// the menu only says how bad it is; this says which phase to attack.
#define PROFILE_FRAMES 300
#if GBSP_MEMORY_PROFILE_ACTIVE
#define PROFILE_FILE RG_BASE_PATH "/gbsp-memory.txt"
#else
#define PROFILE_FILE RG_BASE_PATH "/gbsp-perfil.txt"
#endif

static int profileEnabled = 1;
static bool profileHeaderWritten = false;
// Internal RAM available right before we try to place iwram there. Logged in
// the profile header so a failed placement says how much was missing.
static int internalFreeKB = 0, internalBlockKB = 0;
// Display processing time as of the start of the window. See profile_flush().
static int64_t lastDisplayBusy = 0;
static struct
{
    int frames, drawn;
    int64_t input, emu, video, audio, audioPrepare, audioSubmit, total;
    int64_t rtcStart;
    int audioRateChanges;
} profile;

// Wall clock for the window, read from the RTC. This started as a guard against
// rg_system_timer() (esp_timer) following the PLL under overclock, which would
// have inflated exactly the gains we were measuring. The guard did its job and
// reported no such drift: "clk" has read 0.99-1.00 in every undisturbed window,
// including at oc 2 / 280MHz. It is kept because it costs one read per window
// and it catches something else instead -- real time that passed with no
// emulation in it. A card write or a menu visit inflates the RTC interval
// without adding to any phase, and "clk" well above 1.00 is how that shows.
//
// The phase figures below are NOT scaled by it. They are esp_timer measurements
// of actual work and they are the truth; the surplus belongs to no phase, so
// spreading it across them would invent emulation time that never happened.
// "clk" is printed as a diagnostic: a window with clk outside ~1.00 is one whose
// absolute microseconds should not be compared with the others.
extern uint64_t esp_rtc_get_time_us(void);

// Audio sync.
//
// The core produces sound on the GBA's clock, not on the wall clock: one
// emulated frame always yields ~548 samples, so 60fps is 32768 samples per real
// second and 35fps is only ~19200. The DAC does not care -- the I2S clock keeps
// consuming 32768 a second regardless -- so the DMA ring (RG_AUDIO_DMA_BUFFER_*
// in the target config) runs dry and the hardware replays what was in it. That,
// several dozen times a second, is the buzz. It is also why every other core
// here sounds clean: they actually reach 100%.
//
// The missing samples cannot be invented -- that audio has not been emulated
// yet -- so the only honest fix is to slow the DAC down to the rate the core
// really sustains. The stream then becomes continuous. The cost is pitch: at
// 60% speed the sound drops roughly six semitones, like a tape played slow.
// That is consistent with what is on screen (the game IS in slow motion) and is
// a much milder artifact than the buzz. It also means overclock and audio
// quality pull in the same direction: the closer to 100%, the smaller the
// detune.
//
// Set to Off in the options menu to go back to a fixed 32768Hz and hear the
// difference.
// A steady detune is far less objectionable than a wobbling one: the ear barely
// registers a tape running 14% slow, but hears a semitone of wow immediately.
// The first version of this tracked the measured capability window by window,
// and the log showed why that is wrong -- capability in Fire Red swings +/-15%
// from one second to the next (scene load, ROM page faults), so the rate moved
// in 32 of 35 windows and the pitch wobbled with it. So: track the MEDIAN of the
// last few windows rather than the latest one. The median ignores the one-off
// spike or stall and moves only when the game settles into a genuinely different
// speed; over those same 35 windows it brings the changes down from 32 to 6.
//
// The variance it refuses to chase has to go somewhere, and that somewhere is
// the DMA ring: see RG_AUDIO_DMA_BUFFER_COUNT in the target config. The two
// changes only work together -- a steady rate with a 22ms ring just moves the
// artifact from wow to dropouts.
// Two failure modes the median alone does not cover, both found by review:
//
// The nominal rate is not just another rate. It is the only one with no detune,
// and the only one where the DAC is not holding the emulator back -- below it,
// a full ring makes i2s_write() block and paces emulation at whatever the DAC is
// doing. With a flat 1/16 hysteresis, 31232, 31744 and 32256 Hz can never reach
// 32768: the step is under 6% from each. A core capable of full speed would sit
// at 95% forever. So nominal gets its own rule: entering it ignores hysteresis,
// leaving it obeys. That asymmetry is deliberate -- being stuck below nominal
// costs a permanent detune AND throttling, while being at nominal without quite
// sustaining it costs an underrun the ring absorbs.
//
// Except when it does not. A capability that settles just below realtime -- 97%,
// 99% -- produces less than the DAC consumes, every second, forever. The ring
// only postpones that; it drains in a few seconds and then the buzz is back for
// good. And the median cannot rescue it, because the step down from nominal is
// itself under the hysteresis. Simulated against this controller: at a sustained
// 99%, 19 of 30 seconds ran dry; at 97%, 25 of 30. That band -- roughly 94% to
// 100% -- is the worst possible place for a blind spot, because it is exactly
// where the games that ALMOST run full speed live.
//
// So a deficit guard, which overrides hysteresis downwards. The signal is not
// "produced less than the rate" on its own: when the ring is full the emulator
// is throttled and production equals the rate by definition. What distinguishes
// the two is blocking. No blocking AND production below consumption means the
// ring is draining, and three of those in a row is not noise.
#define AUDIO_SYNC_WINDOW_US 1000000
#define AUDIO_SYNC_HISTORY   8
#define AUDIO_SYNC_STARVE    3
#define AUDIO_SYNC_MIN_RATE  (AUDIO_SAMPLE_RATE / 4)

static int audioSync = 1;
static int audioRate = AUDIO_SAMPLE_RATE; // rate currently programmed on the DAC
static int audioCapability = 0;           // last window's measurement, for the log
static int audioStarving = 0;             // consecutive windows with the ring draining
static int audioHistory[AUDIO_SYNC_HISTORY];
static int audioHistoryLen = 0, audioHistoryPos = 0;
static int64_t audioWindowStart = 0;      // RTC microseconds
static uint32_t audioWindowFrames = 0;    // frames handed to the DAC this window
static int64_t audioWindowBlocked = 0;    // us spent inside rg_audio_submit

// Start a fresh measuring window, keeping the history. Called whenever real time
// passed without emulation in it -- a menu, a card write -- since that dead time
// would otherwise be charged to the emulator and read as a slowdown.
static void audio_sync_window(void)
{
    // RTC rather than rg_system_timer(), for the same reason the profile uses
    // it: esp_timer follows the PLL, so under overclock it could read the window
    // short and we would tune the DAC to a speed we are not actually running at.
    audioWindowStart = (int64_t)esp_rtc_get_time_us();
    audioWindowFrames = 0;
    audioWindowBlocked = 0;
}

static void audio_sync_reset(void)
{
    audioHistoryLen = 0;
    audioHistoryPos = 0;
    audioStarving = 0;
    audio_sync_window();
}

static int audio_sync_median(void)
{
    int sorted[AUDIO_SYNC_HISTORY];
    int i, j;

    for (i = 0; i < audioHistoryLen; ++i) // insertion sort, at most 8 elements
    {
        int v = audioHistory[i];
        for (j = i - 1; j >= 0 && sorted[j] > v; --j)
            sorted[j + 1] = sorted[j];
        sorted[j + 1] = v;
    }
    return sorted[audioHistoryLen / 2];
}

static void audio_sync_update(void)
{
    const int64_t elapsed = (int64_t)esp_rtc_get_time_us() - audioWindowStart;
    if (elapsed < AUDIO_SYNC_WINDOW_US)
        return;

    const int64_t frames = audioWindowFrames;
    // Clamped so a pathological window can never make the divisor vanish.
    const int64_t blocked = RG_MIN(audioWindowBlocked, elapsed / 2);
    audio_sync_window();

    // Capability, not throughput. Time spent blocked in rg_audio_submit() is
    // time the DAC held the emulator back, so what we just measured came out of
    // only (elapsed - blocked) worth of emulation; without the block it would
    // have produced proportionally more. Correcting for it is what stops the
    // loop running away: aiming below plain throughput throttles the core, which
    // lowers the next measurement, which lowers the rate again, all the way to
    // the floor.
    audioCapability = (int)(frames * 1000000 / (elapsed - blocked));

    audioHistory[audioHistoryPos] = audioCapability;
    audioHistoryPos = (audioHistoryPos + 1) % AUDIO_SYNC_HISTORY;
    if (audioHistoryLen < AUDIO_SYNC_HISTORY)
        audioHistoryLen++;

    // Aim a couple of percent under the median, so the ring keeps a small
    // surplus instead of sitting on empty -- an empty ring is precisely the
    // replay-the-last-buffer buzz being fixed here. The price is those two
    // percent, given up waiting in i2s_write(). At or above realtime the margin
    // is dropped: there the surplus exists on its own, and shaving it would put
    // us back below nominal for nothing.
    const int median = audio_sync_median();
    int target = median;
    if (median < AUDIO_SAMPLE_RATE)
        target -= target >> 6;
    target = (target + 256) & ~511; // no point chasing single hertz
    target = RG_MIN(RG_MAX(target, AUDIO_SYNC_MIN_RATE), AUDIO_SAMPLE_RATE);

    // Draining, or merely throttled? See the note above the declarations.
    const int64_t consumed = (int64_t)audioRate * elapsed / 1000000;
    if (blocked == 0 && frames < consumed)
        audioStarving++;
    else
        audioStarving = 0;

    // Hysteresis is ~6% to match the median: re-programming the rate stops the
    // I2S channel and clears its DMA buffers, which is a click of its own, and
    // every move is a step in pitch. Better to sit slightly wrong and hold still
    // than to be continually almost right -- but not when sitting still means
    // running dry, hence the two overrides.
    bool move;
    if (target == AUDIO_SAMPLE_RATE)
        move = (audioRate != AUDIO_SAMPLE_RATE);        // nothing beats no detune
    else if (audioStarving >= AUDIO_SYNC_STARVE)
        move = (target < audioRate);                    // escape hatch, downwards only
    else
        move = (abs(target - audioRate) * 16 > audioRate);

    if (move)
    {
        audioRate = target;
        audioStarving = 0;
        profile.audioRateChanges++;
        rg_audio_set_sample_rate(audioRate);
    }
}

void netpacket_poll_receive()
{
}

void netpacket_send(uint16_t client_id, const void *buf, size_t len)
{
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

// Start a clean window. rtcStart is taken HERE rather than on the first frame of
// the window: taken at the end of that frame, as it used to be, the RTC interval
// missed a frame that profile.total already contained, which skewed clk.
// The slice counters live in the emulator core and reset on the same boundary.
static void profile_reset_window(void)
{
    memset(&profile, 0, sizeof(profile));
    profile.rtcStart = (int64_t)esp_rtc_get_time_us();
    gamepak_page_faults = 0;
    gamepak_page_time = 0;
    gbsp_scanline_ticks = 0;
    gbsp_scanline_count = 0;
    gbsp_update_ticks = 0;
    gbsp_memory_profile_reset();
    // Snapshot rather than reset: this one is written by the display task on the
    // OTHER core. Resetting it from here would race with that task and lose
    // counts, so it is left strictly monotonic -- core 1 only ever adds, we only
    // ever read -- and the window's value is the difference. See profile_flush()
    // for what happens when the 64-bit read tears.
    lastDisplayBusy = rg_display_get_counters().busyTime;
}

static void profile_flush(void)
{
    if (profile.frames < 1)
        return;

    // Close the measurement before opening/writing the log: logging I/O must
    // not inflate this window's residual or the asynchronous display sample.
    const int64_t realWindow = (int64_t)esp_rtc_get_time_us() - profile.rtcStart;
    const int64_t displayNow = rg_display_get_counters().busyTime;
    // profile.total already holds the SUM, not the printed per-frame average.
    // Keep signed microseconds: clock disagreement must remain visible too.
    const int64_t residualUs = realWindow - profile.total;
    // NEWLIB_NANO_FORMAT cannot print 64-bit integers. Saturate before casting:
    // a long menu visit can exceed int32 microseconds. +/-2e9 marks saturation;
    // keep residualUs intact for analysis and leave clk based on the full window.
    const int residualShown = residualUs > 2000000000LL ? 2000000000
                            : residualUs < -2000000000LL ? -2000000000
                            : (int)residualUs;

    FILE *fp = fopen(PROFILE_FILE, "a");
    if (!fp)
    {
        RG_LOGE("Could not open the profile log (%s)", PROFILE_FILE);
        profile.frames = 0; // don't retry every 300 frames
        return;
    }
    // (window reset happens at the end, via profile_reset_window)

    if (!profileHeaderWritten)
    {
        // One header per session so the lines below can be attributed to a
        // build/configuration when comparing experiments.
        // 0x3fc.../0x3fd... is internal SRAM on the S3, 0x3c... is PSRAM.
        fprintf(fp, "\n=== gbsp %s | %s | iwram %s | fb %s | cache de ROM %dMB"
                    " | RAM interna no boot: %dKB livre, maior bloco %dKB"
                    " | config compilada: dcache %dKB | psram %dMHz | fast_ram %d (%s) | memstats %d | compact_reads %d | sparse_regs %d | cpu_rodata_dram %d" RENDER_CORE1_FMT " ===\n",
                app->version ? app->version : "?",
                app->romPath ? rg_basename(app->romPath) : "?",
                ((u32)iwram_ptr >> 24) == 0x3c ? "PSRAM" : "interna",
                ((u32)currentUpdate->data >> 24) == 0x3c ? "PSRAM" : "interna",
                (int)gamepak_buffer_count, internalFreeKB, internalBlockKB,
                (int)RG_DCACHE_KB, (int)RG_PSRAM_MHZ, GBSP_FAST_RAM_WRITES_ACTIVE, GBSP_FAST_RAM_WRITES_IMPL, GBSP_MEMORY_PROFILE_ACTIVE, GBSP_COMPACT_READS_ACTIVE, GBSP_SPARSE_REGISTERS_ACTIVE, GBSP_CPU_RODATA_DRAM_ACTIVE RENDER_CORE1_ARG);
        profileHeaderWritten = true;
    }

    rg_stats_t stats = rg_system_get_stats();
    const int n = profile.frames;
    const int mhz = rg_system_get_cpu_speed();
    const double perFrame = 1.0 / n;

    // "clk" is a health flag for the window, not a correction factor. It is the
    // RTC's wall time over the sum of the measured phases: 1.00 means everything
    // that happened is accounted for, and anything much above it means real time
    // passed with no emulation in it -- a card write, a menu. The phases below
    // are NOT scaled by it; see the note by esp_rtc_get_time_us(). A window with
    // clk outside ~1.00 is one whose absolute microseconds should be ignored.
    const double clk = (profile.total > 0 && realWindow > 0)
                     ? (double)realWindow / profile.total : 1.0;

    // "emu" split three ways, all of them slices of it rather than siblings:
    // execute_arm() calls update_gba(), which calls update_scanline().
    //
    //   render   -- update_scanline(), the PPU
    //   sistema  -- the rest of update_gba(): timers, DMA, serial, sound
    //   fora     -- emu minus update_gba()
    //
    // "fora" is deliberately NOT called "interpretador". It is everything the
    // emulator does outside update_gba(), which includes the memory handlers and
    // the ROM paging the interpreter calls into, not just instruction decode.
    // Naming it after the interpreter would smuggle in a conclusion we have not
    // measured.
    const int64_t updateUs = mhz > 0 ? (int64_t)(gbsp_update_ticks / mhz) : 0;
    const int64_t renderUs = mhz > 0 ? (int64_t)(gbsp_scanline_ticks / mhz) : 0;

    // Display processing on the other core. This is DURATION, not occupancy:
    // rg_display's busyTime covers write_update(), so it excludes the wait on
    // the task queue and the final lcd_sync(), but it still contains any stall
    // on a DMA buffer inside the blit. It says how long core 1 was occupied with
    // our frames, not how many cycles it actually burned.
    //
    // The counter is int64_t, written on core 1 and read here on core 0, so a
    // read can tear when a carry crosses the 32-bit boundary -- roughly once per
    // 71 minutes of accumulated busy time. Rather than lock anything, take the
    // difference and check it is physically possible; a torn read then costs one
    // window, reported as -1, instead of a silently wrong number.
    int64_t displayDelta = displayNow - lastDisplayBusy;
    if (displayDelta < 0 || (realWindow > 0 && displayDelta > realWindow))
        displayDelta = -1;

    // The configured frameskip, not app->frameskip, which the automatic logic
    // keeps moving even while a forced value is in effect. "f" marks forced.
    const int skipShown = (forcedFrameskip >= 0) ? forcedFrameskip : (int)app->frameskip;

    fprintf(fp, "%d quadros: %d desenhados, %d pulados | oc %d (%dMHz) | skip %d%s | entrada %d"
                " | emu %d (render %d | sistema %d | fora %d | rom %d em %d faults)"
                " | video %d | audio %d (preparo %d | envio %d) | display %d | total %d us"
                " | residual_janela %d us"
                " | %d qps | clk %d.%02d | vel %d%% | ocupado %d%%"
                " | dac %dHz (cap %d, %d trocas) | %d chamadas de scanline\n",
            n, profile.drawn, n - profile.drawn,
            rg_system_get_overclock(), mhz, skipShown, forcedFrameskip >= 0 ? "f" : "",
            (int)(profile.input * perFrame), (int)(profile.emu * perFrame),
            (int)(renderUs * perFrame), (int)((updateUs - renderUs) * perFrame),
            (int)((profile.emu - updateUs) * perFrame),
            (int)(gamepak_page_time * perFrame), (int)gamepak_page_faults,
            (int)(profile.video * perFrame), (int)(profile.audio * perFrame),
            (int)(profile.audioPrepare * perFrame), (int)(profile.audioSubmit * perFrame),
            displayDelta < 0 ? -1 : (int)(displayDelta * perFrame),
            (int)(profile.total * perFrame), residualShown,
            (int)(profile.total ? (1000000.0 * n / profile.total) : 0),
            (int)(clk + 0.005), (int)((clk + 0.005 - (int)(clk + 0.005)) * 100),
            (int)(stats.speedPercent + 0.5f), (int)(stats.busyPercent + 0.5f),
            audioRate, audioCapability, profile.audioRateChanges,
            (int)gbsp_scanline_count);

    gbsp_memory_profile_write(fp, (unsigned)n);
#if GBSP_RENDER_ASYNC_STATS
    {
        gbsp_render_stats_t rs;
        gbsp_render_stats_take(&rs);
        fprintf(fp, "render_core1 jobs %u | esperas %u (no envio %u) | espera_us %u | worker_us %u | pilha_livre %u bytes\n",
                (unsigned)rs.jobs, (unsigned)rs.waits, (unsigned)rs.waits_submit, mhz > 0 ? (unsigned)(rs.wait_ticks / mhz) : 0,
                mhz > 0 ? (unsigned)(rs.worker_ticks / mhz) : 0, (unsigned)rs.stack_free);
    }
#endif
    fclose(fp);
    profile_reset_window();
}

// Forget any pending write: the backup in memory and the .sram file agree.
static void sram_mark_clean(void)
{
    gamepak_backup_dirty = 0;
    lastBackupWrites = 0;
    autoSaveSRAM_Timer = 0;
}

// The cartridge backup (SRAM/flash/EEPROM) lives in a .sram file next to the
// save states, same as every other core does (see retro-core/main/main_gbc.c).
static bool sram_load(void)
{
    if (!sramFile)
        return false;

    void *buffer = gamepak_backup;
    size_t buffer_len = sizeof(gamepak_backup); // macro'd to gbsp_memory->gamepak_backup

    if (!rg_storage_read_file(sramFile, &buffer, &buffer_len, RG_FILE_USER_BUFFER))
    {
        RG_LOGI("No SRAM file loaded (%s)", sramFile);
        return false;
    }

    RG_LOGI("SRAM loaded (%d bytes)", (int)buffer_len);
    sram_mark_clean();
    return true;
}

static bool sram_save(void)
{
    if (!sramFile)
        return false;

    if (!rg_storage_write_file(sramFile, gamepak_backup, sizeof(gamepak_backup), 0))
    {
        RG_LOGE("Failed to write SRAM file (%s)", sramFile);
        return false;
    }

    RG_LOGI("SRAM saved");
    sram_mark_clean();
    sramSaveCount++;
    return true;
}

static bool save_state_handler(const char *filename)
{
    size_t buffer_len = GBA_STATE_MEM_SIZE;
    void *buffer = stateBuffer ? stateBuffer : malloc(buffer_len);
    if (!buffer)
    {
        RG_LOGE("Could not get a %d bytes buffer for the save state!", (int)buffer_len);
        return false;
    }
    gba_save_state(buffer);
    bool success = rg_storage_write_file(filename, buffer, buffer_len, 0);
    if (buffer != stateBuffer)
        free(buffer);
    return success;
}

static bool load_state_handler(const char *filename)
{
    size_t buffer_len = GBA_STATE_MEM_SIZE;
    void *buffer = stateBuffer ? stateBuffer : malloc(buffer_len);
    if (!buffer)
    {
        RG_LOGE("Could not get a %d bytes buffer for the save state!", (int)buffer_len);
        return false;
    }
    void *owned = (buffer == stateBuffer) ? NULL : buffer;
    bool success = rg_storage_read_file(filename, &buffer, &buffer_len, RG_FILE_USER_BUFFER)
                    && gba_load_state(buffer);
    free(owned);

    if (!success)
    {
        // If a state fails to load then behave as we do on boot:
        // hard reset and load the cartridge backup if present.
        reset_gba();
        sram_load();
        return false;
    }

    // The state now carries the cartridge backup too (gba_memory.c,
    // "backup-data"), so loading one replaces what is in memory. Mark it dirty
    // so the .sram on the card catches up with the state the player just
    // restored -- otherwise the two would disagree until the game writes again.
    gamepak_backup_dirty++;

    return true;
}

static bool reset_handler(bool hard)
{
    if (gamepak_backup_dirty) // the backup survives reset_gba(), don't lose it
        sram_save();
    reset_gba();
    return true;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
    {
        rg_display_submit(currentUpdate, 0);
    }
    else if (event == RG_EVENT_SHUTDOWN)
    {
#if GBSP_RENDER_ASYNC_ACTIVE
        gbsp_render_async_stop();
#endif
        if (gamepak_backup_dirty)
            sram_save();
        profile_flush();
    }
}

int16_t input_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    // RG_LOGI("%u, %u, %u, %u", port, device, index, id);
    uint32_t joystick = rg_input_read_gamepad();
    int16_t val = 0;
    if (joystick & RG_KEY_DOWN) val |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (joystick & RG_KEY_UP) val |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if (joystick & RG_KEY_LEFT) val |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (joystick & RG_KEY_RIGHT) val |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (joystick & RG_KEY_START) val |= (1 << RETRO_DEVICE_ID_JOYPAD_START);
    if (joystick & RG_KEY_SELECT) val |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (joystick & RG_KEY_B) val |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (joystick & RG_KEY_A) val |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    return val;
}

void set_fastforward_override(bool fastforward)
{
}

static rg_gui_event_t sound_toggle_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        sound_master_enable = !sound_master_enable;
        rg_settings_set_number(NS_APP, SETTING_SOUND_EMULATION, sound_master_enable);
    }

    strcpy(option->value, sound_master_enable ? _("On") : _("Off"));

    return RG_DIALOG_VOID;
}

// Doubles as a readout: with sync on, the value is the rate the DAC is running
// at right now, which is also a direct reading of emulation speed.
static rg_gui_event_t audio_sync_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        audioSync = !audioSync;
        rg_settings_set_number(NS_APP, SETTING_AUDIO_SYNC, audioSync);

        if (audioSync)
        {
            audio_sync_reset();
        }
        else
        {
            audioRate = AUDIO_SAMPLE_RATE;
            rg_audio_set_sample_rate(audioRate);
        }
    }

    if (audioSync)
        sprintf(option->value, "%dHz", audioRate);
    else
        strcpy(option->value, _("Off"));

    return RG_DIALOG_VOID;
}

static rg_gui_event_t frameskip_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV && forcedFrameskip > -1) forcedFrameskip--;
    if (event == RG_DIALOG_NEXT && forcedFrameskip <  5) forcedFrameskip++;

    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        // Persisted on purpose: the kit is powered off at the rail between runs,
        // and a setting that reverts to Auto on every boot would quietly undo
        // the experiment it exists to make possible.
        rg_settings_set_number(NS_APP, SETTING_FRAMESKIP, forcedFrameskip);
        if (forcedFrameskip >= 0)
            app->frameskip = forcedFrameskip;
    }

    if (forcedFrameskip < 0) sprintf(option->value, "%s (%d)", _("Auto"), (int)app->frameskip);
    else sprintf(option->value, "%d", forcedFrameskip);

    return RG_DIALOG_VOID;
}

static rg_gui_event_t sram_autosave_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV) autoSaveSRAM--;
    if (event == RG_DIALOG_NEXT) autoSaveSRAM++;

    autoSaveSRAM = RG_MIN(RG_MAX(0, autoSaveSRAM), 999);

    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        rg_settings_set_number(NS_APP, SETTING_SAVESRAM, autoSaveSRAM);
    }

    if (autoSaveSRAM == 0) strcpy(option->value, _("Off"));
    else sprintf(option->value, "%3ds", autoSaveSRAM);

    return RG_DIALOG_VOID;
}

// Diagnostics: the serial monitor isn't usable while playing, so surface the
// SRAM state in the options menu. Shows the pending autosave countdown, or
// clean/dirty when no flush is scheduled, plus saves made this session.
static rg_gui_event_t sram_status_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (autoSaveSRAM_Timer > 0)
    {
        int remaining = (int)((autoSaveSRAM_Timer - rg_system_timer()) / 1000000);
        sprintf(option->value, "%ds (%d)", remaining > 0 ? remaining : 0, sramSaveCount);
    }
    else
    {
        sprintf(option->value, "%s (%d)", gamepak_backup_dirty ? "dirty" : "clean", sramSaveCount);
    }
    return RG_DIALOG_VOID;
}

static rg_gui_event_t profile_toggle_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        profileEnabled = !profileEnabled;
        rg_settings_set_number(NS_APP, SETTING_PROFILE, profileEnabled);
        if (!profileEnabled)
            profile_flush(); // don't leave a partial window unwritten
        else
            profile_reset_window();
    }

    strcpy(option->value, profileEnabled ? _("On") : _("Off"));

    return RG_DIALOG_VOID;
}

static void options_handler(rg_gui_option_t *dest)
{
    *dest++ = (rg_gui_option_t){0, _("Audio enable"),  "-", RG_DIALOG_FLAG_NORMAL, &sound_toggle_cb};
    *dest++ = (rg_gui_option_t){0, _("Audio sync"),    "-", RG_DIALOG_FLAG_NORMAL, &audio_sync_cb};
    *dest++ = (rg_gui_option_t){0, _("Frameskip"),     "-", RG_DIALOG_FLAG_NORMAL, &frameskip_cb};
    *dest++ = (rg_gui_option_t){0, _("SRAM autosave"), "-", RG_DIALOG_FLAG_NORMAL, &sram_autosave_cb};
    *dest++ = (rg_gui_option_t){0, _("SRAM state"),    "-", RG_DIALOG_FLAG_NORMAL, &sram_status_cb};
    *dest++ = (rg_gui_option_t){0, _("Profile log"),   "-", RG_DIALOG_FLAG_NORMAL, &profile_toggle_cb};
    *dest++ = (rg_gui_option_t)RG_DIALOG_END;
}

// Claim iwram before anything else in this app runs. Measured after
// rg_system_init(), the largest contiguous internal block is 31KB -- one
// kilobyte short of the 32KB iwram needs, with 112KB internal still free but
// fragmented. Before it, the heap has not been carved up by the system's tasks,
// drivers and buffers yet. heap_caps_calloc() directly rather than rg_alloc(),
// because rg_alloc logs through a system that is not up at this point.
static void claim_iwram(void)
{
    internalFreeKB = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    internalBlockKB = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024;
    iwram_ptr = heap_caps_calloc(1, IWRAM_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void app_main(void)
{
    claim_iwram();

    app = rg_system_init(&(const rg_config_t){
        .sampleRate = AUDIO_SAMPLE_RATE,
        .frameRate = 60,
        .storageRequired = true,
        .romRequired = true,
        .handlers = {
            .loadState = &load_state_handler,
            .saveState = &save_state_handler,
            .reset = &reset_handler,
            .screenshot = &screenshot_handler,
            .event = &event_handler,
            .options = &options_handler,
        },
    });
    // rg_system_set_overclock(2);

    sound_master_enable = rg_settings_get_number(NS_APP, SETTING_SOUND_EMULATION, true);
    autoSaveSRAM = (int)rg_settings_get_number(NS_APP, SETTING_SAVESRAM, autoSaveSRAM);
    profileEnabled = (int)rg_settings_get_number(NS_APP, SETTING_PROFILE, profileEnabled);
    audioSync = (int)rg_settings_get_number(NS_APP, SETTING_AUDIO_SYNC, audioSync);
    forcedFrameskip = (int)rg_settings_get_number(NS_APP, SETTING_FRAMESKIP, forcedFrameskip);

    sramFile = rg_emu_get_path(RG_PATH_SAVE_SRAM, app->romPath);
    if (!rg_storage_mkdir(rg_dirname(sramFile)))
        RG_LOGE("Unable to create SRAM folder...");

    // iwram in PSRAM is the fallback, not the plan: claim_iwram() above already
    // tried internal RAM before anything else in this app ran.
    if (!iwram_ptr)
    {
        RG_LOGW("No internal RAM for iwram (%dKB free, largest block %dKB), using PSRAM",
                internalFreeKB, internalBlockKB);
        iwram_ptr = rg_alloc(IWRAM_BYTES, MEM_ANY);
    }

    updates[0] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_FAST);
    updates[0]->height = GBA_SCREEN_HEIGHT;
    // updates[1] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_FAST);
    // updates[1]->height = GBA_SCREEN_HEIGHT;
    currentUpdate = updates[0];

    gba_screen_pixels = currentUpdate->data;

    gbsp_memory = rg_alloc(sizeof(*gbsp_memory), MEM_ANY);
    RG_LOGI("gbsp_memory=%p iwram=%p fb=%p", gbsp_memory, iwram_ptr, currentUpdate->data);

    // init_gamepak_buffer() below allocates 1MB blocks in a loop until malloc
    // fails, i.e. it deliberately takes the whole heap for ROM paging. Any
    // large buffer we need later has to be reserved before it runs, otherwise
    // the save state's malloc() always fails ("Save failed" in the menu).
    stateBuffer = rg_alloc(GBA_STATE_MEM_SIZE, MEM_SLOW | MEM_NOPANIC);
    if (!stateBuffer)
        RG_LOGE("Could not reserve the save state buffer, save states may fail!");

    libretro_supports_bitmasks = true;
    retro_set_input_state(input_cb);
    init_gamepak_buffer();
    init_sound();
    // load_bios(RG_BASE_PATH_BIOS "/gba_bios.bin");

    memset(gamepak_backup, 0xff, sizeof(gamepak_backup));
    if (load_gamepak(NULL, app->romPath, FEAT_DISABLE, FEAT_DISABLE, SERIAL_MODE_DISABLED) != 0)
    {
        RG_PANIC("Could not load the game file.");
    }

    RG_LOGI("reset_gba");
    reset_gba();

#if GBSP_RENDER_ASYNC_ACTIVE
    if (!gbsp_render_async_start())
        RG_LOGE("render_core1: worker not created, rendering on core 0");
#endif

    // Load the cartridge backup first, unconditionally. A save state now
    // carries its own copy (gba_memory.c, "backup-data") and will overwrite
    // this a few lines below -- which is correct, the state is the more
    // specific snapshot -- but a state that is missing, refused or from an
    // older format leaves the .sram as the fallback instead of a blank save.
    sram_load();

    if (app->bootFlags & RG_BOOT_RESUME)
    {
        RG_LOGI("load_state");
        rg_emu_load_state(app->saveSlot);
    }

    RG_LOGI("emulation loop");

    // static, not on the stack: 2.6KB now that we ask for a full 50fps frame.
    static rg_audio_sample_t mixbuffer[AUDIO_BUFFER_LENGTH];

    audio_sync_reset();
    profile_reset_window();

    while (true)
    {
        // RG_TIMER_INIT();
        const int64_t startTime = rg_system_timer();
        uint32_t joystick = rg_input_read_gamepad();

        if (joystick & (RG_KEY_MENU | RG_KEY_OPTION))
        {
            if (joystick & RG_KEY_MENU)
            {
                if (gamepak_backup_dirty) // save in case the user quits
                    sram_save();
                profile_flush(); // the kit is often powered off right after this
                rg_gui_game_menu();
            }
            else
                rg_gui_options_menu();
            memset(&mixbuffer, 0, sizeof(mixbuffer));
            // Menu time produced no audio, and counting it would read as a
            // slowdown. The history goes too, not just the window: the menu is
            // exactly where overclock and frameskip change, so what it holds
            // describes a machine that may no longer exist.
            audio_sync_reset();
            continue;
        }

        update_input();
        rumble_frame_reset();
        clear_gamepak_stickybits();
        const int64_t afterInput = rg_system_timer();

        execute_arm(execute_cycles);
        // RG_TIMER_LAP("execute_arm");
        const int64_t afterEmu = rg_system_timer();

        const bool drawnFrame = !skip_next_frame;
        if (drawnFrame)
            rg_display_submit(currentUpdate, 0);
        const int64_t afterVideo = rg_system_timer();

        size_t frames_count = sound_read_samples((s16 *)mixbuffer, AUDIO_BUFFER_LENGTH);
        // RG_TIMER_LAP("sound_read_samples");

        rg_system_tick(rg_system_timer() - startTime);

        // Note: rg_system_tick() only accounts, it never sleeps. Nothing here
        // paces the loop except rg_audio_submit() blocking when the DMA ring is
        // full -- so a near-zero "audio" figure in the log means emulation is
        // the wall and the DAC is starving, which is the state audio sync exists
        // to leave. With sync on it should settle at a small but non-zero
        // fraction of the frame: the couple of percent of headroom we asked for.
        const int64_t beforeSubmit = rg_system_timer();
        rg_audio_submit(mixbuffer, frames_count);
        // RG_TIMER_LAP("rg_audio_submit");
        const int64_t afterAudio = rg_system_timer();

        audioWindowFrames += frames_count;
        audioWindowBlocked += afterAudio - beforeSubmit;
        if (audioSync)
            audio_sync_update();

        // Autosave, debounced: every cartridge write pushes the deadline back,
        // so we only touch the card once the game has gone quiet. That keeps
        // games that write constantly from hammering the SD card, and still
        // gets the save out within seconds of the player saving in-game --
        // which matters because the kit is switched off at the power rail.
        // Kept outside the timed section so a card write doesn't skew the log.
        if (autoSaveSRAM > 0)
        {
            if (gamepak_backup_dirty != lastBackupWrites)
            {
                lastBackupWrites = gamepak_backup_dirty;
                autoSaveSRAM_Timer = rg_system_timer() + (int64_t)autoSaveSRAM * 1000000;
            }
            else if (autoSaveSRAM_Timer && rg_system_timer() >= autoSaveSRAM_Timer)
            {
                sram_save();
                // A 128KB card write is real time with no emulation in it. Left
                // in the window it reads as a slowdown and detunes the sound for
                // the next few seconds; the history is fine, only this window is
                // spoiled.
                audio_sync_window();
            }
        }

        if (profileEnabled)
        {
            profile.input += afterInput - startTime;
            profile.emu += afterEmu - afterInput;
            profile.video += afterVideo - afterEmu;
            profile.audio += afterAudio - afterVideo;
            // Preparation includes sample draining/conversion AND system_tick.
            // Core sound synthesis remains inside emu. Submission includes
            // driver work AND blocking; this is not pure CPU/wait accounting.
            profile.audioPrepare += beforeSubmit - afterVideo;
            profile.audioSubmit += afterAudio - beforeSubmit;
            profile.total += afterAudio - startTime;
            profile.drawn += drawnFrame ? 1 : 0;
            if (++profile.frames >= PROFILE_FRAMES)
                profile_flush();
        }

        // Pinned HERE, at the point of use, not at the top of the loop.
        // rg_system_tick() above runs the automatic frameskip and moves
        // app->frameskip, so a value written at the top of the frame was already
        // stale by the time it got read down here -- roughly once a second, the
        // auto logic stole a frame back. At a forced 0 that showed up as 292 of
        // 300 frames drawn instead of 300, which is 7 or 8 stolen frames against
        // the ~7 auto ticks that fit in 300 frames at 40qps. Exactly the count.
        //
        // Now the automatic frameskip is simply left alone and ignored while a
        // value is forced, rather than being fought with every frame.
        if (skip_next_frame == 0)
            skip_next_frame = (forcedFrameskip >= 0) ? forcedFrameskip : app->frameskip;
        else if (skip_next_frame > 0)
            skip_next_frame--;
    }

    RG_PANIC("GBsP Ended");
}
