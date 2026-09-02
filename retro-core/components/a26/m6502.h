// m6502.h — núcleo 6502/6507 dirigido por ciclo de barramento
//
// Todo ciclo da CPU faz exatamente um acesso ao barramento — é assim que o
// 6502 real funciona, ciclos "internos" inclusive. Por isso o núcleo não
// precisa de máquina de estados por ciclo: basta que cada acesso passe pelos
// callbacks read/write, e quem os implementa avança o resto do sistema.
//
// No Atari 2600 isso é o que dá exatidão: 1 ciclo de CPU = 3 color clocks da
// TIA, então o callback adianta a TIA em 3 antes de responder. A TIA reage no
// instante certo de cada escrita, que é o que faz a imagem sair correta.
//
// Licença: GPLv2 (mesma do retro-go).

#ifndef M6502_H
#define M6502_H

#include <stdint.h>
#include <stdbool.h>

// Bits do registrador de status
#define M6502_C 0x01  // carry
#define M6502_Z 0x02  // zero
#define M6502_I 0x04  // interrupt disable
#define M6502_D 0x08  // decimal
#define M6502_B 0x10  // break (não é bit real; só existe no valor empilhado)
#define M6502_U 0x20  // unused (lê sempre como 1)
#define M6502_V 0x40  // overflow
#define M6502_N 0x80  // negative

typedef struct m6502_s m6502_t;

struct m6502_s {
    uint16_t pc;
    uint8_t a, x, y, s, p;

    // Barramento. Um acesso = um ciclo de CPU.
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t val);

    // Linhas de interrupção (o 6507 do 2600 não tem nenhuma ligada, mas o
    // núcleo é um 6502 completo para poder ser validado contra os testes)
    bool nmi_pending;
    bool irq_line;

    bool jammed;  // atingiu um opcode JAM/KIL
};

// Reinicia lendo o vetor em 0xFFFC (consome 7 ciclos, como o real).
void m6502_reset(m6502_t *cpu);

// Executa uma instrução completa (ou o atendimento de uma interrupção
// pendente). Cada ciclo gasto vira uma chamada a read() ou write().
void m6502_step(m6502_t *cpu);

#endif
