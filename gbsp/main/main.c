#include <rg_system.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>

#include "../components/gbsp-libretro/common.h"
#include "../components/gbsp-libretro/memmap.h"
#include "../components/gbsp-libretro/sound.h"
#include "../components/gbsp-libretro/gba_memory.h"
#include "../components/gbsp-libretro/gba_cc_lut.h"

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

// Performance log, same idea as the a26 core's a26-perfil.txt: one line every
// PROFILE_FRAMES frames with where the time actually went. Reading SPEED% off
// the menu only says how bad it is; this says which phase to attack.
#define PROFILE_FRAMES 300
#define PROFILE_FILE RG_BASE_PATH "/gbsp-perfil.txt"

static int profileEnabled = 1;
static bool profileHeaderWritten = false;
// Internal RAM available right before we try to place iwram there. Logged in
// the profile header so a failed placement says how much was missing.
static int internalFreeKB = 0, internalBlockKB = 0;
static struct
{
    int frames, drawn;
    int64_t input, emu, video, audio, total;
    int64_t rtcStart;
} profile;

// The RTC clock is the only timebase on this chip that overclocking does not
// skew -- rg_system_set_overclock() itself relies on it for that reason, and
// measurements showed rg_system_timer() (esp_timer) speeding up with the PLL,
// which would hide the very gains we are measuring. So: the window's wall time
// comes from the RTC, and the per-phase esp_timer deltas are scaled to it.
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
#define AUDIO_SYNC_WINDOW_US 1000000
#define AUDIO_SYNC_HISTORY   8
#define AUDIO_SYNC_MIN_RATE  (AUDIO_SAMPLE_RATE / 4)

static int audioSync = 1;
static int audioRate = AUDIO_SAMPLE_RATE; // rate currently programmed on the DAC
static int audioCapability = 0;           // last window's measurement, for the log
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
    // percent, given up waiting in i2s_write().
    int target = audio_sync_median();
    target -= target >> 6;
    target = (target + 256) & ~511; // no point chasing single hertz
    target = RG_MIN(RG_MAX(target, AUDIO_SYNC_MIN_RATE), AUDIO_SAMPLE_RATE);

    // Hysteresis, widened to ~6% to match the median: re-programming the rate
    // stops the I2S channel and clears its DMA buffers, which is a click of its
    // own, and every move is a step in pitch. Better to sit slightly wrong and
    // hold still than to be continually almost right.
    if (abs(target - audioRate) * 16 > audioRate)
    {
        audioRate = target;
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

static void profile_flush(void)
{
    if (profile.frames < 1)
        return;

    FILE *fp = fopen(PROFILE_FILE, "a");
    if (!fp)
    {
        RG_LOGE("Could not open the profile log (%s)", PROFILE_FILE);
        profile.frames = 0; // don't retry every 300 frames
        return;
    }

    if (!profileHeaderWritten)
    {
        // One header per session so the lines below can be attributed to a
        // build/configuration when comparing experiments.
        // 0x3fc.../0x3fd... is internal SRAM on the S3, 0x3c... is PSRAM.
        fprintf(fp, "\n=== gbsp %s | %s | iwram %s | fb %s | cache de ROM %dMB"
                    " | RAM interna no boot: %dKB livre, maior bloco %dKB ===\n",
                app->version ? app->version : "?",
                app->romPath ? rg_basename(app->romPath) : "?",
                ((u32)iwram_ptr >> 24) == 0x3c ? "PSRAM" : "interna",
                ((u32)currentUpdate->data >> 24) == 0x3c ? "PSRAM" : "interna",
                (int)gamepak_buffer_count, internalFreeKB, internalBlockKB);
        profileHeaderWritten = true;
    }

    rg_stats_t stats = rg_system_get_stats();
    int n = profile.frames;

    // Scale the measured phases so they add up to the window's real duration
    // (see the esp_rtc_get_time_us note above). "clk" in the line below is the
    // ratio we observed: 1.00 means esp_timer agreed with the RTC, anything
    // higher means it was running fast -- which is what overclocking does here.
    int mhz = rg_system_get_cpu_speed();
    int64_t realWindow = (int64_t)esp_rtc_get_time_us() - profile.rtcStart;
    double clk = (profile.total > 0 && realWindow > 0) ? (double)realWindow / profile.total : 1.0;
    double toRealUs = clk / n;

    fprintf(fp, "%d quadros: %d desenhados, %d pulados | oc %d (%dMHz) | skip %d | entrada %d"
                " | emu %d | rom %d (%d faults) | video %d | audio %d | total %d us"
                " | %d qps | clk %d.%02d | vel %d%% | ocupado %d%% | dac %dHz (cap %d)\n",
            n, profile.drawn, n - profile.drawn,
            rg_system_get_overclock(), mhz, (int)app->frameskip,
            (int)(profile.input * toRealUs), (int)(profile.emu * toRealUs),
            (int)(gamepak_page_time * toRealUs), (int)gamepak_page_faults,
            (int)(profile.video * toRealUs), (int)(profile.audio * toRealUs),
            (int)(profile.total * toRealUs),
            (int)(profile.total ? (1000000.0 / (profile.total * toRealUs)) : 0),
            (int)(clk + 0.005), (int)((clk + 0.005 - (int)(clk + 0.005)) * 100),
            (int)(stats.speedPercent + 0.5f), (int)(stats.busyPercent + 0.5f),
            audioRate, audioCapability);

    fclose(fp);
    memset(&profile, 0, sizeof(profile));
    // "rom" is a slice of "emu", not a sibling: the page faults happen inside
    // execute_arm(). Reset per window like everything else.
    gamepak_page_faults = 0;
    gamepak_page_time = 0;
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
            memset(&profile, 0, sizeof(profile)); // start a clean window
    }

    strcpy(option->value, profileEnabled ? _("On") : _("Off"));

    return RG_DIALOG_VOID;
}

static void options_handler(rg_gui_option_t *dest)
{
    *dest++ = (rg_gui_option_t){0, _("Audio enable"),  "-", RG_DIALOG_FLAG_NORMAL, &sound_toggle_cb};
    *dest++ = (rg_gui_option_t){0, _("Audio sync"),    "-", RG_DIALOG_FLAG_NORMAL, &audio_sync_cb};
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
            if (profile.frames == 0)
                profile.rtcStart = (int64_t)esp_rtc_get_time_us();
            profile.input += afterInput - startTime;
            profile.emu += afterEmu - afterInput;
            profile.video += afterVideo - afterEmu;
            profile.audio += afterAudio - afterVideo;
            profile.total += afterAudio - startTime;
            profile.drawn += drawnFrame ? 1 : 0;
            if (++profile.frames >= PROFILE_FRAMES)
                profile_flush();
        }

        if (skip_next_frame == 0)
            skip_next_frame = app->frameskip;
        else if (skip_next_frame > 0)
            skip_next_frame--;
    }

    RG_PANIC("GBsP Ended");
}
