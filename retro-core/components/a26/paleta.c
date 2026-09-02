// paleta.c — geração das 128 cores da TIA. Ver paleta.h.
// Licença: GPLv2 (mesma do retro-go).

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdbool.h>
#include "paleta.h"

// O sinal da TIA é luminância mais uma fase de cor. Cada matiz é um ângulo
// diferente na subportadora; a luminância é o nível de branco. Converter isso
// para RGB é a transformação YIQ padrão do NTSC.
//
// Os dois números que definem o resultado são o **ângulo da matiz 1** e o
// **passo entre matizes**. No NTSC o passo é negativo (as fases andam para
// trás); no PAL o chip tem menos matizes úteis e o passo é outro. Os valores
// abaixo são os de praxe e dão cores próximas das corretas — mas "próximas" é
// exatamente o que este projeto não aceita em outros lugares, e por isso eles
// estão marcados para serem substituídos pela medição de `roms/paleta.asm`.
typedef struct { double base, passo, sat; } sistema_t;

static const sistema_t SISTEMAS[2] = {
    /* NTSC */ { 192.0, -24.0, 0.34 },
    /* PAL  */ { 148.0, -22.5, 0.32 },
};

static uint16_t rgb565(double r, double g, double b, bool be)
{
    int R = (int)(r * 255.0 + 0.5), G = (int)(g * 255.0 + 0.5), B = (int)(b * 255.0 + 0.5);
    if (R < 0) R = 0;
    if (R > 255) R = 255;
    if (G < 0) G = 0;
    if (G > 255) G = 255;
    if (B < 0) B = 0;
    if (B > 255) B = 255;
    uint16_t c = (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
    return be ? (uint16_t)((c << 8) | (c >> 8)) : c;
}

void a26_paleta(uint16_t *dest, int sistema, bool be)
{
    const sistema_t *s = &SISTEMAS[sistema ? 1 : 0];

    for (int c = 0; c < 256; c += 2) {
        int matiz = (c >> 4) & 0x0F;
        int lum = (c >> 1) & 0x07;

        // A luminância vai de quase preto a quase branco em oito degraus.
        double y = 0.10 + lum * 0.1257;

        double r, g, b;
        if (matiz == 0) {
            r = g = b = y;                        // a coluna cinza
        } else {
            double ang = (s->base + (matiz - 1) * s->passo) * (M_PI / 180.0);
            double i = s->sat * cos(ang);
            double q = s->sat * sin(ang);
            r = y + 0.956 * i + 0.621 * q;
            g = y - 0.272 * i - 0.647 * q;
            b = y - 1.106 * i + 1.703 * q;
        }

        uint16_t v = rgb565(r, g, b, be);
        dest[c] = v;
        dest[c + 1] = v;                          // o bit 0 não existe no chip
    }
}
