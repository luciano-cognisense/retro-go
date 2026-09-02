// riot.c — MOS 6532 RIOT do Atari 2600.
// Licença: GPLv2 (mesma do retro-go).

#include <string.h>
#include "riot.h"

static const uint16_t DIVIDER[4]  = { 1, 8, 64, 1024 };
static const uint8_t  DIV_SHIFT[4] = { 0, 3, 6, 10 };

void riot_reset(riot_t *r)
{
    memset(r, 0, sizeof(*r));
    r->timer = 0;
    r->divider = 1024;
    r->divider_shift = 10;
    r->sub_timer = 0;
    r->wrapped_this_cycle = false;
    r->int_flag = 0;
    r->in_a = 0xFF;          // nada pressionado
    r->in_b = 0xFF;
    r->edge_positive = false;
    r->last_cycle = 0;
}

// Detector de borda em PA7, com o sincronizador de dois estágios do Stella.
// Amostrado uma vez por atualização, não por ciclo: as entradas só mudam
// entre quadros, então isso não é observável na prática.
static void pa7_edge(riot_t *r)
{
    bool raw = ((r->ddra & 0x80) != 0)
                   ? ((r->outa & 0x80) != 0)
                   : ((r->in_a & 0x80) != 0);

    bool stable = r->pa7_sync1;
    r->pa7_sync1 = raw;

    if (r->pa7_last_stable != stable && stable == r->edge_positive)
        r->int_flag |= RIOT_INT_PA7;

    r->pa7_last_stable = stable;
}

void riot_update(riot_t *r, uint64_t cycle)
{
    uint32_t cycles = (uint32_t)(cycle - r->last_cycle);
    if (cycles == 0)
        return;

    uint32_t sub_old = r->sub_timer;

    r->wrapped_this_cycle = false;
    r->sub_timer = (cycles + r->sub_timer) & (uint32_t)(r->divider - 1);

    if ((r->int_flag & RIOT_INT_TIMER) == 0) {
        uint32_t ticks = (cycles + sub_old) >> r->divider_shift;
        if (ticks > r->timer) {
            // O temporizador estourou no meio do intervalo. Descontamos os
            // ciclos gastos até o estouro; o resto é contado de um em um.
            cycles -= (uint32_t)(((uint32_t)(r->timer + 1) << r->divider_shift) - sub_old);
            r->wrapped_this_cycle = (cycles == 0);
            r->timer = 0xFF;
            r->int_flag |= RIOT_INT_TIMER;
        } else {
            r->timer = (uint8_t)(r->timer - ticks);
            cycles = 0;
        }
    }

    // Depois do estouro o temporizador decrementa a cada ciclo, seja qual for
    // o divisor programado, até a próxima escrita em TIMxT.
    if (r->int_flag & RIOT_INT_TIMER) {
        r->timer = (uint8_t)(r->timer - cycles);
        r->wrapped_this_cycle = (r->timer == 0xFF);
    }

    pa7_edge(r);
    r->last_cycle = cycle;
}

static void set_timer(riot_t *r, uint8_t value, uint8_t interval)
{
    r->divider = DIVIDER[interval];
    r->divider_shift = DIV_SHIFT[interval];
    r->timer = value;
    r->sub_timer = (uint32_t)(r->divider - 1);

    // A flag some na escrita — mas não se o estouro aconteceu neste mesmo
    // ciclo, caso em que ela ainda não é válida.
    if (!r->wrapped_this_cycle)
        r->int_flag &= (uint8_t)~RIOT_INT_TIMER;
}

uint8_t riot_read(riot_t *r, uint16_t addr, uint64_t cycle)
{
    riot_update(r, cycle);

    // A9 = 0 -> RAM
    if ((addr & 0x0200) == 0)
        return r->ram[addr & 0x7F];

    switch (addr & 0x07) {
    case 0x00:   // SWCHA — controles
        // Pino em nível alto por padrão; só cai se o dispositivo externo
        // puxar para baixo, ou se for saída (DDR=1) com o bit de saída 0.
        return (uint8_t)((r->outa | (uint8_t)~r->ddra) & r->in_a);

    case 0x01:   // SWACNT
        return r->ddra;

    case 0x02:   // SWCHB — chaves do console
        return (uint8_t)((r->outb | (uint8_t)~r->ddrb) & (r->in_b | r->ddrb));

    case 0x03:   // SWBCNT
        return r->ddrb;

    case 0x04:   // INTIM
    case 0x06:
        // Ler INTIM limpa a flag do temporizador — exceto no ciclo exato do
        // estouro, quando ela ainda não foi validada.
        if (!r->wrapped_this_cycle)
            r->int_flag &= (uint8_t)~RIOT_INT_TIMER;
        return r->timer;

    case 0x05:   // INSTAT / TIMINT
    case 0x07:
    default: {
        uint8_t result = r->int_flag;
        r->int_flag &= (uint8_t)~RIOT_INT_PA7;   // ler limpa só o bit de PA7
        return result;
    }
    }
}

void riot_write(riot_t *r, uint16_t addr, uint8_t val, uint64_t cycle)
{
    riot_update(r, cycle);

    if ((addr & 0x0200) == 0) {          // A9 = 0 -> RAM
        r->ram[addr & 0x7F] = val;
        return;
    }

    if (addr & 0x04) {                   // A2 = 1 -> temporizador / borda
        if (addr & 0x10)                 // A4 = 1 -> TIMxT, A1A0 = intervalo
            set_timer(r, val, (uint8_t)(addr & 0x03));
        else                             // A4 = 0 -> controle do detector
            r->edge_positive = (addr & 0x01) != 0;
        return;
    }

    switch (addr & 0x03) {
    case 0: r->outa = val; break;        // SWCHA
    case 1: r->ddra = val; break;        // SWACNT
    case 2: r->outb = val; break;        // SWCHB
    default: r->ddrb = val; break;       // SWBCNT
    }
}
