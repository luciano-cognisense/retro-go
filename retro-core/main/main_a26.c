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

// A janela vertical.
//
// O 2600 não tem resolução vertical: cada jogo escolhe quantas linhas de
// VBLANK põe antes de desenhar e quantas linhas desenha. Não dá para perguntar
// ao jogo onde a imagem dele começa — muitos nem ligam VBLANK no fim do quadro
// (o River Patrol deixa desligado o quadro inteiro), então o registrador não
// serve de sinal. O que sobra é medir.
//
// Medido nos cinco jogos que tenho aqui, olhando quais linhas de fato recebem
// pixel:
//
//   Breakout      TIA  40-230   River Patrol  TIA  44-234
//   Decathlon     TIA  39-238   River Raid    TIA  39-244
//   Frogger II    TIA  44-233
//
// A união é 39-244, ou seja 206 linhas. Com a janela começando na 34 o fim
// caía na 243 e o River Raid perdia a última linha — justamente onde ficam o
// combustível e as vidas. Começando na 36 a janela cobre 36-245 e sobra margem
// para os dois lados em todos os cinco.
#define A26_LARGURA   160
#define A26_LINHA0     36
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
static const char *SETTING_CONTROLE = "controle";   // 0 = joystick, 1 = pá
static const char *SETTING_PENTE = "pente";         // 0 = fiel, 1 = escondido
static int sistema_video;
static int dific_p0, dific_p1;
static int controle_pa;
static int esconde_pente;

// A raquete virtual. O aparelho tem um direcional de oito posições e o jogo
// espera um potenciômetro; a conversão é integrar a direção no tempo. A
// velocidade cresce enquanto a tecla fica presa: um toque dá o ajuste fino que
// o Breakout exige perto da parede, e segurar atravessa a tela em meio segundo.
#define PA_MIN        3      // passos por quadro no começo
#define PA_MAX       12      // passos por quadro com a tecla presa
#define PA_ACELERA   14      // quadros até chegar na velocidade máxima
static int pa_pos = 128;
static int pa_segurando;

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

// O "pente" do HMOVE: os oito pixels apagados na borda esquerda das linhas em
// que o jogo usa HMOVE para mover objetos. É comportamento legítimo do console
// — aparece na televisão de verdade, e o River Raid e o Space Invaders fazem
// isso em linha sim, linha não. Mas numa tela de 2,4 polegadas aquilo lê como
// defeito, então quem quiser pode desligar. O padrão é fiel.
static rg_gui_event_t pente_cb(rg_gui_option_t *opt, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        esconde_pente = !esconde_pente;
        rg_settings_set_number(NS_APP, SETTING_PENTE, esconde_pente);
        a26_set_pente_hmove(&console, esconde_pente == 0);
        return RG_DIALOG_REDRAW;
    }
    strcpy(opt->value, esconde_pente ? "Hidden" : "Faithful");
    return RG_DIALOG_VOID;
}

// Joystick ou pá. Não dá para adivinhar pelo jogo: nada na ROM diz qual
// controle ela espera, e vários títulos aceitam os dois. Então é escolha de
// quem joga — e sem ela metade do catálogo de 1977 a 1980 não funciona.
static rg_gui_event_t controle_cb(rg_gui_option_t *opt, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT) {
        controle_pa = !controle_pa;
        rg_settings_set_number(NS_APP, SETTING_CONTROLE, controle_pa);
        a26_set_paddles_ligadas(&console, controle_pa != 0);
        pa_pos = 128;
        return RG_DIALOG_REDRAW;
    }
    strcpy(opt->value, controle_pa ? "Paddle" : "Joystick");
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
    *dest++ = (rg_gui_option_t){0, _("Controller"), "-", RG_DIALOG_FLAG_NORMAL, &controle_cb};
    *dest++ = (rg_gui_option_t){0, _("TV system"), "-", RG_DIALOG_FLAG_NORMAL, &sistema_cb};
    *dest++ = (rg_gui_option_t){0, _("Difficulty P1"), "-", RG_DIALOG_FLAG_NORMAL, &dific_p0_cb};
    *dest++ = (rg_gui_option_t){0, _("Difficulty P2"), "-", RG_DIALOG_FLAG_NORMAL, &dific_p1_cb};
    *dest++ = (rg_gui_option_t){0, _("HMOVE bar"), "-", RG_DIALOG_FLAG_NORMAL, &pente_cb};
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

    // Marcos de partida. O panic do aparelho entrega endereços de retorno, e
    // sem o .elf casado isso não vira nome de função. Uma linha por etapa
    // troca "travou em algum lugar da partida" por "travou entre a etapa N e
    // a N+1". Custa microssegundos, então fica.
    RG_LOGI("a26: superficies criadas");

    sistema_video = rg_settings_get_number(NS_APP, SETTING_SISTEMA, 0);
    dific_p0 = rg_settings_get_number(NS_APP, SETTING_DIFIC_P0, 0);
    dific_p1 = rg_settings_get_number(NS_APP, SETTING_DIFIC_P1, 0);
    controle_pa = rg_settings_get_number(NS_APP, SETTING_CONTROLE, 0);
    esconde_pente = rg_settings_get_number(NS_APP, SETTING_PENTE, 0);
    RG_LOGI("a26: opcoes lidas (sistema=%d dif=%d/%d controle=%s)",
            sistema_video, dific_p0, dific_p1, controle_pa ? "pa" : "joystick");

    aplica_paleta();
    RG_LOGI("a26: paleta aplicada");

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
    RG_LOGI("a26: rom lida (%d bytes)", (int)rom_size);

    memset(&console, 0, sizeof(console));
    a26_set_framebuffer(&console, currentUpdate->data, A26_LARGURA,
                        A26_LINHA0, A26_ALTURA);
    a26_set_audio_buffer(&console, audio_a26, A26_AUDIO_MAX);
    if (!a26_load(&console, rom_data, rom_size))
        RG_PANIC("Unsupported cartridge bankswitching scheme!");

    a26_set_paddles_ligadas(&console, controle_pa != 0);
    a26_set_pente_hmove(&console, esconde_pente == 0);

    // %zu não é usado de propósito: o alvo compila com CONFIG_NEWLIB_NANO_FORMAT,
    // e a implementação reduzida de printf não cobre todos os modificadores de
    // tamanho. Um int com cast explícito não depende disso.
    RG_LOGI("a26: %d bytes, esquema %s", (int)rom_size, a26_esquema(&console));

    if (app->bootFlags & RG_BOOT_RESUME)
        rg_emu_load_state(app->saveSlot);

    rg_system_set_tick_rate(sistema_video ? 50 : 60);
    app->frameskip = 0;

    int skipFrames = 0;

    // Acumuladores do perfil de quadro. Ver o bloco no fim do laço.
    struct {
        int64_t entrada, emu_des, emu_pul, video, audio, total, ciclos;
        int n, desenhados;
    } perf = {0};
    static char perf_texto[1024];
    static int perf_texto_len;
    perf_texto_len = 0;

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
        if (controle_pa) {
            // Modo pá: o direcional não vai para o console. Esquerda e direita
            // giram o potenciômetro, e o botão entra pela linha do SWCHA, que
            // é por onde o botão da pá entra no aparelho de verdade.
            // Mais resistência move a raquete para a ESQUERDA — medido com
            // o Breakout, varrendo as 256 posições e olhando onde ela caía.
            // Por isso "direita" diminui o valor.
            int dir = 0;
            if (joystick & RG_KEY_LEFT)  dir += 1;
            if (joystick & RG_KEY_RIGHT) dir -= 1;

            if (dir == 0) {
                pa_segurando = 0;
            } else {
                int v = PA_MIN + (PA_MAX - PA_MIN) *
                        (pa_segurando < PA_ACELERA ? pa_segurando : PA_ACELERA) / PA_ACELERA;
                pa_pos += dir * v;
                if (pa_pos < 0)   pa_pos = 0;
                if (pa_pos > 255) pa_pos = 255;
                if (pa_segurando < PA_ACELERA)
                    pa_segurando++;
            }
            a26_set_paddle(&console, 0, (uint8_t)pa_pos);
            if (joystick & (RG_KEY_A | RG_KEY_B)) botoes |= A26_PA_BOTAO0;
        } else {
            if (joystick & RG_KEY_UP)     botoes |= A26_CIMA;
            if (joystick & RG_KEY_DOWN)   botoes |= A26_BAIXO;
            if (joystick & RG_KEY_LEFT)   botoes |= A26_ESQUERDA;
            if (joystick & RG_KEY_RIGHT)  botoes |= A26_DIREITA;
            if (joystick & (RG_KEY_A | RG_KEY_B)) botoes |= A26_GATILHO;
        }

        // As duas chaves do painel: START é o RESET (que no 2600 é "começar o
        // jogo", não "reiniciar o aparelho") e SELECT escolhe a variação.
        if (joystick & RG_KEY_START)  botoes |= A26_RESET;
        if (joystick & RG_KEY_SELECT) botoes |= A26_SELECT;
        if (dific_p0) botoes |= A26_DIFIC_P0;
        if (dific_p1) botoes |= A26_DIFIC_P1;

        a26_set_input(&console, botoes);
        const int64_t t_entrada = rg_system_timer();

        // Pular quadro é simplesmente não dar framebuffer à TIA: o compositor
        // por pixel, que é mais da metade do custo, nem roda.
        a26_set_framebuffer(&console, drawFrame ? currentUpdate->data : NULL,
                            A26_LARGURA, A26_LINHA0, A26_ALTURA);

        const uint64_t ciclos_antes = console.ciclos;
        int amostras = a26_run_frame(&console);
        const int64_t t_emu = rg_system_timer();

        if (drawFrame) {
            slowFrame = rg_display_is_busy();
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }
        const int64_t t_video = rg_system_timer();

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
        const int64_t t_fim = rg_system_timer();

        // Onde o tempo do quadro vai parar.
        //
        // Isto existe porque uma otimização de 1,78x no núcleo (medida, e
        // idêntica bit a bit) não mexeu nada no indicador do aparelho. `emu` é
        // exatamente `a26_run_frame`, então dá para comparar com os 647 us que
        // a mesma função leva no host e saber quantas vezes o ESP32 é mais
        // lento por unidade de trabalho. `video` inclui a espera pela tarefa de
        // vídeo: `rg_display_submit` bloqueia enquanto ela ainda está mandando
        // o quadro anterior pelo SPI.
        //
        // Vai para arquivo, e não só para o serial, porque nesta PCB o serial
        // só fica acessível com o ESP32 fora da placa — e aí não há controle
        // nem tela para chegar até um jogo.
        perf.entrada += t_entrada - startTime;
        // Separado de propósito: num quadro pulado a TIA não recebe
        // framebuffer, então a diferença entre os dois é exatamente o custo do
        // compositor no aparelho. É o número que decide se vale reescrever o
        // compositor por faixas ou se o gargalo está em outro lugar.
        if (drawFrame)
            perf.emu_des += t_emu - t_entrada;
        else
            perf.emu_pul += t_emu - t_entrada;
        perf.video   += t_video - t_emu;
        perf.audio   += t_fim - t_video;
        perf.total   += t_fim - startTime;
        perf.ciclos  += console.ciclos - ciclos_antes;
        perf.n++;
        perf.desenhados += drawFrame ? 1 : 0;

        if (perf.n >= 300) {
            char linha[200];
            int pulados = perf.n - perf.desenhados;
            int len = snprintf(linha, sizeof(linha),
                "%d quadros: %d desenhados, %d pulados | entrada %d | emu-desenhado %d | "
                "emu-pulado %d | video %d | audio %d | total %d us | %d qps | "
                "%d ciclos de CPU/quadro\n",
                perf.n, perf.desenhados, pulados,
                (int)(perf.entrada / perf.n),
                (int)(perf.desenhados ? perf.emu_des / perf.desenhados : 0),
                (int)(pulados ? perf.emu_pul / pulados : 0),
                (int)(perf.video / perf.n), (int)(perf.audio / perf.n),
                (int)(perf.total / perf.n),
                (int)(1000000LL * perf.n / (perf.total ? perf.total : 1)),
                (int)(perf.ciclos / perf.n));
            RG_LOGI("perfil: %s", linha);

            // Mantém as últimas medidas num buffer e reescreve o arquivo
            // inteiro. Uma gravação a cada 300 quadros não perturba a medição,
            // e o arquivo está sempre completo mesmo se o aparelho for
            // desligado no tapa.
            if (perf_texto_len + len >= (int)sizeof(perf_texto))
                perf_texto_len = 0;
            memcpy(perf_texto + perf_texto_len, linha, (size_t)len + 1);
            perf_texto_len += len;
            rg_storage_write_file(RG_BASE_PATH "/a26-perfil.txt",
                                  perf_texto, (size_t)perf_texto_len, 0);

            memset(&perf, 0, sizeof(perf));
        }

        // Nunca mais de um quadro pulado seguido.
        //
        // O retro-go tem um controlador automático que sobe o frameskip
        // enquanto `speed < 96%` e `busy > 85%`, e só desce quando
        // `speed > 99%` **e** `busy < 85%`. Aqui ele encontra um caso que a
        // regra não prevê: o emulador chega a 100% de velocidade justamente
        // *por causa* do pulo, e `busy` fica em 98% porque estamos no limite —
        // então a condição de descer nunca acontece e ele estaciona em 5. Era
        // isso que dava 50 quadros desenhados em 300, ou seja, imagem a 10 por
        // segundo com a lógica do jogo correndo a 60.
        //
        // A regra abaixo é a do próprio quadro: pula um se o anterior estourou
        // o orçamento, e no máximo um. Isso garante 30 quadros por segundo na
        // tela. Se um jogo for pesado demais para isso, a conta aparece como
        // `speed` abaixo de 100 no menu — o que é honesto, e visível.
        if (skipFrames == 0) {
            int elapsed = rg_system_timer() - startTime;
            if (elapsed > app->frameTime + 1500)
                skipFrames = 1;
            else if (drawFrame && slowFrame)
                skipFrames = 1;
        } else if (skipFrames > 0) {
            skipFrames--;
        }
    }
}
