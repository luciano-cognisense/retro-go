// tia.h — Television Interface Adaptor do Atari 2600 (parte 1: varredura,
// sincronismo, playfield e cores).
//
// A TIA não tem memória de vídeo: ela produz um pixel por color clock e o
// jogo reescreve os registradores enquanto o feixe anda. Por isso a unidade
// de tempo aqui é o color clock, não o quadro nem a linha.
//
//   1 linha  = 228 color clocks = 68 de HBLANK + 160 visíveis
//   1 ciclo de CPU = 3 color clocks
//
// O framebuffer guarda o **índice de cor da TIA** (o valor escrito em COLUPx),
// não RGB. A conversão para a paleta NTSC/PAL fica com quem exibe — no caso,
// o retro-go. Isso mantém o núcleo independente de formato de tela.
//
// Licença: GPLv2 (mesma do retro-go).

#ifndef TIA_H
#define TIA_H

#include <stdint.h>
#include <stdbool.h>

#include "tia_audio.h"

#define TIA_CLOCKS_PER_LINE 228
#define TIA_HBLANK_CLOCKS   68
#define TIA_VISIBLE_PIXELS  160
#define TIA_MAX_LINES       320      // cabe NTSC (262) e PAL (312)

typedef struct {
    // Varredura
    uint16_t clock;          // 0..227, posição dentro da linha
    uint16_t line;           // linha corrente dentro do quadro
    uint32_t frame;          // quadros completos desde o reset

    // Sincronismo
    bool vsync;
    bool vblank;
    bool rdy;                // linha RDY: falsa enquanto WSYNC segura a CPU

    // Playfield
    uint32_t pf_pattern;     // 20 bits, bit 0 = pixel mais à esquerda
    uint8_t  pf0, pf1, pf2;
    uint8_t  ctrlpf;

    // Cores (índices da TIA, não RGB)
    uint8_t colup0, colup1, colupf, colubk;

    // Objetos móveis. Os índices seguem tia_obj_t: 0=P0, 1=P1, 2=M0, 3=M1,
    // 4=BL. `pos` é a coluna visível onde começa a primeira cópia; `hm` é o
    // registrador HMxx cru, aplicado só quando vem um HMOVE.
    uint8_t pos[5];
    uint8_t hm[5];

    // O RESPx não muda a posição na hora. Ele zera o contador do objeto; até o
    // contador dar a volta, o objeto não começa nenhuma cópia nova — mas a
    // cópia que já estava sendo desenhada quando o strobe chegou termina.
    // A troca vale uma linha inteira depois (228 color clocks).
    //
    // Medido nas 16 faixas de hmove.bin: na linha do RESP0 o jogador aparece
    // só no pedaço que veio virando da linha anterior, e some do ponto onde
    // começaria uma cópia nova. É por isso que todo jogo posiciona o objeto
    // numa linha jogada fora.
    uint8_t  pos_nova[5];
    uint16_t pos_falta[5];   // color clocks que faltam; 0 = nada pendente
    uint8_t  pos_pend;       // bitmap de quais objetos têm troca pendente

    uint8_t nusiz[2];        // NUSIZ0/1: cópias, ampliação e largura do míssil
    uint8_t grp[2];          // gráfico corrente do jogador
    uint8_t grp_buf[2];      // gráfico atrasado (VDELPx)
    bool    refp[2];         // REFPx: espelha os 8 bits
    bool    vdelp[2];        // VDELPx
    bool    vdelbl;          // VDELBL
    bool    enam[2];         // ENAMx
    bool    enabl, enabl_buf;// ENABL corrente e atrasado
    bool    resmp[2];        // RESMPx: míssil travado no meio do jogador

    // Entradas: INPT0-INPT5. Só o bit 7 vale. As quatro primeiras são as pás
    // (descarregadas = 0); as duas últimas são os gatilhos dos joysticks, e
    // são **ativas em nível baixo**: 0x80 quer dizer solto.
    //
    // O padrão importa mais do que parece. Com estes registradores devolvendo
    // 0, todo jogo acha que o gatilho está preso desde que o console ligou —
    // e vários simplesmente não saem da tela de abertura.
    uint8_t  inpt[6];

    bool     hmove_line;     // a linha corrente teve HMOVE: 8 pixels a mais de blank
    uint16_t collisions;     // os 15 bits de colisão, zerados por CXCLR

    // Uma escrita na TIA só surte efeito no color clock SEGUINTE. Medido nas
    // linhas de transição de pfalign.bin, onde o VBLANK é ligado no meio da
    // linha: sem esse atraso o meu quadro apagava um pixel antes do Stella,
    // nas oito faixas. O WSYNC é exceção — ele não é sinal de vídeo, é a
    // linha RDY que segura a CPU, e vale na hora.
    bool     w_pend;
    uint16_t w_addr;
    uint8_t  w_val;

    // Som. Mora aqui dentro porque os registradores AUDxx são da TIA e porque
    // o relógio do som é derivado da varredura — duas vezes por linha, em
    // posições fixas do color clock.
    tia_audio_t audio;

    // Saída. O framebuffer é **externo**: quem usa a TIA diz para onde
    // escrever. No ESP32 isso aponta direto para a superfície do retro-go, o
    // que evita uma cópia por quadro e, mais importante, tira 51 KB de dentro
    // deste struct — que não caberiam com folga na RAM interna.
    //
    // `fb_linha0` e `fb_linhas` recortam a janela vertical: linha da TIA fora
    // dela simplesmente não é escrita. Cada jogo produz um número diferente de
    // linhas, então o recorte é o que dá uma imagem de altura fixa.
    uint8_t *fb;
    int      fb_stride;
    int      fb_linha0;
    int      fb_linhas;
    uint8_t *fb_linha;        // linha corrente, ou NULL fora da janela

    uint16_t lines_in_frame;  // linhas do último quadro fechado
} tia_t;

// Bits de CTRLPF
#define TIA_CTRLPF_REF   0x01   // espelha a metade direita do playfield
#define TIA_CTRLPF_SCORE 0x02   // metade esquerda em COLUP0, direita em COLUP1
#define TIA_CTRLPF_PFP   0x04   // playfield na frente dos jogadores

void tia_reset(tia_t *t);

// Onde a TIA desenha. `linha0` é a primeira linha da varredura que aparece na
// imagem e `linhas` a altura dela. Passar fb = NULL desliga o desenho (útil
// para pular quadro sem pagar o custo do compositor).
void tia_set_framebuffer(tia_t *t, uint8_t *fb, int stride, int linha0, int linhas);

// Avança a varredura. Chamado com 3 por ciclo de CPU.
void tia_tick(tia_t *t, int color_clocks);

// `addr` é o endereço cru; a TIA só decodifica os 6 bits baixos na escrita.
void tia_write(tia_t *t, uint16_t addr, uint8_t val);
uint8_t tia_read(tia_t *t, uint16_t addr);

// Padrão do playfield para um pixel visível (0..159). Função pura dos
// registradores — exposta para poder ser testada isoladamente.
bool tia_playfield_pixel(const tia_t *t, int x);

// Bitmap de tia_obj_t com os objetos que estão desenhando no pixel visível
// `x`. Exposto para teste; é o que alimenta a prioridade e as colisões.
uint8_t tia_objects_at(const tia_t *t, int x);

#endif
