// cart.h — o cartucho do Atari 2600 e seus esquemas de troca de banco.
//
// O 6507 tem 13 linhas de endereço. Com A12 ligado, quem responde é o
// cartucho: $1000-$1FFF, 4 KB de janela. Como os jogos passaram de 4 KB, os
// fabricantes inventaram esquemas para pôr mais ROM atrás dessa janela — cada
// um com sua própria gambiarra, e nenhum deles com um registrador de verdade.
//
// O truque comum é o **hotspot**: um endereço que, ao ser *acessado* — lido ou
// escrito, tanto faz —, troca o banco como efeito colateral. Não existe "escrever
// no registrador de banco": o cartucho só enxerga o barramento de endereços.
// Por isso um `LDA $1FF9` troca de banco do mesmo jeito que um `STA $1FF9`, e
// por isso jogos evitam ler dados perto dos hotspots.
//
// Dois esquemas fogem disso e precisam ver o barramento inteiro, não só a
// janela do cartucho:
//
//   3F (Tigervision) — o banco é escolhido por uma **escrita em $00-$3F**, ou
//      seja, dentro da faixa da TIA. O cartucho fica escutando o barramento.
//   FE (Activision)  — não tem hotspot nenhum. O cartucho espia o byte que a
//      CPU empilha em $01FE durante o JSR e usa o bit 5 dele — que distingue
//      um endereço $Fxxx de um $Dxxx — para escolher o banco.
//
// Daí a API ter `cart_snoop`, chamado em todo acesso ao barramento.
//
// A ROM não é copiada: o cartucho guarda um ponteiro. Quem carrega é dono da
// memória e tem de mantê-la viva. Isso importa no ESP32, onde a ROM fica em
// flash mapeada e copiar 32 KB para a RAM seria um desperdício.
//
// Licença: GPLv2 (mesma do retro-go).

#ifndef CART_H
#define CART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    CART_2K,     //  2 KB, espelhado duas vezes na janela
    CART_4K,     //  4 KB, direto
    CART_F8,     //  8 KB, 2 bancos    — hotspots $1FF8-$1FF9
    CART_F6,     // 16 KB, 4 bancos    — hotspots $1FF6-$1FF9
    CART_F4,     // 32 KB, 8 bancos    — hotspots $1FF4-$1FFB
    CART_FA,     // 12 KB, 3 bancos    — hotspots $1FF8-$1FFA, + 256 B de RAM
    CART_E0,     //  8 KB em 8 fatias de 1 KB, 3 janelas móveis
    CART_FE,     //  8 KB, 2 bancos, escolhidos espiando a pilha
    CART_3F,     //  N x 2 KB na metade de baixo, escrita em $00-$3F
    CART_TIPOS
} cart_tipo_t;

#define CART_MAX_RAM 256

typedef struct {
    const uint8_t *rom;
    size_t   tam;
    cart_tipo_t tipo;
    bool     superchip;        // 128 B de RAM extra (F8SC, F6SC, F4SC)

    uint8_t  banco;            // banco corrente, nos esquemas de banco único
    uint8_t  fatia[4];         // E0: a fatia em cada uma das 4 janelas de 1 KB
    uint8_t  ram[CART_MAX_RAM];

    uint16_t ultimo_end;       // FE: endereço do acesso anterior
} cart_t;

// Carrega e escolhe o esquema. Devolve false se o tamanho não bate com nenhum.
// A detecção é heurística — ver cart_detecta() — e pode ser passada por cima
// chamando cart_load_tipo().
bool cart_load(cart_t *c, const uint8_t *rom, size_t tam);
bool cart_load_tipo(cart_t *c, const uint8_t *rom, size_t tam,
                    cart_tipo_t tipo, bool superchip);

// O esquema que a heurística escolheria, sem carregar nada.
cart_tipo_t cart_detecta(const uint8_t *rom, size_t tam, bool *superchip);

void cart_reset(cart_t *c);

// Acessos com A12 ligado. `addr` pode vir cru; só os 13 bits baixos contam.
uint8_t cart_read(cart_t *c, uint16_t addr);
void    cart_write(cart_t *c, uint16_t addr, uint8_t val);

// Todo acesso ao barramento, inclusive os que não são do cartucho. É por aqui
// que 3F e FE enxergam o que precisam. Chamar sempre, para qualquer endereço.
void cart_snoop(cart_t *c, uint16_t addr, uint8_t dado, bool escrita);

// Quantos bancos o esquema tem, para o tamanho carregado.
int cart_bancos(const cart_t *c);

// Onde, dentro da ROM, está o byte que aparece no endereço `addr` da janela.
// Função pura do estado — exposta para que o teste possa comparar o
// mapeamento diretamente com um modelo independente, em vez de comparar só os
// bytes lidos (onde dois offsets errados podem coincidir por acaso).
size_t cart_offset(const cart_t *c, uint16_t addr);

// Nome do esquema, para mensagens.
const char *cart_nome(cart_tipo_t tipo);

#endif
