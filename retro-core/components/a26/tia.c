// tia.c — TIA do Atari 2600, parte 1.
// Licença: GPLv2 (mesma do retro-go).
//
// Ordem dos bits do playfield (conferida contra o Stella):
//   PF0 — só o nibble alto, e da direita para a esquerda:
//         bit 4 -> pixel 0, bit 5 -> 1, bit 6 -> 2, bit 7 -> 3
//   PF1 — byte inteiro, invertido: bit 7 -> pixel 4 ... bit 0 -> pixel 11
//   PF2 — byte inteiro, direto:    bit 0 -> pixel 12 ... bit 7 -> pixel 19
// Cada um dos 20 bits vale 4 pixels na tela; os 80 da direita repetem os da
// esquerda, espelhados se CTRLPF.REF estiver ligado.

#include <string.h>
#include "tia.h"
#include "tia_objects.h"

// Endereços de escrita da TIA (só os 6 bits baixos são decodificados)
enum {
    VSYNC = 0x00, VBLANK = 0x01, WSYNC = 0x02, RSYNC = 0x03,
    NUSIZ0 = 0x04, NUSIZ1 = 0x05,
    COLUP0 = 0x06, COLUP1 = 0x07, COLUPF = 0x08, COLUBK = 0x09,
    CTRLPF = 0x0A, REFP0 = 0x0B, REFP1 = 0x0C,
    PF0 = 0x0D, PF1 = 0x0E, PF2 = 0x0F,
    RESP0 = 0x10, RESP1 = 0x11, RESM0 = 0x12, RESM1 = 0x13, RESBL = 0x14,
    GRP0 = 0x1B, GRP1 = 0x1C, ENAM0 = 0x1D, ENAM1 = 0x1E, ENABL = 0x1F,
    HMP0 = 0x20, HMP1 = 0x21, HMM0 = 0x22, HMM1 = 0x23, HMBL = 0x24,
    VDELP0 = 0x25, VDELP1 = 0x26, VDELBL = 0x27,
    RESMP0 = 0x28, RESMP1 = 0x29,
    HMOVE = 0x2A, HMCLR = 0x2B, CXCLR = 0x2C,
};

// Recalcula o ponteiro da linha corrente. NULL quer dizer "esta linha não
// aparece": render_pixel testa um ponteiro em vez de duas faixas, o que
// importa num laço que roda 160 vezes por linha.
static void atualiza_linha(tia_t *t)
{
    int y = (int)t->line - t->fb_linha0;
    t->fb_linha = (t->fb && y >= 0 && y < t->fb_linhas)
                ? t->fb + (size_t)y * t->fb_stride : NULL;
}

void tia_set_paddle(tia_t *t, int i, uint8_t pos)
{
    if (i >= 0 && i < 4)
        t->paddle[i] = pos;
}

void tia_set_pente_hmove(tia_t *t, bool mostrar)
{
    t->hmove_pente = mostrar;
}

void tia_set_paddles_ligadas(tia_t *t, bool ligadas)
{
    t->pa_ligada = ligadas;
}

void tia_set_framebuffer(tia_t *t, uint8_t *fb, int stride, int linha0, int linhas)
{
    t->fb = fb;
    t->fb_stride = stride;
    t->fb_linha0 = linha0;
    t->fb_linhas = linhas;
    atualiza_linha(t);
}

void tia_reset(tia_t *t)
{
    // O framebuffer é de quem chamou, não da TIA: sobrevive ao reset.
    uint8_t *fb = t->fb;
    int stride = t->fb_stride, l0 = t->fb_linha0, nl = t->fb_linhas;

    memset(t, 0, sizeof(*t));
    t->rdy = true;
    t->hmove_pente = true;
    t->cache_sujo = true;                // o memset zerou o cache junto
    t->inpt[4] = t->inpt[5] = 0x80;      // gatilhos soltos
    tia_audio_reset(&t->audio);
    tia_set_framebuffer(t, fb, stride, l0, nl);
}

static void rebuild_playfield(tia_t *t)
{
    uint32_t p = 0;
    p |= (uint32_t)(t->pf0 >> 4);                 // PF0: bits 4-7 -> 0-3
    for (int i = 0; i < 8; ++i)                   // PF1: bit 7-i -> 4+i
        if (t->pf1 & (0x80 >> i))
            p |= (uint32_t)1 << (4 + i);
    p |= (uint32_t)t->pf2 << 12;                  // PF2: bit i -> 12+i
    t->pf_pattern = p & 0xFFFFF;
}

bool tia_playfield_pixel(const tia_t *t, int x)
{
    if (x < 0 || x >= TIA_VISIBLE_PIXELS)
        return false;

    int bit;
    if (x < 80) {
        bit = x / 4;                              // metade esquerda
    } else if (t->ctrlpf & TIA_CTRLPF_REF) {
        bit = 19 - ((x - 80) / 4);                // direita espelhada
    } else {
        bit = (x - 80) / 4;                       // direita repetida
    }
    return (t->pf_pattern >> bit) & 1;
}

// Cor do playfield num pixel: em modo SCORE a metade esquerda usa a cor do
// jogador 0 e a direita a do jogador 1, ignorando COLUPF.
static uint8_t playfield_color(const tia_t *t, int x)
{
    if (t->ctrlpf & TIA_CTRLPF_SCORE)
        return (x < 80) ? t->colup0 : t->colup1;
    return t->colupf;
}

// Gráfico do jogador que está de fato na tela. Com VDELPx ligado, o que
// aparece é o valor anterior — é assim que os jogos escrevem os dois jogadores
// na mesma linha sem que um pisque.
static uint8_t player_gfx(const tia_t *t, int i)
{
    return t->vdelp[i] ? t->grp_buf[i] : t->grp[i];
}

// Posição efetiva do míssil: com RESMPx ligado ele fica preso ao meio do
// jogador, e o meio anda junto com a ampliação.
static int missile_pos(const tia_t *t, int i)
{
    if (!t->resmp[i])
        return t->pos[OBJ_M0 + i];

    static const int MEIO[3] = { 3, 7, 15 };      // 1x, 2x, 4x
    int e = tia_player_scale(t->nusiz[i]);
    int meio = MEIO[e == 4 ? 2 : (e == 2 ? 1 : 0)];
    return (t->pos[OBJ_P0 + i] + tia_player_offset(t->nusiz[i]) + meio) % 160;
}

// Enquanto um RESPx está a caminho, o contador do objeto foi jogado para
// outro ponto do seu ciclo e não dispara mais nenhum início de cópia até
// completar a volta. O que já estava sendo desenhado quando o strobe chegou
// termina normalmente — o registrador de deslocamento do gráfico continua
// andando.
//
// A condição original era `(228 - pos_falta) < d`, com `d` a distância do
// pixel até o começo da sua cópia. Os dois lados crescem de um a cada color
// clock, então a diferença entre eles não depende do pixel: dá na mesma
// comparar o **começo da cópia** com o **pixel do strobe**. A forma abaixo é
// aritmeticamente idêntica à antiga — e, ao contrário dela, não muda enquanto
// a linha avança, que é o que permite desenhar um trecho inteiro de uma vez.
static bool copia_vale(const tia_t *t, int i, int x, int ini)
{
    if (!(t->pos_pend & (1u << i)))
        return true;
    if (x < ini)
        return true;                     // começou na linha anterior: vale
    return ini < t->pos_strobe_x[i];     // começou antes do strobe: vale
}

// O gráfico do jogador esticado para a escala, já com a reflexão resolvida:
// bit `d` aceso = o pixel a `d` color clocks do início da cópia está aceso.
// São no máximo 32 pixels (escala 4), então cabe num uint32_t.
//
// É a mesma resposta de `tia_player_pixel`, calculada de uma vez só. A divisão
// por pixel que existia ali era o item mais caro do laço.
static uint32_t expande_jogador(uint8_t grp, bool refletido, uint8_t nusiz)
{
    uint32_t p = 0;
    int largura = 8 * tia_player_scale(nusiz);
    for (int d = 0; d < largura; ++d)
        if (tia_player_pixel(grp, refletido, nusiz, d))    // a função de referência
            p |= (uint32_t)1 << d;
    return p;
}

// Marca `len` pixels a partir de `ini` no bitmap de 160 posições, dando a volta.
// Por palavra, não por bit: um jogador ampliado em três cópias são 96 pixels, e
// isto é refeito toda vez que o cache do compositor é invalidado.
static void marca_corrida(uint32_t *m, int ini, int len)
{
    int x = ini;
    while (len > 0) {
        if (x >= TIA_VISIBLE_PIXELS)
            x -= TIA_VISIBLE_PIXELS;

        int b = x & 31;
        int n = 32 - b;                  // até o fim desta palavra
        if (n > len)
            n = len;

        m[x >> 5] |= (n >= 32) ? 0xFFFFFFFFu
                               : ((((uint32_t)1 << n) - 1) << b);
        x += n;
        len -= n;
    }
}

// Refaz o cache do compositor. Chamado no primeiro pixel depois de qualquer
// escrita — nunca no meio de um pixel.
static void atualiza_cache(tia_t *t)
{
    tia_geo_t g;
    memset(&g, 0, sizeof(g));

    for (int i = 0; i < 2; ++i) {
        uint8_t nusiz = t->nusiz[i];
        uint8_t gfx = player_gfx(t, i);
        int esc = tia_player_scale(nusiz);

        // Gráfico zerado é o caso mais comum de todos: nenhuma cópia pode
        // acender pixel nenhum, então nem vale montar a lista.
        if (gfx) {
            g.larg_p[i] = (uint8_t)(8 * esc);
            if (gfx != t->cache.memo_grp[i] || nusiz != t->cache.memo_nusiz[i] ||
                (uint8_t)t->refp[i] != t->cache.memo_refp[i]) {
                t->cache.padrao[i] = expande_jogador(gfx, t->refp[i], nusiz);
                t->cache.memo_grp[i] = gfx;
                t->cache.memo_nusiz[i] = nusiz;
                t->cache.memo_refp[i] = (uint8_t)t->refp[i];
            }
            int pos = (t->pos[OBJ_P0 + i] + tia_player_offset(nusiz)) % 160;
            g.ncop_p[i] = (uint8_t)tia_copy_inicios(nusiz, pos, g.ini_p[i]);
        }

        // Com RESMPx ligado o míssil some (e fica preso ao jogador); com ENAMx
        // desligado, idem. Nos dois casos não há o que desenhar.
        bool ligado = t->resmp[i] ? false : t->enam[i];
        if (ligado) {
            g.larg_m[i] = (uint8_t)tia_missile_width(nusiz);
            g.ncop_m[i] = (uint8_t)tia_copy_inicios(nusiz, missile_pos(t, i), g.ini_m[i]);
        }
    }

    bool bola = t->vdelbl ? t->enabl_buf : t->enabl;
    g.larg_bl = bola ? (uint8_t)tia_ball_width(t->ctrlpf) : 0;
    g.ini_bl = t->pos[OBJ_BL];

    // O mapa de ocupação só muda quando a geometria muda — e a maioria das
    // escritas que invalidam o cache não mexe nela (uma troca de COLUBK, por
    // exemplo). Comparar 22 bytes sai muito mais barato do que remontar o mapa.
    if (memcmp(&g, &t->cache.geo, sizeof(g)) != 0) {
        t->cache.geo = g;

        memset(t->cache.ocupado, 0, sizeof(t->cache.ocupado));
        for (int i = 0; i < 2; ++i) {
            for (int k = 0; k < g.ncop_p[i] && g.larg_p[i]; ++k)
                marca_corrida(t->cache.ocupado, g.ini_p[i][k], g.larg_p[i]);
            for (int k = 0; k < g.ncop_m[i] && g.larg_m[i]; ++k)
                marca_corrida(t->cache.ocupado, g.ini_m[i][k], g.larg_m[i]);
        }
        if (g.larg_bl)
            marca_corrida(t->cache.ocupado, g.ini_bl, g.larg_bl);
    }

    t->cache_sujo = false;
}

static uint8_t objetos_em(tia_t *t, int x)
{
    uint8_t m = 0;

    for (int i = 0; i < 2; ++i) {
        if (t->cache.geo.larg_p[i]) {
            for (int k = 0; k < t->cache.geo.ncop_p[i]; ++k) {
                int d = x - t->cache.geo.ini_p[i][k];
                if (d < 0)
                    d += 160;
                if (d < t->cache.geo.larg_p[i]) {
                    // As cópias do NUSIZ nunca se sobrepõem, então a primeira
                    // que contém `x` é a única: acertando ou não o pixel, não
                    // há mais o que procurar.
                    if (((t->cache.padrao[i] >> d) & 1) &&
                        copia_vale(t, OBJ_P0 + i, x, t->cache.geo.ini_p[i][k]))
                        m |= (uint8_t)(1u << (OBJ_P0 + i));
                    break;
                }
            }
        }

        // O míssil segue as cópias do NUSIZ do seu jogador, mas com a largura
        // dada pelos bits 4-5 do mesmo registrador.
        if (t->cache.geo.larg_m[i]) {
            for (int k = 0; k < t->cache.geo.ncop_m[i]; ++k) {
                int d = x - t->cache.geo.ini_m[i][k];
                if (d < 0)
                    d += 160;
                if (d < t->cache.geo.larg_m[i]) {
                    if (copia_vale(t, OBJ_M0 + i, x, t->cache.geo.ini_m[i][k]))
                        m |= (uint8_t)(1u << (OBJ_M0 + i));
                    break;
                }
            }
        }
    }

    // A bola não tem cópias: nusiz 0.
    if (t->cache.geo.larg_bl) {
        int d = x - t->cache.geo.ini_bl;
        if (d < 0)
            d += 160;
        if (d < t->cache.geo.larg_bl && copia_vale(t, OBJ_BL, x, t->cache.geo.ini_bl))
            m |= (uint8_t)(1u << OBJ_BL);
    }

    if (tia_playfield_pixel(t, x))
        m |= (uint8_t)(1u << OBJ_PF);

    return m;
}

uint8_t tia_objects_at(tia_t *t, int x)
{
    if (t->cache_sujo)
        atualiza_cache(t);
    return objetos_em(t, x);
}

// Prioridade. Normalmente os jogadores ficam na frente do playfield; com
// CTRLPF.PFP o playfield e a bola passam para a frente de todos.
static uint8_t compose(const tia_t *t, int x, uint8_t m)
{
    bool pf_frente = (t->ctrlpf & TIA_CTRLPF_PFP) != 0;

    if (pf_frente && (m & ((1u << OBJ_PF) | (1u << OBJ_BL))))
        return (m & (1u << OBJ_PF)) ? playfield_color(t, x) : t->colupf;

    if (m & ((1u << OBJ_P0) | (1u << OBJ_M0))) return t->colup0;
    if (m & ((1u << OBJ_P1) | (1u << OBJ_M1))) return t->colup1;
    if (m & (1u << OBJ_PF))                    return playfield_color(t, x);
    if (m & (1u << OBJ_BL))                    return t->colupf;
    return t->colubk;
}

// As colisões NÃO dependem do framebuffer.
//
// Isto já foi um erro sério aqui. A versão anterior começava com
// `if (!t->fb_linha) return;`, o que fazia sentido para desenhar e nenhum para
// emular: quem chama pode não dar buffer nenhum (é assim que o retro-go pula
// quadro) e pode dar uma janela de 210 linhas em vez das 262. Nos dois casos
// o pixel deixava de ser composto — e junto com ele deixavam de latchar os
// registradores de colisão. Com o aparelho pulando quase um quadro em cada
// dois, metade das colisões do jogo sumia: no River Raid o tiro atravessava o
// alvo "às vezes".
//
// No chip, o que apaga a saída de vídeo (VBLANK, o HBLANK estendido do HMOVE)
// e o que a manda para a tela são estágios diferentes de quem alimenta os
// latches de colisão. Este código separa as duas coisas: decide a colisão
// sempre, e só então, se houver linha, escreve o pixel.
// Preenche uma faixa que não tem objeto móvel nenhum: só playfield e fundo.
//
// O playfield muda de valor a cada 4 pixels — cada um dos 20 bits vale quatro
// colunas. Perguntar a cor pixel a pixel era chamar `tia_playfield_pixel` (com
// a divisão por 4 dentro) e `playfield_color` 160 vezes por linha para
// responder a mesma coisa quatro vezes seguidas. Aqui a cor é decidida uma vez
// por grupo de quatro e escrita de uma vez.
static void preenche_fundo(const tia_t *t, uint8_t *linha, int x0, int x1)
{
    const uint8_t bk = t->colubk;
    const bool score = (t->ctrlpf & TIA_CTRLPF_SCORE) != 0;
    const bool ref = (t->ctrlpf & TIA_CTRLPF_REF) != 0;
    const uint32_t pat = t->pf_pattern;

    int x = x0;
    while (x < x1) {
        int bit;
        int fim;
        if (x < 80) {
            bit = x >> 2;
            fim = (x & ~3) + 4;
            if (fim > 80)
                fim = 80;
        } else {
            int d = x - 80;
            bit = ref ? 19 - (d >> 2) : (d >> 2);
            fim = 80 + ((d & ~3) + 4);
        }
        if (fim > x1)
            fim = x1;

        uint8_t cor = ((pat >> bit) & 1)
                    ? (score ? (x < 80 ? t->colup0 : t->colup1) : t->colupf)
                    : bk;
        memset(linha + x, cor, (size_t)(fim - x));
        x = fim;
    }
}

// Desenha os pixels dos color clocks [c0, c0+n) da linha corrente.
//
// Um trecho só existe enquanto nada muda: quem chama corta em toda escrita, em
// todo RESPx que completa a volta e no fim da linha. Dentro dele o estado da
// TIA é constante, então o cache do compositor é montado uma vez e as
// verificações de HBLANK, de fim de linha e de borda de áudio saem do laço de
// pixel — eram 30% dos color clocks fazendo só contabilidade, já que 68 dos 228
// de cada linha não produzem pixel nenhum.
static void desenha_faixa(tia_t *t, int c0, int n)
{
    int x0 = c0 - TIA_HBLANK_CLOCKS;
    int x1 = x0 + n;
    if (x0 < 0)
        x0 = 0;
    if (x1 > TIA_VISIBLE_PIXELS)
        x1 = TIA_VISIBLE_PIXELS;
    if (x0 >= x1)
        return;                              // trecho inteiro dentro do HBLANK

    uint8_t *linha = t->fb_linha;

    // Faixa apagada. Nem pixel nem colisão: o que apaga a saída de vídeo é o
    // mesmo sinal que cala os objetos.
    if (t->vblank) {
        if (linha)
            memset(linha + x0, 0, (size_t)(x1 - x0));
        return;
    }

    // Um HMOVE no começo da linha estica o HBLANK por 8 color clocks: os 8
    // primeiros pixels saem apagados, e não com a cor de fundo. É o "pente"
    // que aparece na borda esquerda de tantos jogos. Medido nas 16 faixas de
    // hmove.bin — 8 pixels, exatos.
    if (t->hmove_pente && t->hmove_line && x0 < TIA_HMOVE_BLANK) {
        int corte = x1 < TIA_HMOVE_BLANK ? x1 : TIA_HMOVE_BLANK;
        if (linha)
            memset(linha + x0, 0, (size_t)(corte - x0));
        x0 = corte;
        if (x0 >= x1)
            return;
    }

    if (t->cache_sujo)
        atualiza_cache(t);

    // As colisões NÃO dependem do framebuffer.
    //
    // Isto já foi um erro sério aqui. A versão antiga saía cedo quando não
    // havia linha para escrever, e os latches de colisão paravam junto com o
    // desenho. Quem chama pode não dar framebuffer (é assim que o retro-go pula
    // quadro) e pode dar uma janela mais curta que o quadro; em nenhum dos dois
    // casos o jogo pode enxergar colisão diferente.
    // Sem objeto móvel não há colisão possível (colisão precisa de dois) e a
    // cor sai direto do playfield. O mapa de ocupação deixa pular 32 pixels de
    // uma vez quando a palavra inteira está vazia.
    int x = x0;
    while (x < x1) {
        int fim = ((x >> 5) + 1) << 5;
        if (fim > x1)
            fim = x1;

        uint32_t bits = t->cache.ocupado[x >> 5];
        uint32_t faixa = (fim - x) >= 32 ? 0xFFFFFFFFu
                       : (((uint32_t)1 << (fim - x)) - 1) << (x & 31);

        if ((bits & faixa) == 0) {
            if (linha)
                preenche_fundo(t, linha, x, fim);
            x = fim;
            continue;
        }

        if (linha) {
            for (; x < fim; ++x) {
                uint8_t m = objetos_em(t, x);
                t->collisions |= tia_collision_bits(m);
                linha[x] = compose(t, x, m);
            }
        } else {
            for (; x < fim; ++x)
                t->collisions |= tia_collision_bits(objetos_em(t, x));
        }
    }
}

static void aplica_escrita(tia_t *t, uint16_t addr, uint8_t val);

void tia_tick(tia_t *t, int color_clocks)
{
    while (color_clocks > 0) {
        // O maior trecho que dá para desenhar sem que nada mude no chip.
        int passo = color_clocks;

        int resta = TIA_CLOCKS_PER_LINE - t->clock;
        if (passo > resta)
            passo = resta;

        // Uma escrita pendente vale a partir do color clock SEGUINTE, então o
        // trecho tem de terminar neste.
        if (t->w_pend)
            passo = 1;

        // Um RESPx completando a volta muda a posição do objeto.
        if (t->pos_pend) {
            for (int i = 0; i < 5; ++i)
                if ((t->pos_pend & (1u << i)) && t->pos_falta[i] < (uint16_t)passo)
                    passo = t->pos_falta[i];
        }

        int c0 = t->clock;
        desenha_faixa(t, c0, passo);

        // O relógio do som anda em quatro dos 228 color clocks da linha. Não
        // interfere no desenho, então basta aplicar as bordas que o trecho
        // cobriu, em ordem.
        static const int BORDAS[4] = { 9, 37, 81, 149 };
        for (int b = 0; b < 4; ++b)
            if (BORDAS[b] >= c0 && BORDAS[b] < c0 + passo)
                tia_audio_clock(&t->audio, BORDAS[b]);

        color_clocks -= passo;

        // A carga das pás anda com a varredura. O teto evita que a contagem dê
        // a volta num jogo que nunca aterra os capacitores — o valor já passou
        // do fim do curso muito antes disso.
        if (!t->pa_aterrado && t->pa_carga < 0x0F000000u)
            t->pa_carga += (uint32_t)passo;

        // A escrita é aplicada com o relógio ainda no último color clock do
        // trecho — o RESPx lê `t->clock` para saber onde o objeto cai, e um
        // pixel de diferença aqui move o objeto na tela.
        if (t->w_pend) {
            t->w_pend = false;
            aplica_escrita(t, t->w_addr, t->w_val);
        }

        if (t->pos_pend) {
            for (int i = 0; i < 5; ++i) {
                if (!(t->pos_pend & (1u << i)))
                    continue;
                t->pos_falta[i] -= (uint16_t)passo;
                if (t->pos_falta[i] == 0) {
                    t->pos[i] = t->pos_nova[i];
                    t->pos_pend &= (uint8_t)~(1u << i);
                    t->cache_sujo = true;   // a posição mudou de verdade agora
                }
            }
        }

        t->clock += (uint16_t)passo;

        if (t->clock >= TIA_CLOCKS_PER_LINE) {
            t->clock = 0;
            t->rdy = true;                        // WSYNC solta a CPU aqui
            t->hmove_line = false;                // o pente vale só na linha do HMOVE
            for (int i = 0; i < 5; ++i)           // o strobe agora é de linha passada
                t->pos_strobe_x[i] = -1;
            if (t->line < TIA_MAX_LINES - 1)
                t->line++;
            atualiza_linha(t);
        }
    }
}

void tia_write(tia_t *t, uint16_t addr, uint8_t val)
{
    // WSYNC e RSYNC não são sinais de vídeo: são estrobos de sincronismo, e
    // valem na hora. O resto espera o próximo color clock.
    if ((addr & 0x3F) == WSYNC) {
        t->rdy = false;
        return;
    }
    if ((addr & 0x3F) == RSYNC) {
        t->clock = 0;
        return;
    }
    // Se já houver uma pendente (não deve acontecer: escritas ficam a 3 color
    // clocks uma da outra), aplica a antiga antes de enfileirar a nova.
    if (t->w_pend)
        aplica_escrita(t, t->w_addr, t->w_val);
    t->w_pend = true;
    t->w_addr = addr;
    t->w_val = val;
}

static void aplica_escrita(tia_t *t, uint16_t addr, uint8_t val)
{
    switch (addr & 0x3F) {
    case VSYNC:
        // A borda de descida de VSYNC fecha o quadro.
        if (t->vsync && !(val & 0x02)) {
            t->lines_in_frame = t->line;
            t->frame++;
            t->line = 0;
            atualiza_linha(t);
        }
        t->vsync = (val & 0x02) != 0;
        break;

    case VBLANK:
        t->vblank = (val & 0x02) != 0;
        // Bit 7: aterra os capacitores das pás. Enquanto ligado a contagem
        // fica em zero; ao ser solto, ela recomeça — e é essa contagem que o
        // jogo transforma em posição.
        t->pa_aterrado = (val & 0x80) != 0;
        if (t->pa_aterrado)
            t->pa_carga = 0;
        break;

    case COLUP0: t->colup0 = val & 0xFE; break;   // o bit 0 não existe na TIA
    case COLUP1: t->colup1 = val & 0xFE; break;
    case COLUPF: t->colupf = val & 0xFE; break;
    case COLUBK: t->colubk = val & 0xFE; break;

    case CTRLPF: t->ctrlpf = val; break;

    case PF0: t->pf0 = val; rebuild_playfield(t); break;
    case PF1: t->pf1 = val; rebuild_playfield(t); break;
    case PF2: t->pf2 = val; rebuild_playfield(t); break;

    case NUSIZ0: t->nusiz[0] = val; break;
    case NUSIZ1: t->nusiz[1] = val; break;
    case REFP0:  t->refp[0] = (val & 0x08) != 0; break;
    case REFP1:  t->refp[1] = (val & 0x08) != 0; break;

    // Escrever em GRPx transfere o gráfico do OUTRO jogador para o buffer
    // atrasado. É esse encadeamento que faz o VDELPx funcionar.
    case GRP0: t->grp[0] = val; t->grp_buf[1] = t->grp[1]; break;
    case GRP1: t->grp[1] = val; t->grp_buf[0] = t->grp[0];
               t->enabl_buf = t->enabl; break;

    case ENAM0: t->enam[0] = (val & 0x02) != 0; break;
    case ENAM1: t->enam[1] = (val & 0x02) != 0; break;
    case ENABL: t->enabl   = (val & 0x02) != 0; break;

    case VDELP0: t->vdelp[0] = (val & 0x01) != 0; break;
    case VDELP1: t->vdelp[1] = (val & 0x01) != 0; break;
    case VDELBL: t->vdelbl   = (val & 0x01) != 0; break;

    case RESMP0: t->resmp[0] = (val & 0x02) != 0; break;
    case RESMP1: t->resmp[1] = (val & 0x02) != 0; break;

    case RESP0: case RESP1: case RESM0: case RESM1: case RESBL: {
        static const uint8_t IDX[5] = { OBJ_P0, OBJ_P1, OBJ_M0, OBJ_M1, OBJ_BL };
        int i = IDX[(addr & 0x3F) - RESP0];
        uint8_t nova = (uint8_t)tia_respx_pos(t->clock, i == OBJ_P0 || i == OBJ_P1);

        // Jogar o contador para onde ele já está não é evento nenhum: o
        // hardware não vê diferença nenhuma e a linha sai igual à anterior.
        // É exatamente o que separa nusiz.bin de hmove.bin — mesma sequência
        // de instruções, mesmo color clock, resultados diferentes. Na nusiz
        // todas as faixas usam a mesma posição, o RESP0 é inócuo e o jogador
        // aparece na própria linha do strobe; na hmove cada faixa muda de
        // posição, o contador sai do lugar e a linha do RESP0 sai limpa.
        uint8_t alvo = (t->pos_pend & (1u << i)) ? t->pos_nova[i] : t->pos[i];
        if (nova == alvo)
            break;

        t->pos_nova[i] = nova;
        t->pos_falta[i] = TIA_CLOCKS_PER_LINE;   // a volta completa do contador
        t->pos_strobe_x[i] = (int16_t)(t->clock - TIA_HBLANK_CLOCKS);
        t->pos_pend |= (uint8_t)(1u << i);
        break;
    }

    case HMP0: t->hm[OBJ_P0] = val; break;
    case HMP1: t->hm[OBJ_P1] = val; break;
    case HMM0: t->hm[OBJ_M0] = val; break;
    case HMM1: t->hm[OBJ_M1] = val; break;
    case HMBL: t->hm[OBJ_BL] = val; break;

    case HMOVE:
        // O HMOVE mexe no contador na hora — e também na posição que ainda
        // está a caminho, se houver um RESPx pendente.
        for (int i = 0; i < 5; ++i) {
            t->pos[i] = (uint8_t)tia_hmove_apply(t->pos[i], t->hm[i]);
            t->pos_nova[i] = (uint8_t)tia_hmove_apply(t->pos_nova[i], t->hm[i]);
        }
        t->hmove_line = true;
        break;

    case HMCLR:
        for (int i = 0; i < 5; ++i)
            t->hm[i] = 0;
        break;

    case CXCLR: t->collisions = 0; break;

    // Os seis registradores de som. AUDCx é a voz, AUDFx o divisor de
    // frequência e AUDVx o volume.
    case 0x15: tia_audio_set_audc(&t->audio, 0, val); break;
    case 0x16: tia_audio_set_audc(&t->audio, 1, val); break;
    case 0x17: tia_audio_set_audf(&t->audio, 0, val); break;
    case 0x18: tia_audio_set_audf(&t->audio, 1, val); break;
    case 0x19: tia_audio_set_audv(&t->audio, 0, val); break;
    case 0x1A: tia_audio_set_audv(&t->audio, 1, val); break;

    default:
        break;
    }

    // Invalidação grossa: sai mais barato refazer o cache inteiro algumas
    // vezes por linha do que manter uma lista de quais registradores mexem em
    // quê — lista que erraria calada no dia em que alguém acrescentasse um
    // caso ao switch acima.
    t->cache_sujo = true;
}

uint8_t tia_read(tia_t *t, uint16_t addr)
{
    int reg = addr & 0x0F;

    // CXxx: 0x00-0x07. Os bits 0-5 não são acionados pela TIA; o barramento
    // fica com o que já estava nele. Devolvemos 0 neles.
    if (reg <= 0x07)
        return tia_collision_reg(t->collisions, reg);

    // INPT0-INPT3: as pás. O bit 7 sobe quando o capacitor passa do limiar,
    // e o limiar é proporcional à posição.
    if (reg >= 0x08 && reg <= 0x0B) {
        if (!t->pa_ligada)
            return 0x00;                 // joystick: pino do potenciômetro solto
        uint32_t linhas = TIA_PA_BASE_LINHAS +
                          (uint32_t)t->paddle[reg - 0x08] * TIA_PA_CURSO_LINHAS / 255u;
        uint32_t limiar = linhas * TIA_CLOCKS_PER_LINE;
        return (!t->pa_aterrado && t->pa_carga >= limiar) ? 0x80 : 0x00;
    }

    // INPT4 e INPT5: os gatilhos dos joysticks.
    if (reg == 0x0C || reg == 0x0D)
        return (uint8_t)(t->inpt[reg - 0x08] & 0x80);

    // Os bits não acionados pela TIA ficam com o que já estava no barramento;
    // 0 serve por enquanto.
    return 0;
}
