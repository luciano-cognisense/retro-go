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
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60 + 1)

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
                " | %d qps | clk %d.%02d | vel %d%% | ocupado %d%%\n",
            n, profile.drawn, n - profile.drawn,
            rg_system_get_overclock(), mhz, (int)app->frameskip,
            (int)(profile.input * toRealUs), (int)(profile.emu * toRealUs),
            (int)(gamepak_page_time * toRealUs), (int)gamepak_page_faults,
            (int)(profile.video * toRealUs), (int)(profile.audio * toRealUs),
            (int)(profile.total * toRealUs),
            (int)(profile.total ? (1000000.0 / (profile.total * toRealUs)) : 0),
            (int)(clk + 0.005), (int)((clk + 0.005 - (int)(clk + 0.005)) * 100),
            (int)(stats.speedPercent + 0.5f), (int)(stats.busyPercent + 0.5f));

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
    }

    // The autosave deadline is left alone on purpose: a save state does not
    // touch gamepak_backup, so anything still unsaved stays unsaved and must
    // keep its pending flush.
    return success;
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

    // Always load the cartridge backup, even when resuming a save state:
    // gba_load_state() does not restore gamepak_backup (see gba_memory.c),
    // and reset_gba()/init_memory() do not clear it either.
    sram_load();

    if (app->bootFlags & RG_BOOT_RESUME)
    {
        RG_LOGI("load_state");
        rg_emu_load_state(app->saveSlot);
    }

    RG_LOGI("emulation loop");

    rg_audio_sample_t mixbuffer[AUDIO_BUFFER_LENGTH] = {0};

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
        // paces the loop except rg_audio_submit() blocking when the audio
        // buffer is full -- so a large "audio" figure in the log means we are
        // waiting on audio (running fast enough), and a near-zero one means
        // emulation is the wall.
        rg_audio_submit(mixbuffer, frames_count);
        // RG_TIMER_LAP("rg_audio_submit");
        const int64_t afterAudio = rg_system_timer();

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
