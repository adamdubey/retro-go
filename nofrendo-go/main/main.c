#include <rg_system.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <nofrendo.h>
#include <nes/input.h>
#include <nes/state.h>

#define AUDIO_SAMPLE_RATE   (32000)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 50 + 1)

static uint16_t myPalette[64];
static rg_video_frame_t frames[2];
static rg_video_frame_t *currentUpdate = &frames[0];
static rg_video_frame_t *previousUpdate = NULL;

static gamepad_state_t joystick1;
static gamepad_state_t *localJoystick = &joystick1;

static bool overscan = true;
static long autocrop = 0;
static bool fullFrame = 0;
static long palette = 0;
static nes_t *nes;

static rg_app_desc_t *app;

#ifdef ENABLE_NETPLAY
static gamepad_state_t *remoteJoystick = &joystick2;
static gamepad_state_t joystick2;

static bool netplay = false;
#endif

static const char *SETTING_AUTOCROP = "autocrop";
static const char *SETTING_OVERSCAN = "overscan";
static const char *SETTING_PALETTE = "palette";
static const char *SETTING_SPRITELIMIT = "spritelimit";
// --- MAIN


static void netplay_handler(netplay_event_t event, void *arg)
{
#ifdef ENABLE_NETPLAY
    bool new_netplay;

    switch (event)
    {
    case NETPLAY_EVENT_STATUS_CHANGED:
        new_netplay = (rg_netplay_status() == NETPLAY_STATUS_CONNECTED);

        if (netplay && !new_netplay)
        {
            rg_gui_alert("Netplay", "Connection lost!");
        }
        else if (!netplay && new_netplay)
        {
            // displayScalingMode = RG_DISPLAY_SCALING_FILL;
            // displayFilterMode = RG_DISPLAY_FILTER_NONE;
            // forceVideoRefresh = true;
            input_connect(1, NES_JOYPAD);
            nes_reset(true);
        }

        netplay = new_netplay;
        break;

    default:
        break;
    }

    if (netplay && rg_netplay_mode() == NETPLAY_MODE_GUEST)
    {
        localJoystick = &joystick2;
        remoteJoystick = &joystick1;
    }
    else
    {
        localJoystick = &joystick1;
        remoteJoystick = &joystick2;
    }
#endif
}

static bool screenshot_handler(const char *filename, int width, int height)
{
	return rg_display_save_frame(filename, currentUpdate, width, height);
}

static bool save_state_handler(const char *filename)
{
    return state_save(filename) == 0;
}

static bool load_state_handler(const char *filename)
{
    if (state_load(filename) != 0)
    {
        nes_reset(true);
        return false;
    }
    return true;
}

static bool reset_handler(bool hard)
{
    nes_reset(hard);
    return true;
}


static void build_palette(int n)
{
    uint16_t *pal = nofrendo_buildpalette(n, 16);
    for (int i = 0; i < 64; i++)
        myPalette[i] = (pal[i] >> 8) | ((pal[i]) << 8);
    free(pal);
    previousUpdate = NULL;
}

static dialog_return_t sprite_limit_cb(dialog_option_t *option, dialog_event_t event)
{
    bool spritelimit = ppu_getopt(PPU_LIMIT_SPRITES);

    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        spritelimit = !spritelimit;
        rg_settings_set_app_int32(SETTING_SPRITELIMIT, spritelimit);
        ppu_setopt(PPU_LIMIT_SPRITES, spritelimit);
    }

    strcpy(option->value, spritelimit ? "On " : "Off");

    return RG_DIALOG_IGNORE;
}

static dialog_return_t overscan_update_cb(dialog_option_t *option, dialog_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        overscan = !overscan;
        rg_settings_set_app_int32(SETTING_OVERSCAN, overscan);
    }

    strcpy(option->value, overscan ? "Auto" : "Off ");

    return RG_DIALOG_IGNORE;
}

static dialog_return_t autocrop_update_cb(dialog_option_t *option, dialog_event_t event)
{
    int val = autocrop;
    int max = 2;

    if (event == RG_DIALOG_PREV) val = val > 0 ? val - 1 : max;
    if (event == RG_DIALOG_NEXT) val = val < max ? val + 1 : 0;

    if (val != autocrop)
    {
        rg_settings_set_app_int32(SETTING_AUTOCROP, val);
        autocrop = val;
    }

    if (val == 0) strcpy(option->value, "Never ");
    if (val == 1) strcpy(option->value, "Auto  ");
    if (val == 2) strcpy(option->value, "Always");

    return RG_DIALOG_IGNORE;
}

static dialog_return_t palette_update_cb(dialog_option_t *option, dialog_event_t event)
{
    int pal = palette;
    int max = NES_PALETTE_COUNT - 1;

    if (event == RG_DIALOG_PREV) pal = pal > 0 ? pal - 1 : max;
    if (event == RG_DIALOG_NEXT) pal = pal < max ? pal + 1 : 0;

    if (pal != palette)
    {
        palette = pal;
        rg_settings_set_app_int32(SETTING_PALETTE, pal);
        build_palette(pal);
        rg_display_queue_update(currentUpdate, NULL);
        rg_display_queue_update(currentUpdate, NULL);
        usleep(50000);
    }

    if (pal == NES_PALETTE_NOFRENDO) sprintf(option->value, "%.7s", "Default");
    if (pal == NES_PALETTE_COMPOSITE) sprintf(option->value, "%.7s", "Composite");
    if (pal == NES_PALETTE_NESCLASSIC) sprintf(option->value, "%.7s", "NES Classic");
    if (pal == NES_PALETTE_NTSC) sprintf(option->value, "%.7s", "NTSC");
    if (pal == NES_PALETTE_PVM) sprintf(option->value, "%.7s", "PVM");
    if (pal == NES_PALETTE_SMOOTH) sprintf(option->value, "%.7s", "Smooth");

    return RG_DIALOG_IGNORE;
}

static void settings_handler(void)
{
    const dialog_option_t options[] = {
        {1, "Palette     ", "Default", 1, &palette_update_cb},
        {2, "Overscan    ", "Auto ", 1, &overscan_update_cb},
        {3, "Crop sides  ", "Never", 1, &autocrop_update_cb},
        {4, "Sprite limit", "On   ", 1, &sprite_limit_cb},
        RG_DIALOG_CHOICE_LAST
    };
    rg_gui_dialog("Advanced", options, 0);
}


static void osd_blitscreen(uint8 *bmp)
{
    int crop_v = (overscan) ? nes->overscan : 0;
    int crop_h = (autocrop == 2) || (autocrop == 1 && nes->ppu->left_bg_counter > 210) ? 8 : 0;

    // A rolling average should be used for autocrop == 1, it causes jitter in some games...

    currentUpdate->buffer = NES_SCREEN_GETPTR(bmp, crop_h, crop_v);
    currentUpdate->width = NES_SCREEN_WIDTH - (crop_h * 2);
    currentUpdate->height = NES_SCREEN_HEIGHT - (crop_v * 2);

    fullFrame = rg_display_queue_update(currentUpdate, previousUpdate) == RG_UPDATE_FULL;

    previousUpdate = currentUpdate;
    currentUpdate = &frames[currentUpdate == &frames[0]];
}

void app_main(void)
{
    rg_emu_proc_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .netplay = &netplay_handler,
        .screenshot = &screenshot_handler,
        .settings = &settings_handler,
    };

    app = rg_system_init(AUDIO_SAMPLE_RATE, &handlers);

    frames[0].flags = RG_PIXEL_PAL|RG_PIXEL_565|RG_PIXEL_BE;
    frames[0].stride = NES_SCREEN_PITCH;
    frames[0].pixel_mask = 0x3F;
    frames[0].palette = myPalette;
    frames[1] = frames[0];

    overscan = rg_settings_get_app_int32(SETTING_OVERSCAN, 1);
    autocrop = rg_settings_get_app_int32(SETTING_AUTOCROP, 0);
    palette = rg_settings_get_app_int32(SETTING_PALETTE, 0);

    nes = nes_init(SYS_DETECT, AUDIO_SAMPLE_RATE, true);
    if (!nes)
    {
        RG_PANIC("Init failed.");
    }

    int ret = nes_insertcart(app->romPath, "/sd/bios/fds_bios.bin");
    if (ret == -1)
        RG_PANIC("ROM load failed.");
    else if (ret == -2)
        RG_PANIC("Unsupported mapper.");
    else if (ret == -3)
        RG_PANIC("BIOS file required.");
    else if (ret < 0)
        RG_PANIC("Unsupported ROM.");

    app->refreshRate = nes->refresh_rate;
    nes->blit_func = osd_blitscreen;

    ppu_setopt(PPU_LIMIT_SPRITES, rg_settings_get_app_int32(SETTING_SPRITELIMIT, 1));
    build_palette(palette);

    // This is necessary for successful state restoration
    // I have not yet investigated why that is...
    nes_emulate(false);
    nes_emulate(false);

    if (app->startAction == RG_START_ACTION_RESUME)
    {
        rg_emu_load_state(0);
    }

    int frameTime = get_frame_time(nes->refresh_rate);
    int drawframe = false;
    int skipFrames = 0;

    while (true)
    {
        int64_t startTime = get_elapsed_time();
        unsigned input = 0;

        *localJoystick = rg_input_read_gamepad();

        if (*localJoystick & GAMEPAD_KEY_MENU)
        {
            rg_gui_game_menu();
        }
        else if (*localJoystick & GAMEPAD_KEY_VOLUME)
        {
            rg_gui_game_settings_menu();
        }

    #ifdef ENABLE_NETPLAY
        if (netplay)
        {
            rg_netplay_sync(localJoystick, remoteJoystick, sizeof(gamepad_state_t));
            if (joystick2 & GAMEPAD_KEY_START)  input |= NES_PAD_START;
            if (joystick2 & GAMEPAD_KEY_SELECT) input |= NES_PAD_SELECT;
            if (joystick2 & GAMEPAD_KEY_UP)     input |= NES_PAD_UP;
            if (joystick2 & GAMEPAD_KEY_RIGHT)  input |= NES_PAD_RIGHT;
            if (joystick2 & GAMEPAD_KEY_DOWN)   input |= NES_PAD_DOWN;
            if (joystick2 & GAMEPAD_KEY_LEFT)   input |= NES_PAD_LEFT;
            if (joystick2 & GAMEPAD_KEY_A)      input |= NES_PAD_A;
            if (joystick2 & GAMEPAD_KEY_B)      input |= NES_PAD_B;
        }
        input_update(1, input);
        input = 0;
    #endif

        if (joystick1 & GAMEPAD_KEY_START)  input |= NES_PAD_START;
        if (joystick1 & GAMEPAD_KEY_SELECT) input |= NES_PAD_SELECT;
        if (joystick1 & GAMEPAD_KEY_UP)     input |= NES_PAD_UP;
        if (joystick1 & GAMEPAD_KEY_RIGHT)  input |= NES_PAD_RIGHT;
        if (joystick1 & GAMEPAD_KEY_DOWN)   input |= NES_PAD_DOWN;
        if (joystick1 & GAMEPAD_KEY_LEFT)   input |= NES_PAD_LEFT;
        if (joystick1 & GAMEPAD_KEY_A)      input |= NES_PAD_A;
        if (joystick1 & GAMEPAD_KEY_B)      input |= NES_PAD_B;

        input_update(0, input);

        nes_emulate(drawframe);

        int elapsed = get_elapsed_time_since(startTime);

        if (skipFrames == 0)
        {
            if (app->speedupEnabled)
                skipFrames = app->speedupEnabled * 2;
            else if (elapsed >= frameTime) // Frame took too long
                skipFrames = 1;
            else if (drawframe && fullFrame) // This could be avoided when scaling != full
                skipFrames = 1;
        }
        else if (skipFrames > 0)
        {
            skipFrames--;
        }

        // Tick before submitting audio/syncing
        rg_system_tick(elapsed);

        drawframe = (skipFrames == 0);

        // Use audio to throttle emulation
        if (!app->speedupEnabled)
        {
            rg_audio_submit(nes->apu->buffer, nes->apu->samples_per_frame);
        }
    }

    RG_PANIC("Nofrendo died!");
}
