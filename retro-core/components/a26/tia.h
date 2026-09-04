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

// Geometria dos objetos na linha: o que o compositor precisa saber para
// decidir se um pixel tem objeto. Derivada dos registradores, nunca estado do
// chip.
typedef struct {
    uint8_t  larg_p[2];    // 8 * escala, ou 0 se o gráfico está zerado
    uint8_t  larg_m[2];    // largura do míssil, ou 0 se desligado
    uint8_t  larg_bl;      // largura da bola, ou 0 se desligada
    uint8_t  ncop_p[2];    // cópias do jogador: 1 a 3
    uint8_t  ncop_m[2];    // cópias do míssil: 1 a 3
    uint8_t  ini_p[2][3];  // início de cada cópia, 0..159
    uint8_t  ini_m[2][3];
    uint8_t  ini_bl;
} tia_geo_t;

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

    // Entradas: INPT0-INPT5. Só o bit 7 vale. As duas últimas são os gatilhos
    // dos joysticks, **ativos em nível baixo**: 0x80 quer dizer solto.
    //
    // O padrão importa mais do que parece. Com estes registradores devolvendo
    // 0, todo jogo acha que o gatilho está preso desde que o console ligou —
    // e vários simplesmente não saem da tela de abertura.
    uint8_t  inpt[6];

    // As pás (INPT0-INPT3) não são botões: são potenciômetros lidos por tempo.
    //
    // Cada pá é um resistor variável carregando um capacitor. O bit 7 de INPTx
    // sobe quando o capacitor passa do limiar, e quanto tempo isso leva é a
    // posição da pá. O jogo escreve VBLANK com o bit 7 ligado para aterrar os
    // capacitores, solta, e conta linhas de varredura até o bit subir.
    //
    // Não emular isto não deixa o jogo "sem controle": deixa a contagem sem
    // fim, e o jogo conclui que a pá está no fim do curso. É por isso que a
    // raquete do Breakout ficava travada no canto.
    // `pa_ligada` diz se há pás no console. Com um joystick espetado, os pinos
    // do potenciômetro ficam soltos: o capacitor não carrega e INPT0-3 leem 0.
    // Isso não é detalhe — o Decathlon lê esses registradores, e ligar a
    // carga com joystick muda o comportamento dele.
    bool     pa_ligada;
    bool     pa_aterrado;    // VBLANK bit 7: capacitores em curto
    uint32_t pa_carga;       // color clocks desde que o aterramento foi solto
    uint8_t  paddle[4];      // posição de cada pá, 0..255

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

    // Pixel em que o RESPx foi disparado, na linha corrente, ou -1 se o
    // disparo foi numa linha anterior. Ver `copia_vale`.
    int16_t  pos_strobe_x[5];

    uint16_t lines_in_frame;  // linhas do último quadro fechado


    // Cache do compositor.
    //
    // Nada aqui é estado do chip: é só o que dá para calcular uma vez por
    // escrita em vez de 160 vezes por linha. O compositor rodava 41.920 vezes
    // por quadro e refazia, em cada pixel, a escala do NUSIZ, o deslocamento
    // das cópias, dois módulos por objeto e uma divisão por pixel de jogador.
    // Como nada disso muda entre uma escrita e a seguinte, passou para cá.
    //
    // A invalidação é grossa de propósito: **qualquer** escrita na TIA suja o
    // cache inteiro, e um RESPx que completa a volta também. Invalidar campo a
    // campo seria mais rápido e é exatamente o tipo de coisa que erra em
    // silêncio; e como um kernel escreve poucas vezes por linha, a diferença
    // não se paga.
    struct {
        uint32_t padrao[2];    // jogador expandido: bit d = pixel aceso

        // A geometria: onde cada objeto começa e quanto ocupa. Fica junta num
        // sub-struct para poder ser comparada de uma vez — ver `atualiza_cache`.
        tia_geo_t geo;

        // Memo do padrão expandido. Refazer os 8 a 32 bits do jogador custa um
        // laço, e o cache inteiro é invalidado a cada escrita na TIA — inclusive
        // por escritas que não têm nada a ver com o gráfico, como COLUBK. Estes
        // três dizem para que valores o padrão já foi montado.
        uint8_t  memo_grp[2];
        uint8_t  memo_nusiz[2];
        uint8_t  memo_refp[2];

        // Onde, nos 160 pixels da linha, algum objeto móvel pode aparecer.
        // É um superconjunto: uma cópia que o RESPx suprimiu continua marcada.
        // Serve só para o compositor saber onde NÃO precisa olhar — e é onde
        // está a maior parte da linha na maioria dos jogos.
        uint32_t ocupado[5];
    } cache;
    bool cache_sujo;
} tia_t;

// Bits de CTRLPF
#define TIA_CTRLPF_REF   0x01   // espelha a metade direita do playfield
#define TIA_CTRLPF_SCORE 0x02   // metade esquerda em COLUP0, direita em COLUP1
#define TIA_CTRLPF_PFP   0x04   // playfield na frente dos jogadores

// A carga, em linhas de varredura, nos dois batentes da pá.
//
// Estes dois números foram **medidos**, não escolhidos: com o Breakout, varri
// as 256 posições e olhei onde a raquete caía na tela. Abaixo de ~68 linhas
// ela some (o jogo não espera contagem tão curta) e acima de ~196 ela fica
// grudada na parede esquerda. Ou seja: o potenciômetro de verdade nunca chega
// a zero ohm — tem uma resistência de série —, e o curso útil dele é essa
// faixa. Mapear o curso inteiro do direcional nela é o que faz a raquete
// cobrir a tela de ponta a ponta em vez de metade dela.
//
// Aferido só com o Breakout. Se algum dia entrar outro jogo de pá no kit,
// vale conferir: a faixa é do hardware, não do jogo, e tem de servir para os
// dois.
#define TIA_PA_BASE_LINHAS   68   // batente de menor resistência
#define TIA_PA_CURSO_LINHAS 128   // de um batente ao outro

void tia_reset(tia_t *t);

// Posição de uma das quatro pás, 0 a 255. 0 é a carga instantânea.
void tia_set_paddle(tia_t *t, int i, uint8_t pos);

// Há pás espetadas no console? Com joystick, INPT0-3 leem 0.
void tia_set_paddles_ligadas(tia_t *t, bool ligadas);

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
// Não é const porque pode reconstruir o cache do compositor antes de
// responder. O estado do chip continua intocado.
uint8_t tia_objects_at(tia_t *t, int x);

#endif
