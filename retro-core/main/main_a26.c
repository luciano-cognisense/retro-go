// main_a26.c — Atari 2600 dentro do retro-core.
//
// O núcleo (retro-core/components/a26) é independente do retro-go: ele carrega
// uma ROM, desenha num framebuffer que alguém fornece e devolve amostras de
// som. Este arquivo é só a cola.
//
// Duas decisões que valem explicação:
//
// 1. O framebuffer é a **superfície do retro-go**, sem cópia intermediária. A
//    TIA escreve índices de cor de 8 bits e o retro-go tem um formato para
//    exatamente isso (RG_PIXEL_PAL565), então a paleta faz a conversão na hora
//    de mostrar. Isso economiza uma cópia de 33 KB por quadro e, mais
//    importante, tira o framebuffer de dentro do struct da TIA — 51 KB que não
//    caberiam com folga na RAM interna do ESP32.
//
// 2. O 2600 não tem "linhas visíveis" fixas: cada jogo escolhe quantas linhas
//    desenha, e os quatro que testei vão de 262 a 280. Em vez de adivinhar, o
//    núcleo recebe uma janela vertical fixa e o que cair fora simplesmente não
//    é escrito.
//
// Licença: GPLv2 (mesma do retro-go).

#include "shared.h"

#include <a26.h>
#include <paleta.h>

#if RG_SCREEN_PIXEL_FORMAT == 0
#define FB_PIXEL_FORMAT RG_PIXEL_PAL565_BE
#define PALETA_BE true
#else
#define FB_PIXEL_FORMAT RG_PIXEL_PAL565_LE
#define PALETA_BE false
#endif

// A janela vertical. Um quadro NTSC tem 3 linhas de VSYNC + 37 de VBLANK antes
// da parte visível, que tem 192. Começar um pouco antes e terminar um pouco
// depois dá margem para os jogos que fogem do padrão sem cortar imagem.
#define A26_LARGURA   160
#define A26_LINHA0     34
#define A26_ALTURA    210

// Som: o 2600 produz duas amostras por linha de varredura, ~31,4 kHz em NTSC.
// O aparelho toca a 32 kHz. A diferença é de 2%, então a reamostragem é uma
// repetição ocasional de amostra — inaudível, e muito mais barata que um
// filtro de verdade num chip que já está no limite.
#define A26_AUDIO_MAX  1024

static rg_app_t *app;
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;
static a26_t console;
static uint8_t *rom_data;
static size_t rom_size;
static int16_t audio_a26[A26_AUDIO_MAX];

static const char *SETTING_SISTEMA = "sistema";     // 0 = NTSC, 1 = PAL
static const char *SETTING_DIFIC_P0 = "dificP0";
static const char *SETTING_DIFIC_P1 = "dificP1";
static int sistema_video;
static int dific_p0, dific_p1;

// --- estado -----------------------------------------------------------

// O save state é o struct do console inteiro, menos os ponteiros. Escrever os
// ponteiros seria pior que inútil: eles não valem nada na próxima execução, e
// restaurá-los faria a CPU chamar um callback num endereço morto.
static bool save_state_handler(const char *filename)
{
    FILE *f = fopen(filename, "wb");
    if (!f)
        return false;
    bool ok = fwrite(&console, sizeof(console), 1, f) == 1;
    fclose(f);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f) {
        a26_reset(&console);
        return false;
    }
    a26_t tmp;
    bool ok = fread(&tmp, sizeof(tmp), 1, f) == 1;
    fclose(f);
    if (!ok) {
        a26_reset(&console);
        return false;
    }

    // Os callbacks de barramento vivos são os que já estão no console; os do
    // arquivo apontam para endereços da execução que gravou o estado.
    m6502_t vivo = console.cpu;
    console = tmp;
    console.cpu.read = vivo.read;
    console.cpu.write = vivo.write;
    console.cpu.ctx = &console;
    console.cart.rom = rom_data;
    console.cart.tam = rom_size;
    a26_set_framebuffer(&console, currentUpdate->data, A26_LARGURA,
                        A26_LINHA0, A26_ALTURA);
    a26_set_audio_buffer(&console, audio_a26, A26_AUDIO_MAX);
    return true;
}

static bool reset_handler(bool hard)
{
    a26_reset(&console);
    return true;
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(currentUpdate, 0);
}

// --- opções -----------------------------------------------------------

static void aplica_paleta(void)
{
    a26_paleta(updates[0]->palette, sistema_video, PALETA_BE);
    memcpy(updates[1]->palette, updates[0]->palette, 512);
}

static rg_gui_event_t sistema_cb(rg_gui_option_t *opt, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        sistema_video = !sistema_video;
        rg_settings_set_number(NS_APP, SETTING_SISTEMA, sistema_video);
        aplica_paleta();
        return RG_DIALOG_REDRAW;
    }
    strcpy(opt->value, sistema_video ? "PAL" : "NTSC");
    return RG_DIALOG_VOID;
}

// As duas chaves de dificuldade do painel do console. Muito jogo muda de
// comportamento com elas — e num aparelho sem as chaves físicas, quem não
// expõe isso no menu simplesmente não tem como jogar metade das variações.
static rg_gui_event_t dific_p0_cb(rg_gui_option_t *opt, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        dific_p0 = !dific_p0;
        rg_settings_set_number(NS_APP, SETTING_DIFIC_P0, dific_p0);
        return RG_DIALOG_REDRAW;
    }
    strcpy(opt->value, dific_p0 ? "A" : "B");
    return RG_DIALOG_VOID;
}

static rg_gui_event_t dific_p1_cb(rg_gui_option_t *opt, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        dific_p1 = !dific_p1;
        rg_settings_set_number(NS_APP, SETTING_DIFIC_P1, dific_p1);
        return RG_DIALOG_REDRAW;
    }
    strcpy(opt->value, dific_p1 ? "A" : "B");
    return RG_DIALOG_VOID;
}

static void options_handler(rg_gui_option_t *dest)
{
    *dest++ = (rg_gui_option_t){0, _("TV system"), "-", RG_DIALOG_FLAG_NORMAL, &sistema_cb};
    *dest++ = (rg_gui_option_t){0, _("Difficulty P1"), "-", RG_DIALOG_FLAG_NORMAL, &dific_p0_cb};
    *dest++ = (rg_gui_option_t){0, _("Difficulty P2"), "-", RG_DIALOG_FLAG_NORMAL, &dific_p1_cb};
    *dest++ = (rg_gui_option_t)RG_DIALOG_END;
}

// --- principal --------------------------------------------------------

void a26_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
        .options = &options_handler,
    };

    app = rg_system_reinit(AUDIO_SAMPLE_RATE, &handlers, NULL);

    updates[0] = rg_surface_create(A26_LARGURA, A26_ALTURA, FB_PIXEL_FORMAT, MEM_FAST);
    updates[1] = rg_surface_create(A26_LARGURA, A26_ALTURA, FB_PIXEL_FORMAT, MEM_FAST);
    currentUpdate = updates[0];

    sistema_video = rg_settings_get_number(NS_APP, SETTING_SISTEMA, 0);
    dific_p0 = rg_settings_get_number(NS_APP, SETTING_DIFIC_P0, 0);
    dific_p1 = rg_settings_get_number(NS_APP, SETTING_DIFIC_P1, 0);
    aplica_paleta();

    // O cartucho guarda um ponteiro para a ROM em vez de copiá-la, então este
    // buffer tem de viver enquanto o emulador viver.
    if (rg_extension_match(app->romPath, "zip")) {
        void *data;
        size_t size;
        if (!rg_storage_unzip_file(app->romPath, NULL, &data, &size, 0))
            RG_PANIC("ROM file unzipping failed!");
        rom_data = data;
        rom_size = size;
    } else if (!rg_storage_read_file(app->romPath, (void **)&rom_data, &rom_size, 0)) {
        RG_PANIC("ROM file loading failed!");
    }

    memset(&console, 0, sizeof(console));
    a26_set_framebuffer(&console, currentUpdate->data, A26_LARGURA,
                        A26_LINHA0, A26_ALTURA);
    a26_set_audio_buffer(&console, audio_a26, A26_AUDIO_MAX);
    if (!a26_load(&console, rom_data, rom_size))
        RG_PANIC("Unsupported cartridge bankswitching scheme!");

    RG_LOGI("Atari 2600: %zu bytes, esquema %s", rom_size, a26_esquema(&console));

    if (app->bootFlags & RG_BOOT_RESUME)
        rg_emu_load_state(app->saveSlot);

    rg_system_set_tick_rate(sistema_video ? 50 : 60);
    app->frameskip = 0;

    int skipFrames = 0;

    while (true) {
        const int64_t startTime = rg_system_timer();
        uint32_t joystick = rg_input_read_gamepad();
        bool drawFrame = !skipFrames;
        bool slowFrame = false;

        if (joystick & (RG_KEY_MENU | RG_KEY_OPTION)) {
            if (joystick & RG_KEY_MENU)
                rg_gui_game_menu();
            else
                rg_gui_options_menu();
            continue;
        }

        // O 2600 tem uma alavanca e **um** botão. A e B fazem os dois o mesmo
        // gatilho — quem cresceu com o joystick original espera isso, e quem
        // não cresceu não vai procurar qual dos dois é.
        uint16_t botoes = 0;
        if (joystick & RG_KEY_UP)     botoes |= A26_CIMA;
        if (joystick & RG_KEY_DOWN)   botoes |= A26_BAIXO;
        if (joystick & RG_KEY_LEFT)   botoes |= A26_ESQUERDA;
        if (joystick & RG_KEY_RIGHT)  botoes |= A26_DIREITA;
        if (joystick & (RG_KEY_A | RG_KEY_B)) botoes |= A26_GATILHO;

        // As duas chaves do painel: START é o RESET (que no 2600 é "começar o
        // jogo", não "reiniciar o aparelho") e SELECT escolhe a variação.
        if (joystick & RG_KEY_START)  botoes |= A26_RESET;
        if (joystick & RG_KEY_SELECT) botoes |= A26_SELECT;
        if (dific_p0) botoes |= A26_DIFIC_P0;
        if (dific_p1) botoes |= A26_DIFIC_P1;

        a26_set_input(&console, botoes);

        // Pular quadro é simplesmente não dar framebuffer à TIA: o compositor
        // por pixel, que é mais da metade do custo, nem roda.
        a26_set_framebuffer(&console, drawFrame ? currentUpdate->data : NULL,
                            A26_LARGURA, A26_LINHA0, A26_ALTURA);

        int amostras = a26_run_frame(&console);

        if (drawFrame) {
            slowFrame = rg_display_is_busy();
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }

        // Reamostragem de 31,4 kHz para a taxa do aparelho, por passo fixo.
        size_t saida = AUDIO_SAMPLE_RATE / (sistema_video ? 50 : 60);
        rg_audio_sample_t mix[saida];
        if (amostras > 0) {
            for (size_t i = 0; i < saida; ++i) {
                int j = (int)((i * (size_t)amostras) / saida);
                mix[i].left = mix[i].right = audio_a26[j];
            }
        } else {
            memset(mix, 0, sizeof(mix));
        }

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(mix, saida);

        if (skipFrames == 0) {
            int elapsed = rg_system_timer() - startTime;
            if (app->frameskip > 0)
                skipFrames = app->frameskip;
            else if (elapsed > app->frameTime + 1500)
                skipFrames = 1;
            else if (drawFrame && slowFrame)
                skipFrames = 1;
        } else if (skipFrames > 0) {
            skipFrames--;
        }
    }
}
