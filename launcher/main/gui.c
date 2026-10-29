#include <rg_system.h>
#include <string.h>
#include <stdlib.h>

#include "applications.h"
#include "gui.h"

#define HEADER_HEIGHT       (50)
#define LOGO_WIDTH          (46)
#define PREVIEW_PANEL_WIDTH ((int)(gui.width * 0.40f))
#define PREVIEW_PADDING     (4)
#define PREVIEW_HEIGHT      ((int)(gui.height * 0.55f))
#define PREVIEW_WIDTH       (PREVIEW_PANEL_WIDTH - PREVIEW_PADDING * 2)
#define ACTIONS_HEIGHT      (29)
#define ACTIONS_TOP         (gui.height - ACTIONS_HEIGHT)

#define C_HANDS_ON_LAB_BACKGROUND 0x1149
#define C_HANDS_ON_LAB_FOREGROUND 0xFFBD
#define C_HANDS_ON_LAB_ACCENT     0xFC22
#define C_HOL_PAPER              0xF79D
#define C_HOL_INK                0x2187
#define C_HOL_RULE               0xDEFB
#define C_HOL_ORANGE             0xBAE4
#define C_HOL_WHITE              0xFFFF

retro_gui_t gui;
static rg_image_t *systems_background;
static rg_image_t *button_a;
static rg_image_t *button_b;
static rg_image_t *carousel_images[7];
static rg_image_t *carousel_side_images[7];
// Keep only the current full-screen plate decoded; browsing many systems must not
// accumulate one 320x240 RGB565 surface per console.
static rg_image_t *pilot_image;
static int pilot_image_index = -1;
static rg_image_t *pilot_side_images[16];

#define SETTING_SELECTED_TAB    "SelectedTab"
#define SETTING_START_SCREEN    "StartScreen"
#define SETTING_STARTUP_MODE    "StartupMode"
#define SETTING_LANGUAGE        "Language"
#define SETTING_COLOR_THEME     "ColorTheme"
#define SETTING_SHOW_PREVIEW    "ShowPreview"
#define SETTING_SCROLL_MODE     "ScrollMode"
#define SETTING_HIDE_TAB(name)  strcat((char[99]){"HideTab."}, (name))

static int max_visible_lines(const tab_t *tab, int *_line_height)
{
    int line_height = TEXT_RECT("ABC123", 0).height;
    if (_line_height) *_line_height = line_height;
    return (ACTIONS_TOP - (HEADER_HEIGHT + 6) - (tab->navpath ? line_height : 0)) / line_height;
}

static rg_color_t gui_get_content_foreground(const tab_t *tab)
{
    if (!tab || !tab->background || !tab->background->data)
        return C_HANDS_ON_LAB_FOREGROUND;

    static const int sample_x[] = {8, 80};
    static const int sample_y[] = {72, 108, 144, 180};
    int luminance = 0;
    int samples = 0;

    for (int xi = 0; xi < RG_COUNT(sample_x); ++xi)
    {
        for (int yi = 0; yi < RG_COUNT(sample_y); ++yi)
        {
            int x = RG_MIN(sample_x[xi], tab->background->width - 1);
            int y = RG_MIN(sample_y[yi], tab->background->height - 1);
            const uint16_t *line = (const void *)tab->background->data
                                 + y * tab->background->stride;
            uint16_t pixel = line[x];
            int r = ((pixel >> 11) & 0x1F) * 255 / 31;
            int g = ((pixel >> 5) & 0x3F) * 255 / 63;
            int b = (pixel & 0x1F) * 255 / 31;
            luminance += (r * 3 + g * 6 + b) / 10;
            samples++;
        }
    }

    return luminance / samples > 150
        ? C_HANDS_ON_LAB_BACKGROUND
        : C_HANDS_ON_LAB_FOREGROUND;
}

void gui_init(bool cold_boot)
{
    gui = (retro_gui_t){
        .selected_tab = rg_settings_get_number(NS_APP, SETTING_SELECTED_TAB, 0),
        .startup_mode = rg_settings_get_number(NS_APP, SETTING_STARTUP_MODE, 0),
        .language     = rg_settings_get_number(NS_APP, SETTING_LANGUAGE, 0),
        .color_theme  = rg_settings_get_number(NS_APP, SETTING_COLOR_THEME, 0),
        .start_screen = rg_settings_get_number(NS_APP, SETTING_START_SCREEN, START_SCREEN_AUTO),
        .show_preview = rg_settings_get_number(NS_APP, SETTING_SHOW_PREVIEW, PREVIEW_MODE_SAVE_COVER),
        .scroll_mode  = rg_settings_get_number(NS_APP, SETTING_SCROLL_MODE, SCROLL_MODE_CENTER),
        .width        = rg_display_get_width(),
        .height       = rg_display_get_height(),
    };
    // Auto: Show carousel on cold boot, browser on warm boot (after cleanly exiting an emulator)
    gui.browse = gui.start_screen == START_SCREEN_BROWSER || (gui.start_screen == START_SCREEN_AUTO && !cold_boot);
    gui.theme = &gui.themes[gui.color_theme % RG_COUNT(gui.themes)];
    gui.http_lock = false;
    gui.surface = rg_surface_create(gui.width, gui.height, RG_PIXEL_565_LE, MEM_SLOW);
    gui.low_memory_mode = rg_system_get_stats().freeMemory < 0x100000;
    gui_update_theme();
}

void gui_event(gui_event_t event, tab_t *tab)
{
    if (tab && tab->event_handler)
        (*tab->event_handler)(event, tab);
}

tab_t *gui_add_tab(const char *name, const char *desc, void *arg, void *event_handler)
{
    RG_ASSERT_ARG(name && desc);

    tab_t *tab = calloc(1, sizeof(tab_t));

    snprintf(tab->name, sizeof(tab->name), "%s", name);
    snprintf(tab->desc, sizeof(tab->desc), "%s", desc);
    sprintf(tab->status[1].left, "Loading...");

    tab->event_handler = event_handler;
    tab->initialized = false;
    tab->enabled = !rg_settings_get_number(NS_APP, SETTING_HIDE_TAB(name), 0);
    tab->arg = arg;
    tab->listbox = (listbox_t){
        .items = calloc(10, sizeof(listbox_item_t)),
        .capacity = 10,
        .length = 0,
        .cursor = 0,
        .sort_mode = SORT_TEXT_ASC,
    };

    gui.tabs[gui.tabs_count++] = tab;

    RG_LOGI("Tab '%s' added at index %d\n", tab->name, gui.tabs_count - 1);

    return tab;
}

void gui_init_tab(tab_t *tab)
{
    if (!tab || tab->initialized)
        return;

    tab->initialized = true;
    // tab->status[0] = 0;

    gui_event(TAB_INIT, tab);
    gui_scroll_list(tab, SCROLL_SET, tab->listbox.cursor);
}

void gui_deinit_tab(tab_t *tab)
{
    if (!tab || !tab->initialized)
        return;

    // FIXME: Maybe the other images should be freed too?
    gui_event(TAB_DEINIT, tab);
    gui_set_preview(tab, NULL);
    gui_resize_list(tab, 10);

    tab->initialized = false;
}

tab_t *gui_get_tab(int index)
{
    return (index >= 0 && index < gui.tabs_count) ? gui.tabs[index] : NULL;
}

void gui_invalidate(void)
{
    for (size_t i = 0; i < gui.tabs_count; ++i)
        gui_deinit_tab(gui.tabs[i]);
    // Kick the user out of the tab and only re-init upon manual re-entry
    gui.browse = false;
    // gui_init_tab(gui_get_current_tab());
}

rg_image_t *gui_get_image(const char *type, const char *subtype)
{
    char name[64];

    if (gui.low_memory_mode)
        return NULL;

    if (subtype && *subtype)
        snprintf(name, sizeof(name), "%s_%s.png", type, subtype);
    else
        snprintf(name, sizeof(name), "%s.png", type);

    // Try to get image from theme
    rg_image_t *img = rg_gui_get_theme_image(name);
    if (img)
        return img;

    // Then fallback to built-in images
    for (const binfile_t **img = builtin_images; *img; img++)
    {
        if (strcmp((*img)->name, name) == 0)
            return rg_surface_load_image((*img)->data, (*img)->size, 0);
    }

    return NULL;
}

tab_t *gui_get_current_tab(void)
{
    tab_t *tab = gui_get_tab(gui.selected_tab);
    if (!tab)
        RG_LOGE("current tab is NULL!");
    return tab;
}

tab_t *gui_set_current_tab(int index)
{
    tab_t *prev_tab = gui_get_tab(gui.selected_tab);
    tab_t *curr_tab;

    index %= (int)gui.tabs_count;

    if (index < 0)
        index += gui.tabs_count;

    gui.selected_tab = index;

    curr_tab = gui_get_tab(gui.selected_tab);

    if (prev_tab && prev_tab != curr_tab)
    {
        // FIXME: We should recompress the images rather than fully free them, because if a custom theme is
        //        used then it means that we're constantly reloading from SD Card which is very slow...
        rg_surface_free(prev_tab->background), prev_tab->background = NULL;
        // rg_surface_free(prev_tab->banner), prev_tab->banner = NULL;
        // rg_surface_free(prev_tab->logo), prev_tab->logo = NULL;
    }

    return curr_tab;
}

void gui_set_status(tab_t *tab, const char *left, const char *right)
{
    if (!tab)
        tab = gui_get_current_tab();
    if (tab && left)
        strcpy(tab->status[1].left, left);
    if (tab && right)
        strcpy(tab->status[1].right, right);
}

void gui_update_theme(void)
{
    // Load our four color schemes from gui theme
    gui.themes[0].background = rg_gui_get_theme_color("launcher_1", "background", C_HANDS_ON_LAB_BACKGROUND);
    gui.themes[0].foreground = rg_gui_get_theme_color("launcher_1", "foreground", C_HANDS_ON_LAB_FOREGROUND);
    gui.themes[0].list.standard_bg = rg_gui_get_theme_color("launcher_1", "list_standard_bg", C_TRANSPARENT);
    gui.themes[0].list.standard_fg = rg_gui_get_theme_color("launcher_1", "list_standard_fg", C_HANDS_ON_LAB_FOREGROUND);
    gui.themes[0].list.selected_bg = rg_gui_get_theme_color("launcher_1", "list_selected_bg", C_HANDS_ON_LAB_ACCENT);
    gui.themes[0].list.selected_fg = rg_gui_get_theme_color("launcher_1", "list_selected_fg", C_HANDS_ON_LAB_BACKGROUND);

    gui.themes[1].background = rg_gui_get_theme_color("launcher_2", "background", C_HANDS_ON_LAB_BACKGROUND);
    gui.themes[1].foreground = rg_gui_get_theme_color("launcher_2", "foreground", C_HANDS_ON_LAB_FOREGROUND);
    gui.themes[1].list.standard_bg = rg_gui_get_theme_color("launcher_2", "list_standard_bg", C_TRANSPARENT);
    gui.themes[1].list.standard_fg = rg_gui_get_theme_color("launcher_2", "list_standard_fg", C_HANDS_ON_LAB_FOREGROUND);
    gui.themes[1].list.selected_bg = rg_gui_get_theme_color("launcher_2", "list_selected_bg", C_HANDS_ON_LAB_ACCENT);
    gui.themes[1].list.selected_fg = rg_gui_get_theme_color("launcher_2", "list_selected_fg", C_HANDS_ON_LAB_BACKGROUND);

    gui.themes[2].background = rg_gui_get_theme_color("launcher_3", "background", C_HANDS_ON_LAB_BACKGROUND);
    gui.themes[2].foreground = rg_gui_get_theme_color("launcher_3", "foreground", C_HANDS_ON_LAB_FOREGROUND);
    gui.themes[2].list.standard_bg = rg_gui_get_theme_color("launcher_3", "list_standard_bg", C_TRANSPARENT);
    gui.themes[2].list.standard_fg = rg_gui_get_theme_color("launcher_3", "list_standard_fg", C_HANDS_ON_LAB_FOREGROUND);
    gui.themes[2].list.selected_bg = rg_gui_get_theme_color("launcher_3", "list_selected_bg", C_HANDS_ON_LAB_ACCENT);
    gui.themes[2].list.selected_fg = rg_gui_get_theme_color("launcher_3", "list_selected_fg", C_HANDS_ON_LAB_BACKGROUND);

    gui.themes[3].background = rg_gui_get_theme_color("launcher_4", "background", C_HANDS_ON_LAB_BACKGROUND);
    gui.themes[3].foreground = rg_gui_get_theme_color("launcher_4", "foreground", C_HANDS_ON_LAB_FOREGROUND);
    gui.themes[3].list.standard_bg = rg_gui_get_theme_color("launcher_4", "list_standard_bg", C_TRANSPARENT);
    gui.themes[3].list.standard_fg = rg_gui_get_theme_color("launcher_4", "list_standard_fg", C_HANDS_ON_LAB_FOREGROUND);
    gui.themes[3].list.selected_bg = rg_gui_get_theme_color("launcher_4", "list_selected_bg", C_HANDS_ON_LAB_ACCENT);
    gui.themes[3].list.selected_fg = rg_gui_get_theme_color("launcher_4", "list_selected_fg", C_HANDS_ON_LAB_BACKGROUND);

    // Flush our image cache to make sure the new images are loaded next time
    for (size_t i = 0; i < gui.tabs_count; ++i)
    {
        tab_t *tab = gui.tabs[i];
        rg_surface_free(tab->background), tab->background = NULL;
        rg_surface_free(tab->banner), tab->banner = NULL;
        rg_surface_free(tab->logo), tab->logo = NULL;
    }
    rg_surface_free(systems_background), systems_background = NULL;
    rg_surface_free(pilot_image), pilot_image = NULL;
    pilot_image_index = -1;
    for (int i = 0; i < RG_COUNT(pilot_side_images); ++i)
        rg_surface_free(pilot_side_images[i]), pilot_side_images[i] = NULL;
}

void gui_save_config(void)
{
    rg_settings_set_number(NS_APP, SETTING_SELECTED_TAB, gui.selected_tab);
    rg_settings_set_number(NS_APP, SETTING_START_SCREEN, gui.start_screen);
    rg_settings_set_number(NS_APP, SETTING_SHOW_PREVIEW, gui.show_preview);
    rg_settings_set_number(NS_APP, SETTING_SCROLL_MODE, gui.scroll_mode);
    rg_settings_set_number(NS_APP, SETTING_COLOR_THEME, gui.color_theme);
    rg_settings_set_number(NS_APP, SETTING_STARTUP_MODE, gui.startup_mode);
    for (int i = 0; i < gui.tabs_count; i++)
        rg_settings_set_number(NS_APP, SETTING_HIDE_TAB(gui.tabs[i]->name), !gui.tabs[i]->enabled);
    rg_settings_commit();
}

listbox_item_t *gui_get_selected_item(tab_t *tab)
{
    if (tab && gui.browse)
    {
        listbox_t *list = &tab->listbox;
        if (list->cursor >= 0 && list->cursor < list->length)
            return &list->items[list->cursor];
    }
    return NULL;
}

static int list_comp_text_asc(const listbox_item_t *a, const listbox_item_t *b)
{
    return a->group == b->group ? strcasecmp(a->text, b->text) : ((int)a->group - b->group);
}

static int list_comp_text_desc(const listbox_item_t *a, const listbox_item_t *b)
{
    return a->group == b->group ? strcasecmp(b->text, a->text) : ((int)a->group - b->group);
}

static int list_comp_id_asc(const listbox_item_t *a, const listbox_item_t *b)
{
    return a->group == b->group ? ((int)a->order - b->order) : ((int)a->group - b->group);
}

static int list_comp_id_desc(const listbox_item_t *a, const listbox_item_t *b)
{
    return a->group == b->group ? ((int)b->order - a->order) : ((int)a->group - b->group);
}

void gui_sort_list(tab_t *tab)
{
    void *comp[] = {&list_comp_id_asc, &list_comp_id_desc, &list_comp_text_asc, &list_comp_text_desc};
    size_t sort_mode = tab->listbox.sort_mode - 1;

    if (!tab->listbox.length || sort_mode > RG_COUNT(comp) - 1)
        return;

    qsort((void*)tab->listbox.items, tab->listbox.length, sizeof(listbox_item_t), comp[sort_mode]);
}

void gui_resize_list(tab_t *tab, int new_size)
{
    listbox_t *list = &tab->listbox;

    if (new_size == list->length)
        return;

    // Always grow but only shrink past a certain threshold
    if (new_size >= list->capacity || list->capacity - new_size >= 20)
    {
        list->capacity = new_size + 10;
        list->items = realloc(list->items, list->capacity * sizeof(listbox_item_t));
        RG_LOGI("Resized list '%s' from %d to %d items (new capacity: %d)\n",
            tab->name, list->length, new_size, list->capacity);
    }

    for (int i = list->length; i < list->capacity; i++)
        memset(&list->items[i], 0, sizeof(listbox_item_t));

    list->length = new_size;

    if (list->cursor >= new_size)
        list->cursor = new_size ? new_size - 1 : 0;
}

void gui_scroll_list(tab_t *tab, scroll_whence_t mode, int arg)
{
    listbox_t *list = &tab->listbox;
    int list_length = list->length;
    int old_cursor = list->cursor;
    int new_cursor = RG_MAX(RG_MIN(old_cursor, list_length - 1), 0);

    if (list_length == 0)
    {
        // new_cursor = -1;
        new_cursor = 0;
    }
    else if (mode == SCROLL_SET)
    {
        new_cursor = arg;
    }
    else if (mode == SCROLL_LINE)
    {
        new_cursor += arg;
        // In line mode we wrap around
        if (new_cursor > list_length - 1)
            new_cursor = 0;
        else if (new_cursor < 0)
            new_cursor = list_length - 1;
    }
    else if (mode == SCROLL_PAGE)
    {
        new_cursor += arg * max_visible_lines(tab, NULL);
        // In page mode we stop at the edges
        if (new_cursor > list_length - 1)
            new_cursor = list_length - 1;
        else if (new_cursor < 0)
            new_cursor = 0;
    }

    // Check for invalid cursor
    if (new_cursor < 0 || new_cursor > list_length - 1)
    {
        RG_LOGW("Invalid cursor position: %d, list length: %d", new_cursor, list_length);
        new_cursor = 0; // -1;
    }

    if (list_length > 0 && list->items[new_cursor].arg)
        sprintf(tab->status[0].left, "%d / %d", (new_cursor + 1) % 10000, list_length % 10000);
    else
        strcpy(tab->status[0].left, "List empty");

    // if (new_cursor != old_cursor)
    {
        list->cursor = new_cursor;
        gui_event(TAB_SCROLL, tab);
    }
}

static void gui_draw_game_browser(tab_t *tab)
{
    const listbox_t *list = &tab->listbox;
    bool has_preview = tab->preview != NULL;
    int list_right = has_preview ? 201 : gui.width - 16;
    char count[32];

    rg_gui_draw_rect(0, 0, gui.width, gui.height, 0, 0, C_HOL_PAPER);
    rg_gui_draw_text(16, 11, 120, "HANDS ON LAB", C_HOL_INK,
                     C_TRANSPARENT, RG_TEXT_ALIGN_LEFT);
    rg_gui_draw_text(175, 11, 129, tab->desc, C_HOL_ORANGE,
                     C_TRANSPARENT, RG_TEXT_ALIGN_RIGHT);
    rg_gui_draw_rect(16, 31, gui.width - 32, 1, 0, 0, C_HOL_RULE);

    rg_gui_draw_text(16, 40, 100, tab->navpath ? tab->navpath : "JOGOS",
                     C_HOL_INK, C_TRANSPARENT, RG_TEXT_ALIGN_LEFT);
    snprintf(count, sizeof(count), "%d JOGOS  %d / %d", list->length,
             list->length ? list->cursor + 1 : 0, list->length);
    rg_gui_draw_text(gui.width - 139, 42, 123, count, C_HOL_INK,
                     C_TRANSPARENT, RG_TEXT_ALIGN_RIGHT);

    int first = gui.scroll_mode == SCROLL_MODE_PAGING
              ? (list->cursor / 6) * 6 : list->cursor - 2;
    first = RG_MAX(0, first);
    for (int row = 0; row < 6; ++row)
    {
        int idx = first + row;
        if (idx >= list->length) break;
        int y = 68 + row * 22;
        bool selected = idx == list->cursor;
        if (selected)
        {
            rg_gui_draw_rect(16, y - 2, list_right - 16, 21, 1,
                             C_HOL_RULE, C_HOL_WHITE);
            rg_gui_draw_rect(16, y - 2, 3, 21, 0, 0, C_HOL_ORANGE);
        }
        rg_gui_draw_text(27, y + 2, list_right - 34,
                         list->items[idx].text, C_HOL_INK,
                         C_TRANSPARENT, RG_TEXT_ALIGN_LEFT);
    }

    if (has_preview)
    {
        rg_gui_draw_rect(205, 72, 99, 118, 1, C_HOL_RULE, C_HOL_WHITE);
        int width = RG_MIN(tab->preview->width, 85);
        int height = tab->preview->height * width / tab->preview->width;
        if (height > 82)
        {
            height = 82;
            width = tab->preview->width * height / tab->preview->height;
        }
        rg_gui_draw_image(254 - width / 2, 85 + (82 - height) / 2,
                          width, height, true, tab->preview);
    }

    rg_gui_draw_rect(16, 211, gui.width - 32, 1, 0, 0, C_HOL_RULE);
    if (!button_a) button_a = gui_get_image("button", "a");
    if (!button_b) button_b = gui_get_image("button", "b");
    if (button_a) rg_gui_draw_image(16, 218, 18, 18, false, button_a);
    if (button_b) rg_gui_draw_image(196, 218, 18, 18, false, button_b);
    rg_gui_draw_text(36, 220, 72, "JOGAR", C_HOL_INK,
                     C_TRANSPARENT, RG_TEXT_ALIGN_LEFT);
    rg_gui_draw_text(216, 220, 88, "CONSOLES", C_HOL_INK,
                     C_TRANSPARENT, RG_TEXT_ALIGN_LEFT);
}

void gui_redraw(void)
{
    while (rg_display_is_busy())
        rg_task_yield(); // Wait for gui.surface to be released

    rg_gui_set_surface(gui.surface);

    tab_t *tab = gui_get_current_tab();
    if (!tab)
    {
        RG_LOGW("No tab to redraw...");
    }
    else if (gui.browse && tab->arg)
    {
        gui_draw_game_browser(tab);
    }
    else if (gui.browse)
    {
        gui_draw_background(tab, 0);
        gui_draw_header(tab, 0);
        gui_draw_status(tab);
        gui_draw_list(tab);
        gui_draw_preview(tab);
        gui_draw_actions(tab);
    }
    else
    {
        gui_draw_system_selector();
    }

    rg_gui_set_surface(NULL);
    rg_display_submit(gui.surface, 0);
}

static int gui_pilot_index(const tab_t *tab)
{
    static const char *const names[] = {
        "nes", "snes", "gb", "gbc", "gba", "gw", "sms", "gg",
        "md", "col", "pce", "lnx", "a26", "msx", "favorite", "recent",
    };
    for (int i = 0; i < RG_COUNT(names); ++i)
        if (strcmp(tab->name, names[i]) == 0)
            return i;
    return -1;
}

static bool gui_draw_pilot_selector(tab_t *const cards[3], const int image_types[3],
                                    int selected, int enabled_count)
{
    int index = gui_pilot_index(cards[1]);
    if (index < 0 || gui.width != 320 || gui.height != 240)
        return false;

    if (pilot_image_index != index)
    {
        rg_surface_free(pilot_image), pilot_image = NULL;
        pilot_image_index = index;
    }
    if (!pilot_image)
        pilot_image = gui_get_image("pilot", cards[1]->name);
    const rg_image_t *plate = pilot_image;
    // Keep the approved renderer as a fallback, including low-memory mode.
    if (!plate || plate->width != 320 || plate->height != 240)
        return false;
    rg_gui_draw_image(0, 0, 320, 240, false, plate);

    for (int card = 0; card <= 2; card += 2)
    {
        int side_index = gui_pilot_index(cards[card]);
        const rg_image_t *side = carousel_side_images[image_types[card]];
        if (side_index >= 0)
        {
            if (!pilot_side_images[side_index])
                pilot_side_images[side_index] = gui_get_image("pilot_side", cards[card]->name);
            if (pilot_side_images[side_index])
                side = pilot_side_images[side_index];
        }
        if (side && side->width == 88 && side->height == 60)
        {
            const uint16_t *edge = (const uint16_t *)side->data + (card == 0 ? 41 : 0);
            rg_gui_copy_buffer(card == 0 ? 0 : 273, 82, 47, 60, side->stride, edge, true);
        }
    }

    int dots_x = (gui.width - (enabled_count * 7 - 2)) / 2;
    for (int dot = 0; dot < enabled_count; ++dot)
    {
        int size = dot == selected ? 5 : 3;
        rg_gui_draw_rect(dots_x + dot * 7, 195 + (5 - size) / 2, size, size, 0, 0,
                         dot == selected ? 0xBAE4 : 0xDEFB);
    }
    return true;
}

void gui_draw_system_selector(void)
{
    static const char *const image_names[] = {
        "8bit", "16bit", "portable_vertical", "portable_horizontal",
        "recent", "favorite", "clock",
    };

    if (!systems_background)
        systems_background = gui_get_image("background", "systems");

    if (systems_background)
        rg_gui_draw_image(0, 0, gui.width, gui.height, false, systems_background);
    else
        rg_gui_draw_rect(0, 0, gui.width, gui.height, 0, 0, C_HANDS_ON_LAB_BACKGROUND);

    int enabled[RG_COUNT(gui.tabs)];
    int enabled_count = 0;
    int selected = 0;
    for (int i = 0; i < gui.tabs_count; ++i)
    {
        if (!gui.tabs[i]->enabled)
            continue;
        if (i == gui.selected_tab)
            selected = enabled_count;
        enabled[enabled_count++] = i;
    }

    if (enabled_count == 0)
        return;

    tab_t *current = gui.tabs[enabled[selected]];
    tab_t *previous = gui.tabs[enabled[(selected + enabled_count - 1) % enabled_count]];
    tab_t *next = gui.tabs[enabled[(selected + 1) % enabled_count]];

    int image_types[3];
    tab_t *cards[] = {previous, current, next};
    for (int card = 0; card < 3; ++card)
    {
        const char *name = cards[card]->name;
        int type = 0;

        if (strcmp(name, "snes") == 0 || strcmp(name, "md") == 0 || strcmp(name, "pce") == 0)
            type = 1;
        else if (strcmp(name, "gb") == 0 || strcmp(name, "gbc") == 0)
            type = 2;
        else if (strcmp(name, "gba") == 0 || strcmp(name, "gg") == 0 ||
                 strcmp(name, "lnx") == 0 || strcmp(name, "ngp") == 0)
            type = 3;
        else if (strcmp(name, "recent") == 0)
            type = 4;
        else if (strcmp(name, "favorite") == 0)
            type = 5;
        else if (strcmp(name, "gw") == 0)
            type = 6;

        image_types[card] = type;
        if (!carousel_images[type])
            carousel_images[type] = gui_get_image("carousel", image_names[type]);
        if (!carousel_side_images[type])
            carousel_side_images[type] = gui_get_image("carousel_side", image_names[type]);
    }

    if (gui_draw_pilot_selector(cards, image_types, selected, enabled_count))
        return;

    // Neighbor cards intentionally remain partially outside the screen: this
    // makes the horizontal navigation discoverable without animation.
    rg_gui_draw_rect(0, 64, 50, 108, 0, 0, C_HANDS_ON_LAB_ACCENT);
    rg_gui_draw_rect(0, 64, 50, 3, 0, 0, C_HANDS_ON_LAB_FOREGROUND);
    rg_gui_draw_rect(0, 169, 50, 3, 0, 0, C_HANDS_ON_LAB_FOREGROUND);
    rg_gui_draw_rect(47, 64, 3, 108, 0, 0, C_HANDS_ON_LAB_FOREGROUND);
    rg_gui_draw_rect(gui.width - 50, 64, 50, 108, 0, 0, C_HANDS_ON_LAB_ACCENT);
    rg_gui_draw_rect(gui.width - 50, 64, 50, 3, 0, 0, C_HANDS_ON_LAB_FOREGROUND);
    rg_gui_draw_rect(gui.width - 50, 169, 50, 3, 0, 0, C_HANDS_ON_LAB_FOREGROUND);
    rg_gui_draw_rect(gui.width - 50, 64, 3, 108, 0, 0, C_HANDS_ON_LAB_FOREGROUND);
    if (carousel_side_images[image_types[0]])
    {
        const rg_image_t *image = carousel_side_images[image_types[0]];
        const int visible_width = 47;
        const uint16_t *right_edge = (const uint16_t *)image->data
                                   + image->width - visible_width;
        rg_gui_copy_buffer(0, 88, visible_width, image->height,
                           image->stride, right_edge, true);
    }
    if (carousel_side_images[image_types[2]])
    {
        const rg_image_t *image = carousel_side_images[image_types[2]];
        const int visible_width = 47;
        rg_gui_copy_buffer(gui.width - visible_width, 88, visible_width, image->height,
                           image->stride, image->data, true);
    }

    rg_gui_draw_rect(72, 38, 176, 148, 4,
                     C_HANDS_ON_LAB_ACCENT, C_HANDS_ON_LAB_ACCENT);
    rg_gui_draw_rect(78, 44, 164, 136, 3,
                     C_HANDS_ON_LAB_FOREGROUND, C_HANDS_ON_LAB_ACCENT);
    if (carousel_images[image_types[1]])
        rg_gui_draw_image(96, 51, 128, 88, true, carousel_images[image_types[1]]);

    const char *label = current->desc;
    if (strcmp(current->name, "nes") == 0) label = "NINTENDO 8-BIT";
    else if (strcmp(current->name, "snes") == 0) label = "SUPER NINTENDO";
    else if (strcmp(current->name, "gb") == 0) label = "GAME BOY";
    else if (strcmp(current->name, "gbc") == 0) label = "GAME BOY COLOR";
    else if (strcmp(current->name, "gba") == 0) label = "GAME BOY ADVANCE";
    else if (strcmp(current->name, "gw") == 0) label = "GAME & WATCH";
    else if (strcmp(current->name, "sms") == 0) label = "MASTER SYSTEM";
    else if (strcmp(current->name, "gg") == 0) label = "GAME GEAR";
    else if (strcmp(current->name, "md") == 0) label = "MEGA DRIVE";
    else if (strcmp(current->name, "col") == 0) label = "COLECOVISION";
    else if (strcmp(current->name, "pce") == 0) label = "PC ENGINE";
    else if (strcmp(current->name, "lnx") == 0) label = "ATARI LYNX";
    else if (strcmp(current->name, "a26") == 0) label = "ATARI 2600";
    else if (strcmp(current->name, "favorite") == 0) label = "FAVORITOS";
    else if (strcmp(current->name, "recent") == 0) label = "RECENTES";

    rg_gui_draw_rect(81, 143, 158, 34, 1,
                     C_HANDS_ON_LAB_FOREGROUND, C_HANDS_ON_LAB_BACKGROUND);
    const int label_flags = RG_TEXT_ALIGN_CENTER | RG_TEXT_NO_PADDING;
    rg_gui_draw_text(82, 154, 156, label, C_HANDS_ON_LAB_FOREGROUND,
                     C_TRANSPARENT, label_flags);
    rg_gui_draw_text(83, 154, 156, label, C_HANDS_ON_LAB_FOREGROUND,
                     C_TRANSPARENT, label_flags);
    rg_gui_draw_text(54, 104, 18, "<", C_HANDS_ON_LAB_ACCENT,
                     C_TRANSPARENT, RG_TEXT_ALIGN_CENTER | RG_TEXT_BIGGER);
    rg_gui_draw_text(gui.width - 72, 104, 18, ">", C_HANDS_ON_LAB_ACCENT,
                     C_TRANSPARENT, RG_TEXT_ALIGN_CENTER | RG_TEXT_BIGGER);

    int dots_width = enabled_count * 7 - 2;
    int dots_x = (gui.width - dots_width) / 2;
    for (int dot = 0; dot < enabled_count; ++dot)
    {
        int size = dot == selected ? 5 : 3;
        int y = 195 + (5 - size) / 2;
        rg_gui_draw_rect(dots_x + dot * 7, y, size, size, 0, 0,
                         dot == selected ? C_HANDS_ON_LAB_ACCENT : C_HANDS_ON_LAB_FOREGROUND);
    }

    if (!button_a)
        button_a = gui_get_image("button", "a");
    rg_gui_draw_rect(0, ACTIONS_TOP, gui.width, 1, 0, 0, C_HANDS_ON_LAB_FOREGROUND);
    if (button_a)
        rg_gui_draw_image(70, ACTIONS_TOP + 6, 18, 18, false, button_a);
    rg_gui_draw_text(93, ACTIONS_TOP + 8, 96, "JOGOS", C_HANDS_ON_LAB_FOREGROUND,
                     C_TRANSPARENT, RG_TEXT_ALIGN_LEFT);
}

void gui_draw_preview(tab_t *tab)
{
    rg_color_t foreground = gui_get_content_foreground(tab);
    if (tab->preview)
    {
        int width = RG_MIN(tab->preview->width, PREVIEW_WIDTH);
        int height = tab->preview->height * width / tab->preview->width;
        if (height > PREVIEW_HEIGHT)
        {
            height = PREVIEW_HEIGHT;
            width = tab->preview->width * height / tab->preview->height;
        }

        int panel_x = gui.width - PREVIEW_PANEL_WIDTH;
        int x = panel_x + (PREVIEW_PANEL_WIDTH - width) / 2;
        int y = HEADER_HEIGHT + (ACTIONS_TOP - HEADER_HEIGHT - height) / 2;

        rg_gui_draw_rect(x - 2, y - 2, width + 4, height + 4, 2,
                         foreground, C_HANDS_ON_LAB_BACKGROUND);
        rg_gui_draw_image(x, y, width, height, true, tab->preview);
    }
}

void gui_draw_background(tab_t *tab, int shade)
{
    // We can't losslessly change shade, must reload!
    if (tab->background && tab->background_shade > 0 && tab->background_shade != shade)
    {
        rg_surface_free(tab->background);
        tab->background = NULL;
    }

    if (!tab->background)
    {
        tab->background = gui_get_image("background", tab->name); // Try background_<tabname>.png
        if (!tab->background)
            tab->background = gui_get_image("background", NULL); // Fallback to a background.png
        tab->background_shade = 0;
        if (tab->background && (tab->background->width != gui.width || tab->background->height != gui.height))
        {
            rg_image_t *temp = rg_surface_resize(tab->background, gui.width, gui.height);
            if (temp)
            {
                rg_surface_free(tab->background);
                tab->background = temp;
            }
        }
    }

    if (tab->background && tab->background_shade != shade && shade > 0)
    {
        rg_image_t *img = tab->background;
        for (int y = 0; y < img->height; ++y)
        {
            uint16_t *line = img->data + y * img->stride;
            for (int x = 0; x < img->width; ++x)
            {
                int pixel = line[x];
                int r = ((pixel >> 11) & 0x1F) / shade;
                int g = ((pixel >> 5) & 0x3F) / shade;
                int b = ((pixel) & 0x1F) / shade;
                line[x] = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | ((b & 0x1F) << 0);
            }
        }
        tab->background_shade = shade;
    }

    if (tab->background)
        rg_gui_draw_image(0, 0, gui.width, gui.height, false, tab->background);
    else
        rg_gui_draw_rect(0, 0, gui.width, gui.height, 0, 0, gui.theme->background);
}

void gui_draw_header(tab_t *tab, int offset)
{
    rg_color_t foreground = gui_get_content_foreground(tab);
    if (!tab->banner)
        tab->banner = gui_get_image("banner", tab->name);
    if (!tab->logo)
        tab->logo = gui_get_image("logo", tab->name);

    rg_gui_draw_image(0, offset, LOGO_WIDTH, HEADER_HEIGHT, false, tab->logo);
    if (tab->banner)
        rg_gui_draw_image(LOGO_WIDTH + 1, offset + 8, 0, HEADER_HEIGHT - 8, false, tab->banner);
    else
        rg_gui_draw_text(LOGO_WIDTH + 8, offset + 8, 0, tab->desc, foreground, C_TRANSPARENT, RG_TEXT_BIGGER);
}

void gui_draw_tab_indicator(void)
{
    char buffer[64] = {0};
    memset(buffer, '-', gui.tabs_count);
    rg_gui_draw_text(RG_GUI_CENTER, RG_GUI_BOTTOM, 0, buffer, C_DIM_GRAY, C_TRANSPARENT, RG_TEXT_BIGGER|RG_TEXT_MONOSPACE);
    memset(buffer, ' ', gui.tabs_count);
    buffer[gui.selected_tab] = '-';
    rg_gui_draw_text(RG_GUI_CENTER, RG_GUI_BOTTOM, 0, buffer, C_SNOW, C_TRANSPARENT, RG_TEXT_BIGGER|RG_TEXT_MONOSPACE);
}

void gui_draw_status(tab_t *tab)
{
    rg_color_t foreground = gui_get_content_foreground(tab);
    const int status_x = LOGO_WIDTH + 12;
    const int status_y = HEADER_HEIGHT - 16;
    const int status_width = gui.width - status_x - 88;
    char *txt_left = tab->status[tab->status[1].left[0] ? 1 : 0].left;
    char *txt_right = tab->status[tab->status[1].right[0] ? 1 : 0].right;

    rg_gui_draw_text(status_x, status_y, status_width, txt_right, foreground, C_TRANSPARENT, RG_TEXT_ALIGN_RIGHT);
    rg_gui_draw_text(status_x, status_y, 0, txt_left, foreground, C_TRANSPARENT, RG_TEXT_ALIGN_LEFT);
    rg_gui_draw_icons();
}

void gui_draw_actions(tab_t *tab)
{
    const int button_y = ACTIONS_TOP + 6;
    rg_color_t foreground = gui_get_content_foreground(tab);

    if (!button_a)
        button_a = gui_get_image("button", "a");
    if (!button_b)
        button_b = gui_get_image("button", "b");

    rg_gui_draw_rect(0, ACTIONS_TOP, gui.width, 1, 0, 0, foreground);

    if (button_a)
        rg_gui_draw_image(6, button_y, 18, 18, false, button_a);
    rg_gui_draw_text(27, button_y + 2, 72, "JOGAR", foreground,
                     C_TRANSPARENT, RG_TEXT_ALIGN_LEFT);

    if (button_b)
        rg_gui_draw_image(gui.width - 92, button_y, 18, 18, false, button_b);
    rg_gui_draw_text(gui.width - 69, button_y + 2, 69, "CONSOLES",
                     foreground, C_TRANSPARENT, RG_TEXT_ALIGN_LEFT);
}

void gui_draw_list(tab_t *tab)
{
    rg_color_t foreground = gui_get_content_foreground(tab);
    rg_color_t fg[2] = {foreground, gui.theme->list.selected_fg};
    rg_color_t bg[2] = {gui.theme->list.standard_bg, gui.theme->list.selected_bg};

    const listbox_t *list = &tab->listbox;
    int list_width = tab->preview ? gui.width - PREVIEW_PANEL_WIDTH - PREVIEW_PADDING : gui.width;
    int line_height, top = HEADER_HEIGHT + 6;
    int lines = max_visible_lines(tab, &line_height);
    int line_offset = 0;

    if (tab->navpath)
    {
        char buffer[64];
        snprintf(buffer, 63, "[%s]",  tab->navpath);
        top += rg_gui_draw_text(0, top, list_width, buffer, foreground, C_TRANSPARENT, 0).height;
    }

    top += ((ACTIONS_TOP - top) - (lines * line_height)) / 2;

    if (gui.scroll_mode == SCROLL_MODE_PAGING)
    {
        line_offset = (list->cursor / lines) * lines;
    }
    else // (gui.scroll_mode == SCROLL_MODE_CENTER)
    {
        line_offset = list->cursor - (lines / 2);
    }

    for (int i = 0; i < lines; i++)
    {
        int idx = line_offset + i;
        int selected = idx == list->cursor;
        char *label = (idx >= 0 && idx < list->length) ? list->items[idx].text : "";
        int row_top = top;
        top += rg_gui_draw_text(0, top, list_width, label, fg[selected], bg[selected], 0).height;
        if (selected)
            rg_gui_draw_rect(0, row_top, 3, line_height, 0, 0, C_HANDS_ON_LAB_ACCENT);
    }
}

void gui_set_preview(tab_t *tab, rg_image_t *preview)
{
    if (!tab)
        return;

    if (tab->preview)
        rg_surface_free(tab->preview);

    tab->preview = preview;
}

void gui_load_preview(tab_t *tab)
{
    listbox_item_t *item = gui_get_selected_item(tab);
    bool show_missing_cover = false;
    uint32_t order;

    gui_set_preview(tab, NULL);

    if (!item || !item->arg || gui.low_memory_mode)
        return;

    switch (gui.show_preview)
    {
        case PREVIEW_MODE_COVER_SAVE:
            show_missing_cover = true;
            order = 0x4123;
            break;
        case PREVIEW_MODE_SAVE_COVER:
            show_missing_cover = true;
            order = 0x1234;
            break;
        case PREVIEW_MODE_COVER_ONLY:
            show_missing_cover = true;
            order = 0x0123;
            break;
        case PREVIEW_MODE_SAVE_ONLY:
            show_missing_cover = false;
            order = 0x0004;
            break;
        default:
            show_missing_cover = false;
            order = 0x0000;
    }

    retro_file_t *file = item->arg;
    retro_app_t *app = file->app;
    uint32_t errors = 0;

    while (order && !tab->preview)
    {
        char path[RG_PATH_MAX + 1];
        size_t path_len = 0;
        int type = order & 0xF;

        order >>= 4;

        // Give up on any button press to improve responsiveness
        if ((gui.joystick |= rg_input_read_gamepad()))
            break;

        if (file->missing_cover & (1 << type))
            continue;

        if (type == 0x1 && app->use_crc_covers && application_get_file_crc32(file)) // Game cover (old format)
            path_len = snprintf(path, RG_PATH_MAX, "%s/%X/%08X.art", app->paths.covers, (int)(file->checksum >> 28), (int)file->checksum);
        else if (type == 0x2 && app->use_crc_covers && application_get_file_crc32(file)) // Game cover (png)
            path_len = snprintf(path, RG_PATH_MAX, "%s/%X/%08X.png", app->paths.covers, (int)(file->checksum >> 28), (int)file->checksum);
        else if (type == 0x3) // Game cover (based on filename)
        {
            path_len = snprintf(path, RG_PATH_MAX, "%s/%s", app->paths.covers, file->name);
            if (path_len < RG_PATH_MAX - 3) // Don't bother if we already have an overflow
                strcpy(path + path_len - strlen(rg_extension(file->name) ?: ""), "png");
        }
        else if (type == 0x4 && file->saves > 0) // Save state screenshot (png)
        {
            snprintf(path, RG_PATH_MAX, "%s/%s", file->folder, file->name);
            uint8_t last_used_slot = rg_emu_get_last_used_slot(path);
            if (last_used_slot != 0xFF)
            {
                char *preview = rg_emu_get_path(RG_PATH_SCREENSHOT + last_used_slot, path);
                path_len = snprintf(path, RG_PATH_MAX, "%s", preview);
                free(preview);
            }
        }

        if (path_len > 0 && path_len < RG_PATH_MAX)
        {
            RG_LOGD("Looking for %s", path);
            gui_set_preview(tab, rg_surface_load_image_file(path, 0));
            // if (!tab->preview && rg_storage_exists(path))
            //     errors++;
        }

        file->missing_cover |= (tab->preview ? 0 : 1) << type;
    }

    if (!tab->preview && file->checksum && (show_missing_cover || errors))
    {
        RG_LOGD("No image found for '%s'\n", file->name);
        gui_set_status(tab, NULL, errors ? "Bad cover" : "No cover");
        // gui_draw_status(tab);
        // tab->preview = gui_get_image("cover", file->app);
    }
}
