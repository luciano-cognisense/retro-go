// m6502.c — núcleo 6502/6507 dirigido por ciclo de barramento.
// Licença: GPLv2 (mesma do retro-go).
//
// Convenções seguidas (comportamento NMOS, não 65C02):
//  - todo ciclo faz um acesso ao barramento, ciclos internos inclusive;
//  - leitura falsa (dummy read) antes de índice que cruza página;
//  - RMW escreve duas vezes: o valor antigo e depois o novo;
//  - JMP (ind) tem o bug de wrap de página no ponteiro;
//  - modo decimal com as regras NMOS de N/V/Z;
//  - opcodes não documentados implementados, instáveis inclusive.

#include "m6502.h"

// ---------------------------------------------------------------- barramento

static inline uint8_t rd(m6502_t *c, uint16_t a) { return c->read(c->ctx, a); }
static inline void wr(m6502_t *c, uint16_t a, uint8_t v) { c->write(c->ctx, a, v); }

static inline uint8_t fetch(m6502_t *c) { return rd(c, c->pc++); }

static inline void push(m6502_t *c, uint8_t v) { wr(c, 0x0100 | c->s, v); c->s--; }
static inline uint8_t pop(m6502_t *c) { c->s++; return rd(c, 0x0100 | c->s); }

// ------------------------------------------------------------------- flags

static inline void set_flag(m6502_t *c, uint8_t bit, bool on)
{
    if (on) c->p |= bit; else c->p &= (uint8_t)~bit;
}

static inline uint8_t set_nz(m6502_t *c, uint8_t v)
{
    set_flag(c, M6502_Z, v == 0);
    set_flag(c, M6502_N, (v & 0x80) != 0);
    return v;
}

// ---------------------------------------------------- modos de endereçamento
// Cada função consome os ciclos do seu modo e devolve o endereço efetivo.

static inline uint16_t am_zp(m6502_t *c)
{
    return fetch(c);
}

static inline uint16_t am_zpx(m6502_t *c)
{
    uint8_t base = fetch(c);
    rd(c, base);                       // leitura falsa no endereço sem índice
    return (uint8_t)(base + c->x);
}

static inline uint16_t am_zpy(m6502_t *c)
{
    uint8_t base = fetch(c);
    rd(c, base);
    return (uint8_t)(base + c->y);
}

static inline uint16_t am_abs(m6502_t *c)
{
    uint8_t lo = fetch(c);
    uint8_t hi = fetch(c);
    return (uint16_t)(lo | (hi << 8));
}

// Variante de leitura: só gasta o ciclo extra se cruzar página.
static inline uint16_t am_abi_r(m6502_t *c, uint8_t idx)
{
    uint8_t lo = fetch(c);
    uint8_t hi = fetch(c);
    uint16_t base = (uint16_t)(lo | (hi << 8));
    uint16_t addr = (uint16_t)(base + idx);
    if ((addr & 0xFF00) != (base & 0xFF00))
        rd(c, (uint16_t)((base & 0xFF00) | (addr & 0x00FF)));  // endereço errado
    return addr;
}

// Variante de escrita/RMW: a leitura falsa acontece sempre.
static inline uint16_t am_abi_w(m6502_t *c, uint8_t idx)
{
    uint8_t lo = fetch(c);
    uint8_t hi = fetch(c);
    uint16_t base = (uint16_t)(lo | (hi << 8));
    uint16_t addr = (uint16_t)(base + idx);
    rd(c, (uint16_t)((base & 0xFF00) | (addr & 0x00FF)));
    return addr;
}

static inline uint16_t am_izx(m6502_t *c)
{
    uint8_t zp = fetch(c);
    rd(c, zp);
    uint8_t p = (uint8_t)(zp + c->x);
    uint8_t lo = rd(c, p);
    uint8_t hi = rd(c, (uint8_t)(p + 1));   // wrap dentro da página zero
    return (uint16_t)(lo | (hi << 8));
}

static inline uint16_t am_izy_r(m6502_t *c)
{
    uint8_t zp = fetch(c);
    uint8_t lo = rd(c, zp);
    uint8_t hi = rd(c, (uint8_t)(zp + 1));
    uint16_t base = (uint16_t)(lo | (hi << 8));
    uint16_t addr = (uint16_t)(base + c->y);
    if ((addr & 0xFF00) != (base & 0xFF00))
        rd(c, (uint16_t)((base & 0xFF00) | (addr & 0x00FF)));
    return addr;
}

static inline uint16_t am_izy_w(m6502_t *c)
{
    uint8_t zp = fetch(c);
    uint8_t lo = rd(c, zp);
    uint8_t hi = rd(c, (uint8_t)(zp + 1));
    uint16_t base = (uint16_t)(lo | (hi << 8));
    uint16_t addr = (uint16_t)(base + c->y);
    rd(c, (uint16_t)((base & 0xFF00) | (addr & 0x00FF)));
    return addr;
}

// ------------------------------------------------------------------- ALU

static void op_adc(m6502_t *c, uint8_t m)
{
    uint8_t a = c->a;
    uint8_t carry = (c->p & M6502_C) ? 1 : 0;

    if (c->p & M6502_D) {
        // NMOS: Z vem do resultado binário; N e V do nibble alto antes do
        // ajuste final; C do ajuste.
        unsigned bin = (unsigned)a + m + carry;
        set_flag(c, M6502_Z, (bin & 0xFF) == 0);

        unsigned lo = (unsigned)(a & 0x0F) + (m & 0x0F) + carry;
        unsigned hi = (unsigned)(a & 0xF0) + (m & 0xF0);
        if (lo > 0x09) { hi += 0x10; lo += 0x06; }

        set_flag(c, M6502_N, (hi & 0x80) != 0);
        set_flag(c, M6502_V, (~(a ^ m) & (a ^ (uint8_t)hi) & 0x80) != 0);

        if (hi > 0x90) hi += 0x60;
        set_flag(c, M6502_C, (hi & 0xFF00) != 0);
        c->a = (uint8_t)((lo & 0x0F) | (hi & 0xF0));
    } else {
        unsigned sum = (unsigned)a + m + carry;
        set_flag(c, M6502_C, sum > 0xFF);
        set_flag(c, M6502_V, (~(a ^ m) & (a ^ (uint8_t)sum) & 0x80) != 0);
        c->a = set_nz(c, (uint8_t)sum);
    }
}

static void op_sbc(m6502_t *c, uint8_t m)
{
    uint8_t a = c->a;
    uint8_t borrow = (c->p & M6502_C) ? 0 : 1;

    // Em modo decimal todas as flags saem do cálculo binário.
    unsigned bin = (unsigned)a - m - borrow;
    set_flag(c, M6502_C, bin < 0x100);
    set_flag(c, M6502_V, ((a ^ m) & (a ^ (uint8_t)bin) & 0x80) != 0);
    set_nz(c, (uint8_t)bin);

    if (c->p & M6502_D) {
        int lo = (a & 0x0F) - (m & 0x0F) - borrow;
        int hi = (a >> 4) - (m >> 4);
        if (lo & 0x10) { lo -= 6; hi--; }
        if (hi & 0x10) hi -= 6;
        c->a = (uint8_t)(((hi & 0x0F) << 4) | (lo & 0x0F));
    } else {
        c->a = (uint8_t)bin;
    }
}

static void op_cmp_reg(m6502_t *c, uint8_t reg, uint8_t m)
{
    unsigned d = (unsigned)reg - m;
    set_flag(c, M6502_C, reg >= m);
    set_nz(c, (uint8_t)d);
}

static void op_bit(m6502_t *c, uint8_t m)
{
    set_flag(c, M6502_Z, (c->a & m) == 0);
    set_flag(c, M6502_N, (m & 0x80) != 0);
    set_flag(c, M6502_V, (m & 0x40) != 0);
}

static uint8_t op_asl(m6502_t *c, uint8_t v)
{
    set_flag(c, M6502_C, (v & 0x80) != 0);
    return set_nz(c, (uint8_t)(v << 1));
}

static uint8_t op_lsr(m6502_t *c, uint8_t v)
{
    set_flag(c, M6502_C, (v & 0x01) != 0);
    return set_nz(c, (uint8_t)(v >> 1));
}

static uint8_t op_rol(m6502_t *c, uint8_t v)
{
    uint8_t in = (c->p & M6502_C) ? 1 : 0;
    set_flag(c, M6502_C, (v & 0x80) != 0);
    return set_nz(c, (uint8_t)((v << 1) | in));
}

static uint8_t op_ror(m6502_t *c, uint8_t v)
{
    uint8_t in = (c->p & M6502_C) ? 0x80 : 0;
    set_flag(c, M6502_C, (v & 0x01) != 0);
    return set_nz(c, (uint8_t)((v >> 1) | in));
}

static uint8_t op_inc(m6502_t *c, uint8_t v) { return set_nz(c, (uint8_t)(v + 1)); }
static uint8_t op_dec(m6502_t *c, uint8_t v) { return set_nz(c, (uint8_t)(v - 1)); }

// ------------------------------------------------------------------ desvios

static void branch(m6502_t *c, bool taken)
{
    int8_t off = (int8_t)fetch(c);
    if (!taken)
        return;
    rd(c, c->pc);                                   // ciclo interno
    uint16_t dst = (uint16_t)(c->pc + off);
    if ((dst & 0xFF00) != (c->pc & 0xFF00))
        rd(c, (uint16_t)((c->pc & 0xFF00) | (dst & 0x00FF)));  // correção de página
    c->pc = dst;
}

// -------------------------------------------------------------- interrupções

static void service_interrupt(m6502_t *c, uint16_t vector, bool brk)
{
    if (brk) {
        fetch(c);                       // BRK descarta o byte seguinte
    } else {
        rd(c, c->pc);
        rd(c, c->pc);
    }
    push(c, (uint8_t)(c->pc >> 8));
    push(c, (uint8_t)(c->pc & 0xFF));
    push(c, (uint8_t)(c->p | M6502_U | (brk ? M6502_B : 0)));
    c->p |= M6502_I;
    uint8_t lo = rd(c, vector);
    uint8_t hi = rd(c, (uint16_t)(vector + 1));
    c->pc = (uint16_t)(lo | (hi << 8));
}

void m6502_reset(m6502_t *c)
{
    c->jammed = false;
    c->p |= M6502_I | M6502_U;
    rd(c, c->pc);
    rd(c, c->pc);
    rd(c, c->pc);
    rd(c, (uint16_t)(0x0100 | c->s)); c->s--;
    rd(c, (uint16_t)(0x0100 | c->s)); c->s--;
    rd(c, (uint16_t)(0x0100 | c->s)); c->s--;
    uint8_t lo = rd(c, 0xFFFC);
    uint8_t hi = rd(c, 0xFFFD);
    c->pc = (uint16_t)(lo | (hi << 8));
}

// -------------------------------------------------------------------- macros

#define RMW(ADDR, OP) do {                  \
        uint16_t _a = (ADDR);               \
        uint8_t _v = rd(c, _a);             \
        wr(c, _a, _v);                      \
        wr(c, _a, OP(c, _v));               \
    } while (0)

// Combinada (ilegal): RMW cujo resultado também alimenta uma operação na CPU.
#define RMW2(ADDR, OP, THEN) do {           \
        uint16_t _a = (ADDR);               \
        uint8_t _v = rd(c, _a);             \
        wr(c, _a, _v);                      \
        _v = OP(c, _v);                     \
        wr(c, _a, _v);                      \
        THEN;                               \
    } while (0)

// Endereço "mágico" dos opcodes instáveis SHA/SHX/SHY/TAS: o valor gravado é
// reg & (byte alto do endereço + 1). Se o índice cruzou a página, o byte alto
// do endereço também é corrompido pelo valor.
static void store_high(m6502_t *c, uint8_t lo_base, uint8_t hi_base, uint8_t idx, uint8_t reg)
{
    uint16_t base = (uint16_t)(lo_base | (hi_base << 8));
    uint16_t addr = (uint16_t)(base + idx);
    uint8_t val = (uint8_t)(reg & (uint8_t)(hi_base + 1));
    if ((addr & 0xFF00) != (base & 0xFF00))
        addr = (uint16_t)((val << 8) | (addr & 0x00FF));
    wr(c, addr, val);
}

// --------------------------------------------------------------------- passo

void m6502_step(m6502_t *c)
{
    if (c->jammed) {
        rd(c, c->pc);
        return;
    }

    if (c->nmi_pending) {
        c->nmi_pending = false;
        rd(c, c->pc);
        service_interrupt(c, 0xFFFA, false);
        return;
    }
    if (c->irq_line && !(c->p & M6502_I)) {
        rd(c, c->pc);
        service_interrupt(c, 0xFFFE, false);
        return;
    }

    uint8_t op = fetch(c);

    switch (op) {

    // ------------------------------------------------------------ carga
    case 0xA9: c->a = set_nz(c, fetch(c)); break;
    case 0xA5: c->a = set_nz(c, rd(c, am_zp(c))); break;
    case 0xB5: c->a = set_nz(c, rd(c, am_zpx(c))); break;
    case 0xAD: c->a = set_nz(c, rd(c, am_abs(c))); break;
    case 0xBD: c->a = set_nz(c, rd(c, am_abi_r(c, c->x))); break;
    case 0xB9: c->a = set_nz(c, rd(c, am_abi_r(c, c->y))); break;
    case 0xA1: c->a = set_nz(c, rd(c, am_izx(c))); break;
    case 0xB1: c->a = set_nz(c, rd(c, am_izy_r(c))); break;

    case 0xA2: c->x = set_nz(c, fetch(c)); break;
    case 0xA6: c->x = set_nz(c, rd(c, am_zp(c))); break;
    case 0xB6: c->x = set_nz(c, rd(c, am_zpy(c))); break;
    case 0xAE: c->x = set_nz(c, rd(c, am_abs(c))); break;
    case 0xBE: c->x = set_nz(c, rd(c, am_abi_r(c, c->y))); break;

    case 0xA0: c->y = set_nz(c, fetch(c)); break;
    case 0xA4: c->y = set_nz(c, rd(c, am_zp(c))); break;
    case 0xB4: c->y = set_nz(c, rd(c, am_zpx(c))); break;
    case 0xAC: c->y = set_nz(c, rd(c, am_abs(c))); break;
    case 0xBC: c->y = set_nz(c, rd(c, am_abi_r(c, c->x))); break;

    // ---------------------------------------------------------- gravação
    case 0x85: wr(c, am_zp(c), c->a); break;
    case 0x95: wr(c, am_zpx(c), c->a); break;
    case 0x8D: wr(c, am_abs(c), c->a); break;
    case 0x9D: wr(c, am_abi_w(c, c->x), c->a); break;
    case 0x99: wr(c, am_abi_w(c, c->y), c->a); break;
    case 0x81: wr(c, am_izx(c), c->a); break;
    case 0x91: wr(c, am_izy_w(c), c->a); break;

    case 0x86: wr(c, am_zp(c), c->x); break;
    case 0x96: wr(c, am_zpy(c), c->x); break;
    case 0x8E: wr(c, am_abs(c), c->x); break;

    case 0x84: wr(c, am_zp(c), c->y); break;
    case 0x94: wr(c, am_zpx(c), c->y); break;
    case 0x8C: wr(c, am_abs(c), c->y); break;

    // ------------------------------------------------- transferências
    case 0xAA: rd(c, c->pc); c->x = set_nz(c, c->a); break;
    case 0xA8: rd(c, c->pc); c->y = set_nz(c, c->a); break;
    case 0x8A: rd(c, c->pc); c->a = set_nz(c, c->x); break;
    case 0x98: rd(c, c->pc); c->a = set_nz(c, c->y); break;
    case 0xBA: rd(c, c->pc); c->x = set_nz(c, c->s); break;
    case 0x9A: rd(c, c->pc); c->s = c->x; break;

    // ------------------------------------------------------------ pilha
    case 0x48: rd(c, c->pc); push(c, c->a); break;
    case 0x08: rd(c, c->pc); push(c, (uint8_t)(c->p | M6502_B | M6502_U)); break;
    case 0x68: rd(c, c->pc); rd(c, (uint16_t)(0x0100 | c->s)); c->a = set_nz(c, pop(c)); break;
    case 0x28: rd(c, c->pc); rd(c, (uint16_t)(0x0100 | c->s));
               c->p = (uint8_t)((pop(c) & ~M6502_B) | M6502_U); break;

    // ------------------------------------------------------------- lógica
    case 0x29: c->a = set_nz(c, c->a & fetch(c)); break;
    case 0x25: c->a = set_nz(c, c->a & rd(c, am_zp(c))); break;
    case 0x35: c->a = set_nz(c, c->a & rd(c, am_zpx(c))); break;
    case 0x2D: c->a = set_nz(c, c->a & rd(c, am_abs(c))); break;
    case 0x3D: c->a = set_nz(c, c->a & rd(c, am_abi_r(c, c->x))); break;
    case 0x39: c->a = set_nz(c, c->a & rd(c, am_abi_r(c, c->y))); break;
    case 0x21: c->a = set_nz(c, c->a & rd(c, am_izx(c))); break;
    case 0x31: c->a = set_nz(c, c->a & rd(c, am_izy_r(c))); break;

    case 0x09: c->a = set_nz(c, c->a | fetch(c)); break;
    case 0x05: c->a = set_nz(c, c->a | rd(c, am_zp(c))); break;
    case 0x15: c->a = set_nz(c, c->a | rd(c, am_zpx(c))); break;
    case 0x0D: c->a = set_nz(c, c->a | rd(c, am_abs(c))); break;
    case 0x1D: c->a = set_nz(c, c->a | rd(c, am_abi_r(c, c->x))); break;
    case 0x19: c->a = set_nz(c, c->a | rd(c, am_abi_r(c, c->y))); break;
    case 0x01: c->a = set_nz(c, c->a | rd(c, am_izx(c))); break;
    case 0x11: c->a = set_nz(c, c->a | rd(c, am_izy_r(c))); break;

    case 0x49: c->a = set_nz(c, c->a ^ fetch(c)); break;
    case 0x45: c->a = set_nz(c, c->a ^ rd(c, am_zp(c))); break;
    case 0x55: c->a = set_nz(c, c->a ^ rd(c, am_zpx(c))); break;
    case 0x4D: c->a = set_nz(c, c->a ^ rd(c, am_abs(c))); break;
    case 0x5D: c->a = set_nz(c, c->a ^ rd(c, am_abi_r(c, c->x))); break;
    case 0x59: c->a = set_nz(c, c->a ^ rd(c, am_abi_r(c, c->y))); break;
    case 0x41: c->a = set_nz(c, c->a ^ rd(c, am_izx(c))); break;
    case 0x51: c->a = set_nz(c, c->a ^ rd(c, am_izy_r(c))); break;

    case 0x24: op_bit(c, rd(c, am_zp(c))); break;
    case 0x2C: op_bit(c, rd(c, am_abs(c))); break;

    // ------------------------------------------------------- aritmética
    case 0x69: op_adc(c, fetch(c)); break;
    case 0x65: op_adc(c, rd(c, am_zp(c))); break;
    case 0x75: op_adc(c, rd(c, am_zpx(c))); break;
    case 0x6D: op_adc(c, rd(c, am_abs(c))); break;
    case 0x7D: op_adc(c, rd(c, am_abi_r(c, c->x))); break;
    case 0x79: op_adc(c, rd(c, am_abi_r(c, c->y))); break;
    case 0x61: op_adc(c, rd(c, am_izx(c))); break;
    case 0x71: op_adc(c, rd(c, am_izy_r(c))); break;

    case 0xE9: case 0xEB: op_sbc(c, fetch(c)); break;   // 0xEB é SBC ilegal
    case 0xE5: op_sbc(c, rd(c, am_zp(c))); break;
    case 0xF5: op_sbc(c, rd(c, am_zpx(c))); break;
    case 0xED: op_sbc(c, rd(c, am_abs(c))); break;
    case 0xFD: op_sbc(c, rd(c, am_abi_r(c, c->x))); break;
    case 0xF9: op_sbc(c, rd(c, am_abi_r(c, c->y))); break;
    case 0xE1: op_sbc(c, rd(c, am_izx(c))); break;
    case 0xF1: op_sbc(c, rd(c, am_izy_r(c))); break;

    case 0xC9: op_cmp_reg(c, c->a, fetch(c)); break;
    case 0xC5: op_cmp_reg(c, c->a, rd(c, am_zp(c))); break;
    case 0xD5: op_cmp_reg(c, c->a, rd(c, am_zpx(c))); break;
    case 0xCD: op_cmp_reg(c, c->a, rd(c, am_abs(c))); break;
    case 0xDD: op_cmp_reg(c, c->a, rd(c, am_abi_r(c, c->x))); break;
    case 0xD9: op_cmp_reg(c, c->a, rd(c, am_abi_r(c, c->y))); break;
    case 0xC1: op_cmp_reg(c, c->a, rd(c, am_izx(c))); break;
    case 0xD1: op_cmp_reg(c, c->a, rd(c, am_izy_r(c))); break;

    case 0xE0: op_cmp_reg(c, c->x, fetch(c)); break;
    case 0xE4: op_cmp_reg(c, c->x, rd(c, am_zp(c))); break;
    case 0xEC: op_cmp_reg(c, c->x, rd(c, am_abs(c))); break;

    case 0xC0: op_cmp_reg(c, c->y, fetch(c)); break;
    case 0xC4: op_cmp_reg(c, c->y, rd(c, am_zp(c))); break;
    case 0xCC: op_cmp_reg(c, c->y, rd(c, am_abs(c))); break;

    // ------------------------------------------------- incremento/decremento
    case 0xE6: RMW(am_zp(c), op_inc); break;
    case 0xF6: RMW(am_zpx(c), op_inc); break;
    case 0xEE: RMW(am_abs(c), op_inc); break;
    case 0xFE: RMW(am_abi_w(c, c->x), op_inc); break;

    case 0xC6: RMW(am_zp(c), op_dec); break;
    case 0xD6: RMW(am_zpx(c), op_dec); break;
    case 0xCE: RMW(am_abs(c), op_dec); break;
    case 0xDE: RMW(am_abi_w(c, c->x), op_dec); break;

    case 0xE8: rd(c, c->pc); c->x = set_nz(c, (uint8_t)(c->x + 1)); break;
    case 0xC8: rd(c, c->pc); c->y = set_nz(c, (uint8_t)(c->y + 1)); break;
    case 0xCA: rd(c, c->pc); c->x = set_nz(c, (uint8_t)(c->x - 1)); break;
    case 0x88: rd(c, c->pc); c->y = set_nz(c, (uint8_t)(c->y - 1)); break;

    // ------------------------------------------------------- deslocamentos
    case 0x0A: rd(c, c->pc); c->a = op_asl(c, c->a); break;
    case 0x06: RMW(am_zp(c), op_asl); break;
    case 0x16: RMW(am_zpx(c), op_asl); break;
    case 0x0E: RMW(am_abs(c), op_asl); break;
    case 0x1E: RMW(am_abi_w(c, c->x), op_asl); break;

    case 0x4A: rd(c, c->pc); c->a = op_lsr(c, c->a); break;
    case 0x46: RMW(am_zp(c), op_lsr); break;
    case 0x56: RMW(am_zpx(c), op_lsr); break;
    case 0x4E: RMW(am_abs(c), op_lsr); break;
    case 0x5E: RMW(am_abi_w(c, c->x), op_lsr); break;

    case 0x2A: rd(c, c->pc); c->a = op_rol(c, c->a); break;
    case 0x26: RMW(am_zp(c), op_rol); break;
    case 0x36: RMW(am_zpx(c), op_rol); break;
    case 0x2E: RMW(am_abs(c), op_rol); break;
    case 0x3E: RMW(am_abi_w(c, c->x), op_rol); break;

    case 0x6A: rd(c, c->pc); c->a = op_ror(c, c->a); break;
    case 0x66: RMW(am_zp(c), op_ror); break;
    case 0x76: RMW(am_zpx(c), op_ror); break;
    case 0x6E: RMW(am_abs(c), op_ror); break;
    case 0x7E: RMW(am_abi_w(c, c->x), op_ror); break;

    // ------------------------------------------------------------- desvios
    case 0x10: branch(c, !(c->p & M6502_N)); break;
    case 0x30: branch(c, (c->p & M6502_N) != 0); break;
    case 0x50: branch(c, !(c->p & M6502_V)); break;
    case 0x70: branch(c, (c->p & M6502_V) != 0); break;
    case 0x90: branch(c, !(c->p & M6502_C)); break;
    case 0xB0: branch(c, (c->p & M6502_C) != 0); break;
    case 0xD0: branch(c, !(c->p & M6502_Z)); break;
    case 0xF0: branch(c, (c->p & M6502_Z) != 0); break;

    // -------------------------------------------------------------- saltos
    case 0x4C: c->pc = am_abs(c); break;


    case 0x6C: {
        uint8_t lo = fetch(c);
        uint8_t hi = fetch(c);
        uint16_t ptr = (uint16_t)(lo | (hi << 8));
        uint8_t dlo = rd(c, ptr);
        // bug do NMOS: o ponteiro não atravessa a página
        uint8_t dhi = rd(c, (uint16_t)((ptr & 0xFF00) | ((ptr + 1) & 0x00FF)));
        c->pc = (uint16_t)(dlo | (dhi << 8));
    } break;

    case 0x20: {                       // JSR
        uint8_t lo = fetch(c);
        rd(c, (uint16_t)(0x0100 | c->s));
        push(c, (uint8_t)(c->pc >> 8));
        push(c, (uint8_t)(c->pc & 0xFF));
        uint8_t hi = fetch(c);
        c->pc = (uint16_t)(lo | (hi << 8));
    } break;

    case 0x60: {                       // RTS
        rd(c, c->pc);
        rd(c, (uint16_t)(0x0100 | c->s));
        uint8_t lo = pop(c);
        uint8_t hi = pop(c);
        c->pc = (uint16_t)(lo | (hi << 8));
        rd(c, c->pc);
        c->pc++;
    } break;

    case 0x40: {                       // RTI
        rd(c, c->pc);
        rd(c, (uint16_t)(0x0100 | c->s));
        c->p = (uint8_t)((pop(c) & ~M6502_B) | M6502_U);
        uint8_t lo = pop(c);
        uint8_t hi = pop(c);
        c->pc = (uint16_t)(lo | (hi << 8));
    } break;

    case 0x00: service_interrupt(c, 0xFFFE, true); break;

    // --------------------------------------------------------------- flags
    case 0x18: rd(c, c->pc); c->p &= (uint8_t)~M6502_C; break;
    case 0x38: rd(c, c->pc); c->p |= M6502_C; break;
    case 0x58: rd(c, c->pc); c->p &= (uint8_t)~M6502_I; break;
    case 0x78: rd(c, c->pc); c->p |= M6502_I; break;
    case 0xB8: rd(c, c->pc); c->p &= (uint8_t)~M6502_V; break;
    case 0xD8: rd(c, c->pc); c->p &= (uint8_t)~M6502_D; break;
    case 0xF8: rd(c, c->pc); c->p |= M6502_D; break;

    case 0xEA: rd(c, c->pc); break;    // NOP

    // ------------------------------------------------- ilegais: NOP com modo
    case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA:
        rd(c, c->pc); break;
    case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2:
        fetch(c); break;
    case 0x04: case 0x44: case 0x64:
        rd(c, am_zp(c)); break;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4:
        rd(c, am_zpx(c)); break;
    case 0x0C:
        rd(c, am_abs(c)); break;
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
        rd(c, am_abi_r(c, c->x)); break;

    // ------------------------------------------------------ ilegais estáveis
    // LAX = LDA + LDX
    case 0xA7: c->a = c->x = set_nz(c, rd(c, am_zp(c))); break;
    case 0xB7: c->a = c->x = set_nz(c, rd(c, am_zpy(c))); break;
    case 0xAF: c->a = c->x = set_nz(c, rd(c, am_abs(c))); break;
    case 0xBF: c->a = c->x = set_nz(c, rd(c, am_abi_r(c, c->y))); break;
    case 0xA3: c->a = c->x = set_nz(c, rd(c, am_izx(c))); break;
    case 0xB3: c->a = c->x = set_nz(c, rd(c, am_izy_r(c))); break;

    // SAX = grava A & X
    case 0x87: wr(c, am_zp(c), (uint8_t)(c->a & c->x)); break;
    case 0x97: wr(c, am_zpy(c), (uint8_t)(c->a & c->x)); break;
    case 0x8F: wr(c, am_abs(c), (uint8_t)(c->a & c->x)); break;
    case 0x83: wr(c, am_izx(c), (uint8_t)(c->a & c->x)); break;

    // SLO = ASL + ORA
    case 0x07: RMW2(am_zp(c), op_asl, c->a = set_nz(c, c->a | _v)); break;
    case 0x17: RMW2(am_zpx(c), op_asl, c->a = set_nz(c, c->a | _v)); break;
    case 0x0F: RMW2(am_abs(c), op_asl, c->a = set_nz(c, c->a | _v)); break;
    case 0x1F: RMW2(am_abi_w(c, c->x), op_asl, c->a = set_nz(c, c->a | _v)); break;
    case 0x1B: RMW2(am_abi_w(c, c->y), op_asl, c->a = set_nz(c, c->a | _v)); break;
    case 0x03: RMW2(am_izx(c), op_asl, c->a = set_nz(c, c->a | _v)); break;
    case 0x13: RMW2(am_izy_w(c), op_asl, c->a = set_nz(c, c->a | _v)); break;

    // RLA = ROL + AND
    case 0x27: RMW2(am_zp(c), op_rol, c->a = set_nz(c, c->a & _v)); break;
    case 0x37: RMW2(am_zpx(c), op_rol, c->a = set_nz(c, c->a & _v)); break;
    case 0x2F: RMW2(am_abs(c), op_rol, c->a = set_nz(c, c->a & _v)); break;
    case 0x3F: RMW2(am_abi_w(c, c->x), op_rol, c->a = set_nz(c, c->a & _v)); break;
    case 0x3B: RMW2(am_abi_w(c, c->y), op_rol, c->a = set_nz(c, c->a & _v)); break;
    case 0x23: RMW2(am_izx(c), op_rol, c->a = set_nz(c, c->a & _v)); break;
    case 0x33: RMW2(am_izy_w(c), op_rol, c->a = set_nz(c, c->a & _v)); break;

    // SRE = LSR + EOR
    case 0x47: RMW2(am_zp(c), op_lsr, c->a = set_nz(c, c->a ^ _v)); break;
    case 0x57: RMW2(am_zpx(c), op_lsr, c->a = set_nz(c, c->a ^ _v)); break;
    case 0x4F: RMW2(am_abs(c), op_lsr, c->a = set_nz(c, c->a ^ _v)); break;
    case 0x5F: RMW2(am_abi_w(c, c->x), op_lsr, c->a = set_nz(c, c->a ^ _v)); break;
    case 0x5B: RMW2(am_abi_w(c, c->y), op_lsr, c->a = set_nz(c, c->a ^ _v)); break;
    case 0x43: RMW2(am_izx(c), op_lsr, c->a = set_nz(c, c->a ^ _v)); break;
    case 0x53: RMW2(am_izy_w(c), op_lsr, c->a = set_nz(c, c->a ^ _v)); break;

    // RRA = ROR + ADC
    case 0x67: RMW2(am_zp(c), op_ror, op_adc(c, _v)); break;
    case 0x77: RMW2(am_zpx(c), op_ror, op_adc(c, _v)); break;
    case 0x6F: RMW2(am_abs(c), op_ror, op_adc(c, _v)); break;
    case 0x7F: RMW2(am_abi_w(c, c->x), op_ror, op_adc(c, _v)); break;
    case 0x7B: RMW2(am_abi_w(c, c->y), op_ror, op_adc(c, _v)); break;
    case 0x63: RMW2(am_izx(c), op_ror, op_adc(c, _v)); break;
    case 0x73: RMW2(am_izy_w(c), op_ror, op_adc(c, _v)); break;

    // DCP = DEC + CMP
    case 0xC7: RMW2(am_zp(c), op_dec, op_cmp_reg(c, c->a, _v)); break;
    case 0xD7: RMW2(am_zpx(c), op_dec, op_cmp_reg(c, c->a, _v)); break;
    case 0xCF: RMW2(am_abs(c), op_dec, op_cmp_reg(c, c->a, _v)); break;
    case 0xDF: RMW2(am_abi_w(c, c->x), op_dec, op_cmp_reg(c, c->a, _v)); break;
    case 0xDB: RMW2(am_abi_w(c, c->y), op_dec, op_cmp_reg(c, c->a, _v)); break;
    case 0xC3: RMW2(am_izx(c), op_dec, op_cmp_reg(c, c->a, _v)); break;
    case 0xD3: RMW2(am_izy_w(c), op_dec, op_cmp_reg(c, c->a, _v)); break;

    // ISC = INC + SBC
    case 0xE7: RMW2(am_zp(c), op_inc, op_sbc(c, _v)); break;
    case 0xF7: RMW2(am_zpx(c), op_inc, op_sbc(c, _v)); break;
    case 0xEF: RMW2(am_abs(c), op_inc, op_sbc(c, _v)); break;
    case 0xFF: RMW2(am_abi_w(c, c->x), op_inc, op_sbc(c, _v)); break;
    case 0xFB: RMW2(am_abi_w(c, c->y), op_inc, op_sbc(c, _v)); break;
    case 0xE3: RMW2(am_izx(c), op_inc, op_sbc(c, _v)); break;
    case 0xF3: RMW2(am_izy_w(c), op_inc, op_sbc(c, _v)); break;

    // ANC: AND imediato, e o bit 7 do resultado vai para o carry
    case 0x0B: case 0x2B:
        c->a = set_nz(c, c->a & fetch(c));
        set_flag(c, M6502_C, (c->a & 0x80) != 0);
        break;

    // ALR: AND imediato + LSR
    case 0x4B:
        c->a = set_nz(c, c->a & fetch(c));
        c->a = op_lsr(c, c->a);
        break;

    // ARR: AND imediato + ROR, com flags próprias
    case 0x6B: {
        uint8_t m = fetch(c);
        uint8_t t = (uint8_t)(c->a & m);
        if (c->p & M6502_D) {
            uint8_t r = (uint8_t)((t >> 1) | ((c->p & M6502_C) ? 0x80 : 0));
            set_nz(c, r);
            set_flag(c, M6502_V, ((r ^ t) & 0x40) != 0);
            uint8_t lo = (uint8_t)(t & 0x0F);
            if (lo + (lo & 1) > 5) r = (uint8_t)((r & 0xF0) | ((r + 6) & 0x0F));
            set_flag(c, M6502_C, ((unsigned)(t & 0xF0) + (t & 0x10)) > 0x50);
            if (c->p & M6502_C) r = (uint8_t)(r + 0x60);
            c->a = r;
        } else {
            uint8_t r = (uint8_t)((t >> 1) | ((c->p & M6502_C) ? 0x80 : 0));
            c->a = set_nz(c, r);
            set_flag(c, M6502_C, (r & 0x40) != 0);
            set_flag(c, M6502_V, (((r >> 6) ^ (r >> 5)) & 1) != 0);
        }
    } break;

    // SBX/AXS: X = (A & X) - imediato, com carry como comparação
    case 0xCB: {
        uint8_t m = fetch(c);
        uint8_t t = (uint8_t)(c->a & c->x);
        set_flag(c, M6502_C, t >= m);
        c->x = set_nz(c, (uint8_t)(t - m));
    } break;

    // ----------------------------------------------------- ilegais instáveis
    case 0x8B: {   // ANE/XAA
        uint8_t m = fetch(c);
        c->a = set_nz(c, (uint8_t)((c->a | 0xEE) & c->x & m));
    } break;

    case 0xAB: {   // LXA/ATX
        uint8_t m = fetch(c);
        c->a = c->x = set_nz(c, (uint8_t)((c->a | 0xEE) & m));
    } break;

    case 0x9F: {   // SHA abs,Y
        uint8_t lo = fetch(c), hi = fetch(c);
        rd(c, (uint16_t)((hi << 8) | (uint8_t)(lo + c->y)));
        store_high(c, lo, hi, c->y, (uint8_t)(c->a & c->x));
    } break;

    case 0x93: {   // SHA (zp),Y
        uint8_t zp = fetch(c);
        uint8_t lo = rd(c, zp);
        uint8_t hi = rd(c, (uint8_t)(zp + 1));
        rd(c, (uint16_t)((hi << 8) | (uint8_t)(lo + c->y)));
        store_high(c, lo, hi, c->y, (uint8_t)(c->a & c->x));
    } break;

    case 0x9E: {   // SHX abs,Y
        uint8_t lo = fetch(c), hi = fetch(c);
        rd(c, (uint16_t)((hi << 8) | (uint8_t)(lo + c->y)));
        store_high(c, lo, hi, c->y, c->x);
    } break;

    case 0x9C: {   // SHY abs,X
        uint8_t lo = fetch(c), hi = fetch(c);
        rd(c, (uint16_t)((hi << 8) | (uint8_t)(lo + c->x)));
        store_high(c, lo, hi, c->x, c->y);
    } break;

    case 0x9B: {   // TAS/SHS abs,Y
        uint8_t lo = fetch(c), hi = fetch(c);
        rd(c, (uint16_t)((hi << 8) | (uint8_t)(lo + c->y)));
        c->s = (uint8_t)(c->a & c->x);
        store_high(c, lo, hi, c->y, c->s);
    } break;

    case 0xBB: {   // LAS abs,Y
        uint8_t v = rd(c, am_abi_r(c, c->y));
        c->a = c->x = c->s = set_nz(c, (uint8_t)(v & c->s));
    } break;

    // ------------------------------------------------------------ JAM/KIL
    // O processador trava com o barramento oscilando entre os vetores. O
    // padrão abaixo é o que o silício faz e o que os vetores de teste medem.
    case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52:
    case 0x62: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:
        rd(c, c->pc);
        rd(c, 0xFFFF);
        rd(c, 0xFFFE);
        rd(c, 0xFFFE);
        for (int i = 0; i < 6; ++i)
            rd(c, 0xFFFF);
        c->jammed = true;
        break;

    default:
        // Não deveria acontecer: os 256 opcodes estão cobertos.
        rd(c, c->pc);
        break;
    }

    c->p |= M6502_U;
}
