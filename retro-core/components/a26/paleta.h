// paleta.h — as 128 cores da TIA em RGB565.
//
// A TIA não tem paleta programável: o valor escrito em COLUBK/COLUPx escolhe
// uma das 128 cores fixas do chip — matiz nos 4 bits altos, luminância nos
// bits 1-3, e o bit 0 não existe. Emular isso exige uma tabela de RGB, e essa
// tabela não sai de raciocínio nenhum: são as cores que o modulador NTSC (ou
// PAL) produz.
//
// A tabela aqui é **provisória**, gerada pela fórmula YIQ padrão. A definitiva
// vem de medição: `roms/paleta.asm` pinta as 128 cores, uma por linha, e um
// instantâneo do Stella devolve os RGB exatos. É o mesmo método que fechou o
// HMOVE, o NUSIZ e o atraso de escrita da TIA.
//
// Licença: GPLv2 (mesma do retro-go).

#ifndef A26_PALETA_H
#define A26_PALETA_H

#include <stdint.h>

// Preenche 256 entradas RGB565 (o índice é o byte de cor da TIA; o bit 0 é
// ignorado pelo chip, então as entradas ímpares repetem as pares).
//   sistema: 0 = NTSC, 1 = PAL
//   be: true para big-endian (RG_PIXEL_PAL565_BE)
void a26_paleta(uint16_t *dest, int sistema, bool be);

#endif
