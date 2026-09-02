// tia_objects.c — parte 2a da TIA: jogadores, mísseis, bola e colisões.
// Licença: GPLv2 (mesma do retro-go).

#include "tia_objects.h"

// ------------------------------------------------------------- colisões

static const uint16_t MASCARA[OBJ_COUNT] = {
    CX_P0, CX_P1, CX_M0, CX_M1, CX_BL, CX_PF
};

uint16_t tia_collision_bits(uint8_t presentes)
{
    // O objeto que está desenhando não veta bit nenhum (contribui 0xFFFF); o
    // que não está veta todos os pares de que participa (contribui ~máscara).
    // O E de todas as contribuições sobra exatamente com os bits cujos dois
    // objetos estão presentes — inclusive quando há três ou mais na tela.
    uint16_t acc = 0xFFFF;
    for (int i = 0; i < OBJ_COUNT; ++i)
        if (!(presentes & (1u << i)))
            acc &= (uint16_t)~MASCARA[i];
    return acc & 0x7FFF;
}

// Mapa dos 15 bits para os registradores CXxx lidos pela CPU. Cada entrada é
// {bit para 0x80, bit para 0x40}; -1 quando o registrador não usa aquele bit.
static const int8_t CXREG[8][2] = {
    /* 0 CXM0P  */ {  9, 13 },   // M0-P1 , M0-P0
    /* 1 CXM1P  */ { 12,  8 },   // M1-P0 , M1-P1
    /* 2 CXP0FB */ { 10, 11 },   // P0-PF , P0-BL
    /* 3 CXP1FB */ {  6,  7 },   // P1-PF , P1-BL
    /* 4 CXM0FB */ {  3,  4 },   // M0-PF , M0-BL
    /* 5 CXM1FB */ {  1,  2 },   // M1-PF , M1-BL
    /* 6 CXBLPF */ {  0, -1 },   // BL-PF , (não usado)
    /* 7 CXPPMM */ { 14,  5 },   // P0-P1 , M0-M1
};

uint8_t tia_collision_reg(uint16_t bits, int reg)
{
    if (reg < 0 || reg > 7)
        return 0;
    uint8_t v = 0;
    if (CXREG[reg][0] >= 0 && (bits >> CXREG[reg][0]) & 1) v |= 0x80;
    if (CXREG[reg][1] >= 0 && (bits >> CXREG[reg][1]) & 1) v |= 0x40;
    return v;
}

// --------------------------------------------------------------- jogador

int tia_player_scale(uint8_t nusiz)
{
    switch (nusiz & 0x07) {
    case 5:  return 2;
    case 7:  return 4;
    default: return 1;
    }
}

int tia_player_offset(uint8_t nusiz)
{
    return tia_player_scale(nusiz) > 1 ? 1 : 0;
}

bool tia_player_pixel(uint8_t grp, bool refletido, uint8_t nusiz, int i)
{
    int escala = tia_player_scale(nusiz);
    if (i < 0 || i >= 8 * escala)
        return false;

    int bit = i / escala;                 // 0..7, da esquerda para a direita
    // Sem reflexão o bit 7 do GRP sai primeiro; com REFP, o bit 0.
    int desloc = refletido ? bit : (7 - bit);
    return (grp >> desloc) & 1;
}

int tia_missile_width(uint8_t nusiz)
{
    static const int L[4] = { 1, 2, 4, 8 };
    return L[(nusiz >> 4) & 0x03];
}

int tia_ball_width(uint8_t ctrlpf)
{
    static const int L[4] = { 1, 2, 4, 8 };
    return L[(ctrlpf >> 4) & 0x03];
}

// ------------------------------------------------------ cópias do NUSIZ

int tia_copy_starts_at(uint8_t nusiz, int c)
{
    if (c < 0 || c >= 160)
        return 0;

    if (c == 156)
        return 1;                          // a primeira cópia, em todos os modos

    switch (nusiz & 0x07) {
    case 1:  if (c == 12) return 2; break;                       // +16
    case 2:  if (c == 28) return 2; break;                       // +32
    case 3:  if (c == 12) return 2; if (c == 28) return 3; break; // +16, +32
    case 4:  if (c == 60) return 2; break;                       // +64
    case 6:  if (c == 28) return 2; if (c == 60) return 3; break; // +32, +64
    default: break;                        // 0, 5 e 7 têm uma cópia só
    }
    return 0;
}

// -------------------------------------------- posição e movimento (parte 2b)

int tia_respx_pos(int cc)
{
    // Durante o HBLANK o contador ainda não começou a varrer a parte visível,
    // então o objeto cai na posição mínima — medido em hmove.bin e nusiz.bin:
    // pixel 3, nas 24 faixas das duas ROMs.
    if (cc < 68)
        return TIA_RESP_MIN;

    int x = cc - 68 + TIA_RESP_ATRASO;
    return x % 160;
}

int tia_hmove_delta(uint8_t hm_reg)
{
    int n = (hm_reg >> 4) & 0x0F;
    return (n < 8) ? n : n - 16;           // 4 bits com sinal
}

int tia_hmove_apply(int pos, uint8_t hm_reg)
{
    // O objeto anda para a ESQUERDA pelo valor com sinal.
    int p = (pos - tia_hmove_delta(hm_reg)) % 160;
    return p < 0 ? p + 160 : p;
}

int tia_copy_offset(uint8_t nusiz, int pos, int x, int largura)
{
    static const int8_t EXTRA[8][2] = {
        /* 0 */ { -1, -1 },
        /* 1 */ { TIA_COPY_CLOSE, -1 },
        /* 2 */ { TIA_COPY_MED,   -1 },
        /* 3 */ { TIA_COPY_CLOSE, TIA_COPY_MED },
        /* 4 */ { TIA_COPY_WIDE,  -1 },
        /* 5 */ { -1, -1 },
        /* 6 */ { TIA_COPY_MED,   TIA_COPY_WIDE },
        /* 7 */ { -1, -1 },
    };

    if (largura <= 0)
        return -1;

    const int8_t *extra = EXTRA[nusiz & 0x07];

    for (int k = 0; k < 3; ++k) {
        int off;
        if (k == 0)
            off = 0;
        else if (extra[k - 1] < 0)
            continue;
        else
            off = extra[k - 1];

        int d = (x - pos - off) % 160;
        if (d < 0)
            d += 160;
        if (d < largura)
            return d;
    }
    return -1;
}
