// tia_audio.h — som da TIA (parte 3 do M3): dois canais idênticos.
//
// Cada canal tem três registradores:
//   AUDCx (4 bits) — forma de onda
//   AUDFx (5 bits) — divisor de frequência
//   AUDVx (4 bits) — volume
//
// O gerador não é uma tabela de formas de onda: são dois contadores
// realimentados (um de 5 bits para ruído, um de 4 para pulso) cujo
// entrelaçamento produz as 16 "vozes" do chip. Os valores de AUDC escolhem
// como a realimentação é montada, e daí saem períodos de 2, 6, 15, 31, 93,
// 465 e 511 — que são as ordens dos polinômios, não números arbitrários.
//
// O relógio do som roda duas vezes por linha de varredura, em duas fases:
//   fase 0 nos color clocks 9 e 81   — avança o divisor de frequência
//   fase 1 nos color clocks 37 e 149 — avança os contadores e a saída
//
// Modelo conferido contra o Stella (GPLv2), que implementa a versão derivada
// do esquemático do chip.
//
// Licença: GPLv2 (mesma do retro-go).

#ifndef TIA_AUDIO_H
#define TIA_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t audc;            // forma de onda, 4 bits
    uint8_t audf;            // divisor, 5 bits
    uint8_t audv;            // volume, 4 bits

    bool clock_enable;
    bool noise_feedback;
    bool noise_bit4;
    bool pulse_hold;

    uint8_t div_counter;     // 5 bits
    uint8_t pulse_counter;   // 4 bits
    uint8_t noise_counter;   // 5 bits
} tia_chan_t;

typedef struct {
    tia_chan_t ch[2];
} tia_audio_t;

void tia_audio_reset(tia_audio_t *a);

// Avança o som conforme o color clock corrente da linha. Chamar uma vez por
// color clock; a função sabe em quais deles agir.
void tia_audio_clock(tia_audio_t *a, int color_clock);

// Volume instantâneo de um canal (0..15) e a soma dos dois (0..30).
uint8_t tia_audio_channel_volume(const tia_chan_t *c);
uint8_t tia_audio_sample(const tia_audio_t *a);

// Escritas nos registradores. `ch` é 0 ou 1.
void tia_audio_set_audc(tia_audio_t *a, int ch, uint8_t v);
void tia_audio_set_audf(tia_audio_t *a, int ch, uint8_t v);
void tia_audio_set_audv(tia_audio_t *a, int ch, uint8_t v);

// As duas fases, expostas para os testes poderem pulsar o canal isolado.
void tia_chan_phase0(tia_chan_t *c);
void tia_chan_phase1(tia_chan_t *c);

#endif
