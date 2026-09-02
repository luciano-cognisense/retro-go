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
// `d` é a distância do pixel até o começo da sua cópia, ou seja, há quantos
// color clocks aquela cópia começou.
static bool copia_vale(const tia_t *t, int i, int x, int d)
{
    if (!(t->pos_pend & (1u << i)))
        return true;
    if (x < d)
        return true;                     // começou na linha anterior: vale
    int desde = TIA_CLOCKS_PER_LINE - t->pos_falta[i];   // clocks desde o strobe
    return desde < d;                    // começou antes do strobe: vale
}

uint8_t tia_objects_at(const tia_t *t, int x)
{
    uint8_t m = 0;

    for (int i = 0; i < 2; ++i) {
        uint8_t nusiz = t->nusiz[i];
        int esc = tia_player_scale(nusiz);
        int pos = (t->pos[OBJ_P0 + i] + tia_player_offset(nusiz)) % 160;
        int d = tia_copy_offset(nusiz, pos, x, 8 * esc);
        if (d >= 0 && copia_vale(t, OBJ_P0 + i, x, d) &&
            tia_player_pixel(player_gfx(t, i), t->refp[i], nusiz, d))
            m |= (uint8_t)(1u << (OBJ_P0 + i));

        // O míssil segue as cópias do NUSIZ do seu jogador, mas com a largura
        // dada pelos bits 4-5 do mesmo registrador.
        bool ligado = t->resmp[i] ? false : t->enam[i];
        int dm = ligado
               ? tia_copy_offset(nusiz, missile_pos(t, i), x, tia_missile_width(nusiz))
               : -1;
        if (dm >= 0 && copia_vale(t, OBJ_M0 + i, x, dm))
            m |= (uint8_t)(1u << (OBJ_M0 + i));
    }

    // A bola não tem cópias: nusiz 0.
    bool bola = t->vdelbl ? t->enabl_buf : t->enabl;
    int db = bola ? tia_copy_offset(0, t->pos[OBJ_BL], x, tia_ball_width(t->ctrlpf)) : -1;
    if (db >= 0 && copia_vale(t, OBJ_BL, x, db))
        m |= (uint8_t)(1u << OBJ_BL);

    if (tia_playfield_pixel(t, x))
        m |= (uint8_t)(1u << OBJ_PF);

    return m;
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

static void render_pixel(tia_t *t)
{
    if (!t->fb_linha)
        return;
    int x = t->clock - TIA_HBLANK_CLOCKS;
    if (x < 0 || x >= TIA_VISIBLE_PIXELS)
        return;

    // Um HMOVE no começo da linha estica o HBLANK por 8 color clocks: os 8
    // primeiros pixels saem apagados, e não com a cor de fundo. É o "pente"
    // que aparece na borda esquerda de tantos jogos. Medido nas 16 faixas de
    // hmove.bin — 8 pixels, exatos.
    if (t->vblank || (t->hmove_line && x < TIA_HMOVE_BLANK)) {
        t->fb_linha[x] = 0;
        return;
    }

    uint8_t m = tia_objects_at(t, x);
    t->collisions |= tia_collision_bits(m);
    t->fb_linha[x] = compose(t, x, m);
}

static void aplica_escrita(tia_t *t, uint16_t addr, uint8_t val);

void tia_tick(tia_t *t, int color_clocks)
{
    while (color_clocks-- > 0) {
        render_pixel(t);
        tia_audio_clock(&t->audio, t->clock);

        // A escrita pendente vale a partir do próximo pixel, não deste.
        if (t->w_pend) {
            t->w_pend = false;
            aplica_escrita(t, t->w_addr, t->w_val);
        }

        // Contadores dos RESPx dando a volta. Quase sempre não há nenhum, e o
        // teste do bitmap sai barato.
        if (t->pos_pend) {
            for (int i = 0; i < 5; ++i) {
                if (!(t->pos_pend & (1u << i)))
                    continue;
                if (--t->pos_falta[i] == 0) {
                    t->pos[i] = t->pos_nova[i];
                    t->pos_pend &= (uint8_t)~(1u << i);
                }
            }
        }

        if (++t->clock >= TIA_CLOCKS_PER_LINE) {
            t->clock = 0;
            t->rdy = true;                        // WSYNC solta a CPU aqui
            t->hmove_line = false;                // o pente vale só na linha do HMOVE
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

    case VBLANK: t->vblank = (val & 0x02) != 0; break;

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
        uint8_t nova = (uint8_t)tia_respx_pos(t->clock);

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
}

uint8_t tia_read(tia_t *t, uint16_t addr)
{
    int reg = addr & 0x0F;

    // CXxx: 0x00-0x07. Os bits 0-5 não são acionados pela TIA; o barramento
    // fica com o que já estava nele. Devolvemos 0 neles.
    if (reg <= 0x07)
        return tia_collision_reg(t->collisions, reg);

    if (reg >= 0x08 && reg <= 0x0D)
        return (uint8_t)(t->inpt[reg - 0x08] & 0x80);

    // Os bits não acionados pela TIA ficam com o que já estava no barramento;
    // 0 serve por enquanto.
    return 0;
}
