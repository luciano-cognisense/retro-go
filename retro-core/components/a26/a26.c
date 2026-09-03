// a26.c — o console montado. Ver a26.h.
// Licença: GPLv2 (mesma do retro-go).

#include <string.h>
#include "a26.h"
#include "tia_audio.h"

// O 6507 tem 13 linhas de endereço, então o mapa inteiro do console cabe em
// $0000-$1FFF e é decodificado por três bits:
//
//   A12 ligado                             -> cartucho
//   A12 desligado, A7 desligado            -> TIA
//   A12 desligado, A7 ligado, A9 desligado -> os 128 bytes de RAM do RIOT
//   A12 desligado, A7 ligado, A9 ligado    -> registradores do RIOT
//
// Não há decodificação mais fina: é por isso que cada registrador aparece
// espelhado dezenas de vezes no espaço de endereços, e é por isso que jogos
// escrevem em endereços aparentemente aleatórios que funcionam.

// Uma amostra de som é colhida quando a fase 1 do relógio de áudio passa —
// duas vezes por linha, nos color clocks 37 e 149.
static void colhe_audio(a26_t *c, int clock_antes)
{
    // A fase 1 do relógio de áudio roda nos color clocks 37 e 149; a amostra
    // vale logo depois dela. Duas por linha, 2 x 262 x 60 = 31 440 Hz em NTSC.
    if ((clock_antes != 37 && clock_antes != 149) ||
        !c->audio || c->audio_len >= c->audio_cap)
        return;

    // A soma dos dois canais vai de 0 a 30. Centralizada e escalada para
    // caber com folga num int16 sem estourar quando os dois canais estão no
    // volume máximo.
    int v = (int)tia_audio_sample(&c->tia.audio);
    c->audio[c->audio_len++] = (int16_t)((v - 15) * 800);
}

// Um ciclo de CPU = 3 color clocks. Enquanto o WSYNC segurar o RDY, a TIA
// anda sozinha até o fim da linha antes de a CPU conseguir o barramento — é
// assim que o WSYNC "sai de graça" numa arquitetura dirigida por barramento.
// Avança `n` color clocks em blocos, parando exatamente nos dois pontos em que
// o som é amostrado.
//
// A versão anterior chamava `tia_tick(&c->tia, 1)` uma vez por color clock, só
// para poder olhar o relógio entre um e outro. São três chamadas por ciclo de
// CPU, e 228 por linha de WSYNC — medido com gprof, o prólogo e o epílogo
// dessas chamadas eram 31% do tempo de quadro. Aqui a TIA recebe o bloco
// inteiro e o corte é feito onde ele importa.
static void avanca_n(a26_t *c, int n)
{
    while (n > 0) {
        int cl = c->tia.clock;

        // Até onde dá para ir sem passar de um ponto de amostragem.
        int passo;
        if (cl <= 37)
            passo = 37 - cl + 1;
        else if (cl <= 149)
            passo = 149 - cl + 1;
        else
            passo = TIA_CLOCKS_PER_LINE - cl;
        if (passo > n)
            passo = n;

        int ultimo = cl + passo - 1;              // último color clock do bloco
        tia_tick(&c->tia, passo);
        colhe_audio(c, ultimo);
        n -= passo;
    }
}

static void avanca(a26_t *c)
{
    // WSYNC: a CPU fica parada até o fim da linha.
    while (!c->tia.rdy)
        avanca_n(c, TIA_CLOCKS_PER_LINE - c->tia.clock);

    avanca_n(c, 3);
    c->ciclos++;
}

static uint8_t ler(void *ctx, uint16_t addr)
{
    a26_t *c = ctx;
    avanca(c);
    addr &= 0x1FFF;

    uint8_t v;
    if (addr & 0x1000)
        v = cart_read(&c->cart, addr);
    else if (!(addr & 0x80))
        v = tia_read(&c->tia, addr);
    else
        v = riot_read(&c->riot, addr & 0x2FF, c->ciclos);

    // O cartucho escuta o barramento inteiro: 3F e FE dependem disso.
    cart_snoop(&c->cart, addr, v, false);
    return v;
}

static void escrever(void *ctx, uint16_t addr, uint8_t val)
{
    a26_t *c = ctx;
    avanca(c);
    addr &= 0x1FFF;

    if (addr & 0x1000)
        cart_write(&c->cart, addr, val);   // é ROM, mas o hotspot vale
    else if (!(addr & 0x80))
        tia_write(&c->tia, addr, val);
    else
        riot_write(&c->riot, addr & 0x2FF, val, c->ciclos);

    cart_snoop(&c->cart, addr, val, true);
}

bool a26_load(a26_t *c, const uint8_t *rom, size_t tam)
{
    uint8_t *fb = c->tia.fb;
    int stride = c->tia.fb_stride, l0 = c->tia.fb_linha0, nl = c->tia.fb_linhas;
    int16_t *aud = c->audio;
    int aud_cap = c->audio_cap;

    memset(c, 0, sizeof(*c));
    if (!cart_load(&c->cart, rom, tam))
        return false;

    c->audio = aud;
    c->audio_cap = aud_cap;
    tia_set_framebuffer(&c->tia, fb, stride, l0, nl);
    a26_reset(c);
    return true;
}

void a26_reset(a26_t *c)
{
    riot_reset(&c->riot);
    tia_reset(&c->tia);
    cart_reset(&c->cart);
    c->ciclos = 0;
    c->audio_len = 0;

    c->cpu.ctx = c;
    c->cpu.read = ler;
    c->cpu.write = escrever;
    c->cpu.s = 0xFD;
    m6502_reset(&c->cpu);
}

void a26_set_framebuffer(a26_t *c, uint8_t *fb, int stride, int linha0, int linhas)
{
    tia_set_framebuffer(&c->tia, fb, stride, linha0, linhas);
}

void a26_set_audio_buffer(a26_t *c, int16_t *buf, int capacidade)
{
    c->audio = buf;
    c->audio_cap = capacidade;
    c->audio_len = 0;
}

void a26_set_input(a26_t *c, uint16_t b)
{
    // SWCHA, porta A do RIOT: as direções dos dois joysticks, **ativas em
    // nível baixo**. Bits 7-4 são o jogador 1 (direita, esquerda, baixo,
    // cima); bits 3-0 o jogador 2, que o kit não tem.
    uint8_t a = 0xFF;
    if (b & A26_DIREITA)  a &= (uint8_t)~0x80;
    if (b & A26_ESQUERDA) a &= (uint8_t)~0x40;
    if (b & A26_BAIXO)    a &= (uint8_t)~0x20;
    if (b & A26_CIMA)     a &= (uint8_t)~0x10;
    c->riot.in_a = a;

    // SWCHB, porta B: as chaves do painel. Também ativas em nível baixo, o que
    // significa que o estado "solto" é 1 — e um emulador que zera esta porta
    // faz todo jogo achar que o RESET está preso desde que o console ligou.
    //   bit 0 = RESET      bit 1 = SELECT      bit 3 = cor (1) / preto e branco (0)
    //   bit 6 = dificuldade do jogador 1       bit 7 = dificuldade do jogador 2
    uint8_t s = 0xFF;
    if (b & A26_RESET)           s &= (uint8_t)~0x01;
    if (b & A26_SELECT)          s &= (uint8_t)~0x02;
    if (b & A26_PRETO_E_BRANCO)  s &= (uint8_t)~0x08;
    if (!(b & A26_DIFIC_P0))     s &= (uint8_t)~0x40;
    if (!(b & A26_DIFIC_P1))     s &= (uint8_t)~0x80;
    c->riot.in_b = s;

    // O gatilho não passa pelo RIOT: é INPT4, na TIA, e também é ativo em
    // nível baixo.
    c->tia.inpt[4] = (b & A26_GATILHO) ? 0x00 : 0x80;
    c->tia.inpt[5] = 0x80;                       // jogador 2, sempre solto
}

int a26_run_frame(a26_t *c)
{
    c->audio_len = 0;
    uint32_t alvo = c->tia.frame + 1;

    // O limite existe para o caso de uma ROM quebrada que nunca gera VSYNC:
    // sem ele o emulador travaria o aparelho inteiro em vez de mostrar uma
    // tela feia. Um quadro NTSC tem ~20 mil ciclos; 200 mil é folga de sobra.
    for (long passo = 0; passo < 200000L && c->tia.frame < alvo; ++passo) {
        m6502_step(&c->cpu);
        if (c->cpu.jammed)
            break;
    }
    c->linhas_no_quadro = c->tia.lines_in_frame;
    return c->audio_len;
}

const char *a26_esquema(const a26_t *c)
{
    return cart_nome(c->cart.tipo);
}
