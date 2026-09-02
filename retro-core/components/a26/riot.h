// riot.h — MOS 6532 RIOT do Atari 2600 (RAM, I/O e temporizador)
//
// O chip guarda os 128 bytes de RAM do console, as duas portas de entrada
// (controles e chaves) e um temporizador com quatro divisores. É o segundo
// marco do núcleo, depois do 6507.
//
// Semântica conferida contra o Stella (GPLv2), que é a implementação de
// referência do 2600 — em especial as regras de `wrapped_this_cycle`, que
// decidem se uma leitura de INTIM limpa ou não a flag de interrupção.
//
// Licença: GPLv2 (mesma do retro-go).

#ifndef RIOT_H
#define RIOT_H

#include <stdint.h>
#include <stdbool.h>

// Bits de INSTAT/TIMINT
#define RIOT_INT_TIMER 0x80
#define RIOT_INT_PA7   0x40

typedef struct {
    uint8_t ram[128];

    // Temporizador
    uint8_t  timer;          // valor lido em INTIM
    uint32_t sub_timer;      // contagem dentro do período do divisor
    uint16_t divider;        // 1, 8, 64 ou 1024
    uint8_t  divider_shift;  // 0, 3, 6 ou 10
    bool     wrapped_this_cycle;
    uint8_t  int_flag;

    // Portas
    uint8_t ddra, ddrb;      // direção (1 = saída)
    uint8_t outa, outb;      // registrador de saída
    uint8_t in_a;            // controles: entrada externa (nível alto = solto)
    uint8_t in_b;            // chaves do console

    // Detector de borda em PA7
    bool edge_positive;
    bool pa7_sync1;
    bool pa7_last_stable;

    uint64_t last_cycle;
} riot_t;

void riot_reset(riot_t *r);

// Adianta o chip até `cycle`. Chamado antes de qualquer acesso; o custo é
// independente de quantos ciclos passaram.
void riot_update(riot_t *r, uint64_t cycle);

// `addr` é o endereço já dentro do espaço do RIOT (A9 seleciona RAM x I/O).
uint8_t riot_read(riot_t *r, uint16_t addr, uint64_t cycle);
void riot_write(riot_t *r, uint16_t addr, uint8_t val, uint64_t cycle);

#endif
