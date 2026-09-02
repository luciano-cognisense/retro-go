// tia_audio.c — som da TIA.
// Licença: GPLv2 (mesma do retro-go).
//
// Modelo derivado do esquemático do chip, conferido contra o Stella. Os dois
// contadores realimentados fazem todo o trabalho; não há tabela de formas de
// onda em lugar nenhum.

#include <string.h>
#include "tia_audio.h"

void tia_audio_reset(tia_audio_t *a)
{
    memset(a, 0, sizeof(*a));
}

// Fase 0 — decide as realimentações e avança o divisor de frequência.
void tia_chan_phase0(tia_chan_t *c)
{
    if (c->clock_enable) {
        c->noise_bit4 = (c->noise_counter & 0x01) != 0;

        switch (c->audc & 0x03) {
        case 0x00:
        case 0x01:
            c->pulse_hold = false;
            break;
        case 0x02:
            c->pulse_hold = (c->noise_counter & 0x1E) != 0x02;
            break;
        default: /* 0x03 */
            c->pulse_hold = !c->noise_bit4;
            break;
        }

        if ((c->audc & 0x03) == 0x00) {
            c->noise_feedback =
                (((c->pulse_counter ^ c->noise_counter) & 0x01) != 0) ||
                !(c->noise_counter || (c->pulse_counter != 0x0A)) ||
                !(c->audc & 0x0C);
        } else {
            c->noise_feedback =
                ((((c->noise_counter & 0x04) ? 1 : 0) ^ (c->noise_counter & 0x01)) != 0) ||
                (c->noise_counter == 0);
        }
    }

    c->clock_enable = (c->div_counter == c->audf);

    if (c->div_counter == c->audf || c->div_counter == 0x1F)
        c->div_counter = 0;
    else
        c->div_counter++;
}

// Fase 1 — avança os contadores de ruído e de pulso.
void tia_chan_phase1(tia_chan_t *c)
{
    if (!c->clock_enable)
        return;

    bool pulse_feedback = false;
    switch (c->audc >> 2) {
    case 0x00:
        pulse_feedback =
            ((((c->pulse_counter & 0x02) ? 1 : 0) ^ (c->pulse_counter & 0x01)) != 0) &&
            (c->pulse_counter != 0x0A) &&
            ((c->audc & 0x03) != 0);
        break;
    case 0x01:
        pulse_feedback = !(c->pulse_counter & 0x08);
        break;
    case 0x02:
        pulse_feedback = !c->noise_bit4;
        break;
    default: /* 0x03 */
        pulse_feedback = !((c->pulse_counter & 0x02) || !(c->pulse_counter & 0x0E));
        break;
    }

    c->noise_counter >>= 1;
    if (c->noise_feedback)
        c->noise_counter |= 0x10;

    if (!c->pulse_hold) {
        c->pulse_counter = (uint8_t)(~(c->pulse_counter >> 1) & 0x07);
        if (pulse_feedback)
            c->pulse_counter |= 0x08;
    }
}

void tia_audio_clock(tia_audio_t *a, int color_clock)
{
    // Duas fases, duas vezes por linha. As posições vêm do chip.
    if (color_clock == 9 || color_clock == 81) {
        tia_chan_phase0(&a->ch[0]);
        tia_chan_phase0(&a->ch[1]);
    } else if (color_clock == 37 || color_clock == 149) {
        tia_chan_phase1(&a->ch[0]);
        tia_chan_phase1(&a->ch[1]);
    }
}

uint8_t tia_audio_channel_volume(const tia_chan_t *c)
{
    return (uint8_t)((c->pulse_counter & 0x01) * c->audv);
}

uint8_t tia_audio_sample(const tia_audio_t *a)
{
    return (uint8_t)(tia_audio_channel_volume(&a->ch[0]) +
                     tia_audio_channel_volume(&a->ch[1]));
}

void tia_audio_set_audc(tia_audio_t *a, int ch, uint8_t v) { a->ch[ch & 1].audc = v & 0x0F; }
void tia_audio_set_audf(tia_audio_t *a, int ch, uint8_t v) { a->ch[ch & 1].audf = v & 0x1F; }
void tia_audio_set_audv(tia_audio_t *a, int ch, uint8_t v) { a->ch[ch & 1].audv = v & 0x0F; }
