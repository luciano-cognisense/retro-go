#include <rg_system.h>
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
static int autoSaveSRAM = 0;
// Deadline in microseconds (0 = disarmed). Wall clock, not frames: gbsp runs
// well below 60fps, so a frame counter would stretch the interval unpredictably.
static int64_t autoSaveSRAM_Timer = 0;
static int sramSaveCount = 0;

// Reserved at boot, before init_gamepak_buffer() eats the heap. See app_main().
static void *stateBuffer = NULL;

static const char *SETTING_SOUND_EMULATION = "sound";
static const char *SETTING_SAVESRAM = "SaveSRAM";

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
    gamepak_backup_dirty = 0;
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
    gamepak_backup_dirty = 0;
    autoSaveSRAM_Timer = 0;
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

    autoSaveSRAM_Timer = 0;
    return success;
}

static bool reset_handler(bool hard)
{
    reset_gba();
    autoSaveSRAM_Timer = 0;
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
// SRAM state in the options menu. "clean"/"dirty" is the backup flag, the
// countdown is the autosave deadline, and the last number counts saves made.
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

static void options_handler(rg_gui_option_t *dest)
{
    *dest++ = (rg_gui_option_t){0, _("Audio enable"),  "-", RG_DIALOG_FLAG_NORMAL, &sound_toggle_cb};
    *dest++ = (rg_gui_option_t){0, _("SRAM autosave"), "-", RG_DIALOG_FLAG_NORMAL, &sram_autosave_cb};
    *dest++ = (rg_gui_option_t){0, _("SRAM state"),    "-", RG_DIALOG_FLAG_NORMAL, &sram_status_cb};
    *dest++ = (rg_gui_option_t)RG_DIALOG_END;
}

void app_main(void)
{
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
    autoSaveSRAM = (int)rg_settings_get_number(NS_APP, SETTING_SAVESRAM, 0);

    sramFile = rg_emu_get_path(RG_PATH_SAVE_SRAM, app->romPath);
    if (!rg_storage_mkdir(rg_dirname(sramFile)))
        RG_LOGE("Unable to create SRAM folder...");

    updates[0] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_FAST);
    updates[0]->height = GBA_SCREEN_HEIGHT;
    // updates[1] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_FAST);
    // updates[1]->height = GBA_SCREEN_HEIGHT;
    currentUpdate = updates[0];

    gba_screen_pixels = currentUpdate->data;

    gbsp_memory = rg_alloc(sizeof(*gbsp_memory), MEM_ANY);
    RG_LOGI("gbsp_memory=%p", gbsp_memory);

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
        execute_arm(execute_cycles);
        // RG_TIMER_LAP("execute_arm");

        if (autoSaveSRAM > 0)
        {
            if (autoSaveSRAM_Timer == 0)
            {
                if (gamepak_backup_dirty)
                    autoSaveSRAM_Timer = rg_system_timer() + (int64_t)autoSaveSRAM * 1000000;
            }
            else if (rg_system_timer() >= autoSaveSRAM_Timer)
            {
                sram_save();
            }
        }

        if (!skip_next_frame)
            rg_display_submit(currentUpdate, 0);

        size_t frames_count = sound_read_samples((s16 *)mixbuffer, AUDIO_BUFFER_LENGTH);
        // RG_TIMER_LAP("sound_read_samples");

        rg_system_tick(rg_system_timer() - startTime);

        rg_audio_submit(mixbuffer, frames_count);
        // RG_TIMER_LAP("rg_audio_submit");

        if (skip_next_frame == 0)
            skip_next_frame = app->frameskip;
        else if (skip_next_frame > 0)
            skip_next_frame--;
    }

    RG_PANIC("GBsP Ended");
}
