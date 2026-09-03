// a26.h — o console inteiro: 6507 + RIOT + TIA + som + cartucho.
//
// Até aqui o projeto era cinco módulos independentes, cada um com seu próprio
// oráculo. Isso é bom para testar e ruim para usar: quem integra não deveria
// precisar saber que a TIA anda três color clocks por ciclo de CPU, nem que o
// cartucho escuta o barramento, nem como o console decodifica endereço.
//
// Esta camada é a fachada. Quem usa vê: carregue uma ROM, aponte para onde
// desenhar, rode um quadro, leia o som. É o que o retro-go precisa.
//
// Nada de novo acontece aqui — a decodificação de endereços é a mesma que o
// `tests/system.c` já usava para rodar as ROMs de teste. O que muda é que ela
// deixa de ser código de teste e passa a ser código do produto.
//
// Licença: GPLv2 (mesma do retro-go).

#ifndef A26_H
#define A26_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "m6502.h"
#include "riot.h"
#include "tia.h"
#include "cart.h"

// Botões, no formato que o console usa. Os quatro primeiros são a alavanca do
// jogador 1; o gatilho é separado porque no hardware ele nem passa pelo mesmo
// chip (direções vão para o RIOT, gatilho vai para a TIA).
#define A26_CIMA     0x0001
#define A26_BAIXO    0x0002
#define A26_ESQUERDA 0x0004
#define A26_DIREITA  0x0008
#define A26_GATILHO  0x0010

// Chaves do painel do console. RESET e SELECT são os dois botões que todo
// jogo usa para começar e para escolher a variação; sem eles, metade dos
// cartuchos não sai da tela de abertura.
#define A26_RESET    0x0020
#define A26_SELECT   0x0040
#define A26_DIFIC_P0 0x0080   // ligado = "A" (difícil)
#define A26_DIFIC_P1 0x0100
#define A26_PRETO_E_BRANCO 0x0200

typedef struct {
    m6502_t cpu;
    riot_t  riot;
    tia_t   tia;
    cart_t  cart;

    uint64_t ciclos;

    // Color clocks que a CPU já consumiu e a TIA ainda não desenhou.
    //
    // A TIA só precisa estar em dia quando alguém olha para ela: uma leitura
    // ou escrita nos registradores dela, o WSYNC soltando a CPU, ou a escrita
    // pendente que vale no color clock seguinte. Fora disso — código de lógica
    // rodando na RAM, tabelas sendo lidas da ROM — dá para acumular e desenhar
    // um trecho grande de uma vez, que é o que faz a varredura por trecho valer
    // a pena. Ver `sincroniza` em a26.c.
    int      pendente;

    // Som. A TIA produz duas amostras por linha de varredura — cerca de 31,4
    // kHz em NTSC. Elas são acumuladas aqui durante o quadro e reamostradas na
    // saída para a taxa que o aparelho usa.
    int16_t *audio;
    int      audio_cap;
    int      audio_len;
    uint8_t  audio_ultimo;    // para detectar as duas amostras por linha
    int      audio_clock_ant;

    uint32_t linhas_no_quadro;
} a26_t;

// Carrega uma ROM. Não copia: o ponteiro tem de continuar válido.
bool a26_load(a26_t *c, const uint8_t *rom, size_t tam);
void a26_reset(a26_t *c);

// Onde desenhar. Ver tia_set_framebuffer.
void a26_set_framebuffer(a26_t *c, uint8_t *fb, int stride, int linha0, int linhas);

// Onde acumular o som do quadro.
void a26_set_audio_buffer(a26_t *c, int16_t *buf, int capacidade);

// Estado dos botões e das chaves, bitmap de A26_*.
void a26_set_input(a26_t *c, uint16_t botoes);

// Roda até fechar um quadro. Devolve quantas amostras de som foram produzidas.
int a26_run_frame(a26_t *c);

// Nome do esquema de banco detectado, para a interface.
const char *a26_esquema(const a26_t *c);

#endif
