// tia_objects.h — jogadores, mísseis e bola da TIA (parte 2a).
//
// Separado de tia.c para poder ser testado sozinho. Aqui estão as partes que
// são **função pura dos registradores** — padrão de bits do jogador, largura
// do míssil e da bola, posições das cópias do NUSIZ e a lógica de colisão.
// Tudo isso dá para validar exaustivamente, sem referência externa.
//
// O que NÃO está aqui, de propósito: HMOVE e o alinhamento fino entre o
// contador do objeto e o pixel na tela. Esses dependem de detalhes de timing
// que só a comparação de frames contra o Stella fecha — ver README.
//
// Licença: GPLv2 (mesma do retro-go).

#ifndef TIA_OBJECTS_H
#define TIA_OBJECTS_H

#include <stdint.h>
#include <stdbool.h>

// ------------------------------------------------------------- colisões
//
// 15 bits, um para cada par de objetos. O truque (do Stella) é dar a cada
// objeto uma máscara com os bits dos pares de que ele participa: o E lógico
// das máscaras de dois objetos presentes acende exatamente o bit do par.
//
#define CX_P0 0x7C00u   // 0b0111110000000000
#define CX_P1 0x43C0u   // 0b0100001111000000
#define CX_M0 0x2238u   // 0b0010001000111000
#define CX_M1 0x1126u   // 0b0001000100100110
#define CX_BL 0x0895u   // 0b0000100010010101
#define CX_PF 0x044Bu   // 0b0000010001001011

typedef enum { OBJ_P0, OBJ_P1, OBJ_M0, OBJ_M1, OBJ_BL, OBJ_PF, OBJ_COUNT } tia_obj_t;

// Acumula as colisões de um pixel. `presentes` é um bitmap de tia_obj_t.
uint16_t tia_collision_bits(uint8_t presentes);

// A mesma coisa, calculada em vez de consultada. Existe para o teste conferir
// as 64 entradas da tabela — não use no caminho quente.
uint16_t tia_collision_bits_ref(uint8_t presentes);

// Os oito registradores CXxx lidos pela CPU, a partir dos 15 bits.
uint8_t tia_collision_reg(uint16_t bits, int reg);   // reg = 0..7

// --------------------------------------------------------------- jogador
//
// O jogador tem 8 bits de gráfico. NUSIZ define quantas cópias e, nos modos
// 5 e 7, o fator de ampliação (2x e 4x). REFP inverte os bits.
//
// Devolve o bit do gráfico visível na posição `i` dentro da cópia
// (0 <= i < 8*fator), ou false fora dela.
bool tia_player_pixel(uint8_t grp, bool refletido, uint8_t nusiz, int i);

// Fator de ampliação do jogador: 1, 2 ou 4.
int tia_player_scale(uint8_t nusiz);

// Deslocamento extra da borda esquerda quando o jogador está ampliado.
// Medido em nusiz.bin: nos modos 5 (2x) e 7 (4x) o desenho começa 1 pixel à
// direita da posição base; nos outros seis modos, na posição base mesmo.
int tia_player_offset(uint8_t nusiz);

// Largura do míssil em color clocks: 1, 2, 4 ou 8 (bits 4-5 do NUSIZ).
int tia_missile_width(uint8_t nusiz);

// Largura da bola em color clocks: 1, 2, 4 ou 8 (bits 4-5 do CTRLPF).
int tia_ball_width(uint8_t ctrlpf);

// ------------------------------------------------------ cópias do NUSIZ
//
// O contador horizontal do objeto vai de 0 a 159. A tabela de decodificação
// diz em quais valores começa uma cópia: sempre 156 (a primeira), mais as
// posições das cópias adicionais conforme o modo.
//
//   0 — uma cópia            4 — duas cópias, larga    (+64)
//   1 — duas cópias, junta   (+16)   5 — uma cópia, dobrada
//   2 — duas cópias, média   (+32)   6 — três cópias, médias (+32, +64)
//   3 — três cópias, juntas  (+16, +32)   7 — uma cópia, quádrupla
//
// Devolve o número da cópia (1, 2 ou 3) que começa no contador `c`, ou 0.
int tia_copy_starts_at(uint8_t nusiz, int c);

// -------------------------------------------- posição e movimento (parte 2b)
//
// Aqui a posição de um objeto é a coluna visível (0..159) onde começa a sua
// primeira cópia. Os números abaixo saíram da comparação quadro a quadro com o
// Stella — ver tests/ref/ e README.
//
// Deslocamentos das cópias adicionais, a partir da primeira.
#define TIA_COPY_CLOSE  16
#define TIA_COPY_MED    32
#define TIA_COPY_WIDE   64

// Posição da borda esquerda depois de um RESPx que chega à TIA no color clock
// `cc` (0..227). Dentro do HBLANK o objeto vai para a posição mínima; depois
// dela, para o pixel corrente mais o atraso do chip.
#define TIA_RESP_MIN     3   // medido: RESPx no HBLANK põe a borda no pixel 3
#define TIA_RESP_ATRASO  5   // atraso entre o strobe e o pixel — NÃO validado

// Míssil e bola largam **um color clock antes** do jogador. Os dois números
// acima foram medidos com jogadores (hmove.bin e nusiz.bin desenham jogadores),
// e usá-los para os outros três objetos põe míssil e bola um pixel à direita do
// lugar. Ver `tia_respx_pos`.
#define TIA_RESP_MIN_MB     2
#define TIA_RESP_ATRASO_MB  4
int tia_respx_pos(int cc, bool jogador);

// Deslocamento em pixels do nibble alto de HMPx/HMMx/HMBL. O nibble é um
// número de 4 bits com sinal e o objeto anda para a ESQUERDA por esse tanto:
// $70 (+7) anda 7 à esquerda, $80 (-8) anda 8 à direita, $F0 (-1) anda 1 à
// direita. Medido nas 16 faixas de hmove.bin.
int tia_hmove_delta(uint8_t hm_reg);

// Aplica o HMOVE a uma posição, com a volta em 160.
int tia_hmove_apply(int pos, uint8_t hm_reg);

// Quantos pixels da largura da região extra de blanking que um HMOVE
// disparado no início da linha acrescenta ao HBLANK. Medido: 8, exatos.
#define TIA_HMOVE_BLANK 8

// Se o pixel visível `x` cai dentro de alguma cópia do objeto cuja borda
// esquerda está em `pos`, devolve a distância até o início dessa cópia
// (0..largura-1). Devolve -1 se não cai em nenhuma.
//
// `largura` é o tamanho de uma cópia em pixels: 8*escala para o jogador, a
// largura do míssil ou da bola para os demais. Para a bola, passe nusiz = 0 —
// ela não tem cópias.
int tia_copy_offset(uint8_t nusiz, int pos, int x, int largura);

// Os inícios das cópias que o NUSIZ pede, em 0..159, na mesma ordem em que
// `tia_copy_offset` as varre. Devolve quantas são (1 a 3).
//
// Existe para que o compositor possa calcular isto uma vez por escrita em vez
// de uma vez por pixel. `tia_copy_offset` passou a ser escrito em cima dele:
// uma fonte só da verdade, senão a otimização e a referência divergiriam sem
// que teste nenhum percebesse.
int tia_copy_inicios(uint8_t nusiz, int pos, uint8_t *ini);

#endif
