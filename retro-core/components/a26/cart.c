// cart.c — esquemas de troca de banco do cartucho do Atari 2600.
// Licença: GPLv2 (mesma do retro-go).
//
// Uma observação que vale para o arquivo inteiro: o hotspot dispara no
// **acesso**, não na escrita. O cartucho não vê a linha R/W do jeito que um
// registrador veria; ele decodifica endereço. Por isso `cart_read` e
// `cart_write` chamam o mesmo `hotspot()`, e por isso uma leitura de $1FF9
// troca de banco. Emuladores que só tratam a escrita quebram jogos que trocam
// de banco com LDA — que são muitos, porque LDA é mais barato.
//
// A outra: num hotspot de leitura, o byte devolvido vem do banco **novo**. A
// troca acontece na decodificação do endereço, antes de o dado sair da ROM.

#include <string.h>
#include "cart.h"

#define JANELA   0x1000          // tamanho da janela do cartucho
#define MASCARA  0x0FFF          // deslocamento dentro dela

// ------------------------------------------------------------------ tabela
//
// Um esquema de banco simples é só: onde começa o primeiro hotspot, quantos
// são, e qual o tamanho do banco. F8, F6, F4 e FA cabem todos aqui.
//
typedef struct {
    uint16_t hotspot0;           // endereço do hotspot do banco 0
    uint8_t  n_hotspots;         // quantos hotspots consecutivos
    uint16_t tam_banco;
} simples_t;

static const simples_t SIMPLES[CART_TIPOS] = {
    [CART_F8] = { 0x1FF8, 2, 0x1000 },
    [CART_F6] = { 0x1FF6, 4, 0x1000 },
    [CART_F4] = { 0x1FF4, 8, 0x1000 },
    [CART_FA] = { 0x1FF8, 3, 0x1000 },
};

static bool eh_simples(cart_tipo_t t)
{
    return t == CART_F8 || t == CART_F6 || t == CART_F4 || t == CART_FA;
}

const char *cart_nome(cart_tipo_t tipo)
{
    static const char *N[CART_TIPOS] = {
        "2K", "4K", "F8", "F6", "F4", "FA", "E0", "FE", "3F"
    };
    return (tipo >= 0 && tipo < CART_TIPOS) ? N[tipo] : "?";
}

int cart_bancos(const cart_t *c)
{
    switch (c->tipo) {
    case CART_2K: case CART_4K: return 1;
    case CART_E0: return 8;                       // 8 fatias de 1 KB
    case CART_FE: return 2;
    case CART_3F: return (int)(c->tam / 0x800);   // bancos de 2 KB
    default:      return SIMPLES[c->tipo].n_hotspots;
    }
}

// ------------------------------------------------------------------- RAM
//
// Superchip: 128 bytes. A metade de baixo da janela é a porta de escrita
// ($1000-$107F) e a de cima a de leitura ($1080-$10FF). FA (CBS RAM Plus) é a
// mesma ideia com 256 bytes: escrita em $1000-$10FF, leitura em $1100-$11FF.
//
// A separação existe porque o cartucho não enxerga a linha R/W: escrever e ler
// no mesmo endereço seria ambíguo, então usaram dois endereços.
//
static int ram_tam(const cart_t *c)
{
    if (c->tipo == CART_FA) return 256;
    return c->superchip ? 128 : 0;
}

// Devolve o índice na RAM, ou -1 se o endereço não é da RAM.
// `leitura` escolhe qual das duas portas.
static int ram_idx(const cart_t *c, uint16_t a, bool leitura)
{
    int n = ram_tam(c);
    if (n == 0)
        return -1;
    uint16_t base = leitura ? (uint16_t)n : 0;
    uint16_t off = (uint16_t)(a & MASCARA);
    if (off < base || off >= base + n)
        return -1;
    return off - base;
}

// --------------------------------------------------------------- hotspots

// Trata o hotspot do endereço, se houver. Chamado por leitura e por escrita.
static void hotspot(cart_t *c, uint16_t a)
{
    a &= 0x1FFF;

    if (eh_simples(c->tipo)) {
        const simples_t *s = &SIMPLES[c->tipo];
        if (a >= s->hotspot0 && a < s->hotspot0 + s->n_hotspots)
            c->banco = (uint8_t)(a - s->hotspot0);
        return;
    }

    if (c->tipo == CART_E0) {
        // Três janelas de 1 KB são móveis; a quarta é fixa na última fatia.
        //   $1FE0-$1FE7 -> janela 0     $1FE8-$1FEF -> janela 1
        //   $1FF0-$1FF7 -> janela 2     (janela 3 não tem hotspot)
        if (a >= 0x1FE0 && a <= 0x1FE7)      c->fatia[0] = (uint8_t)(a - 0x1FE0);
        else if (a >= 0x1FE8 && a <= 0x1FEF) c->fatia[1] = (uint8_t)(a - 0x1FE8);
        else if (a >= 0x1FF0 && a <= 0x1FF7) c->fatia[2] = (uint8_t)(a - 0x1FF0);
        return;
    }
    // 2K, 4K, FE e 3F não têm hotspot dentro da janela.
}

// ------------------------------------------------------------ mapeamento
//
// Onde, dentro da ROM, está o byte que aparece no endereço `a` da janela.
// Função pura do estado do cartucho — é ela que a suíte de teste compara com
// um modelo independente.
//
// A implementação fica estática para o compilador embuti-la em `cart_read`:
// são 6,6 milhões de chamadas por segundo de jogo, e o prólogo aparecia no
// perfil. `cart_offset` continua exportada para o teste comparar o mapeamento
// com o modelo independente.
static size_t offset_interno(const cart_t *c, uint16_t a)
{
    uint16_t off = (uint16_t)(a & MASCARA);

    switch (c->tipo) {
    case CART_2K:
        return off & 0x7FF;                        // 2 KB espelhado

    case CART_4K:
        return off;

    case CART_E0: {
        // Quatro janelas de 1 KB, todas lidas do mesmo vetor de estado. A
        // quarta não tem hotspot: `fatia[3]` é posta em 7 no reset e ninguém
        // mais mexe nela — é lá que ficam os vetores. Escrever isso como
        // `(janela == 3) ? 7 : ...` também funcionaria, mas deixaria
        // `fatia[3]` como estado morto, que nenhum teste consegue alcançar.
        int janela = off >> 10;
        return (size_t)c->fatia[janela] * 0x400 + (off & 0x3FF);
    }

    case CART_3F:
        // Metade de baixo móvel, metade de cima presa no último banco de 2 KB.
        if (off < 0x800)
            return (size_t)c->banco * 0x800 + off;
        return c->tam - 0x800 + (off - 0x800);

    default:
        // F8, F6, F4, FA, FE: banco inteiro de 4 KB.
        return (size_t)c->banco * 0x1000 + off;
    }
}

size_t cart_offset(const cart_t *c, uint16_t a)
{
    return offset_interno(c, a);
}

// -------------------------------------------------------------- interface

uint8_t cart_read(cart_t *c, uint16_t addr)
{
    hotspot(c, addr);

    int i = ram_idx(c, addr, true);
    if (i >= 0)
        return c->ram[i];

    size_t p = offset_interno(c, addr);
    return (p < c->tam) ? c->rom[p] : 0xFF;
}

void cart_write(cart_t *c, uint16_t addr, uint8_t val)
{
    hotspot(c, addr);

    int i = ram_idx(c, addr, false);
    if (i >= 0)
        c->ram[i] = val;
    // Fora da RAM, escrever na janela do cartucho não faz nada: é ROM.
}

void cart_snoop(cart_t *c, uint16_t addr, uint8_t dado, bool escrita)
{
    uint16_t a = addr & 0x1FFF;

    if (c->tipo == CART_3F) {
        // Uma escrita em $00-$3F escolhe o banco de baixo. O endereço é da
        // TIA — o cartucho está literalmente escutando escritas que não são
        // para ele. Bancos além do que a ROM tem dão a volta.
        if (escrita && a <= 0x003F) {
            int n = cart_bancos(c);
            c->banco = (uint8_t)(n > 0 ? (dado % n) : 0);
        }
        return;
    }

    if (c->tipo == CART_FE) {
        // Sem hotspot. Durante o JSR/RTS a CPU toca $01FE, e o byte que passa
        // no barramento no acesso seguinte é o byte alto do endereço de
        // retorno. Bit 5 ligado quer dizer $Fxxx (banco 0); apagado, $Dxxx
        // (banco 1). É o esquema mais estranho dos dez, e o único que depende
        // de a CPU expor os acessos internos à pilha — o que o nosso 6507
        // faz, porque é dirigido por ciclo de barramento.
        if (c->ultimo_end == 0x01FE)
            c->banco = (dado & 0x20) ? 0 : 1;
        c->ultimo_end = a;
        return;
    }
}

// -------------------------------------------------------------- detecção
//
// A detecção é heurística e é assim em todo emulador de 2600: o formato .bin
// não guarda o esquema, só os bytes. O tamanho resolve a maioria; para 8 KB é
// preciso procurar a assinatura do código que faz a troca.
//
static int conta(const uint8_t *rom, size_t tam, const uint8_t *seq, size_t n)
{
    int k = 0;
    if (tam < n)
        return 0;
    for (size_t i = 0; i + n <= tam; ++i)
        if (memcmp(rom + i, seq, n) == 0)
            k++;
    return k;
}

static bool contem(const uint8_t *rom, size_t tam, const uint8_t *seq, size_t n)
{
    if (tam < n)
        return false;
    for (size_t i = 0; i + n <= tam; ++i)
        if (memcmp(rom + i, seq, n) == 0)
            return true;
    return false;
}

// Superchip: os primeiros 128 bytes de cada banco de 4 KB são a porta de
// escrita da RAM, então o jogo nunca põe código útil lá. Na prática eles saem
// preenchidos com um byte só. Se todos os bancos forem assim, é SC.
static bool parece_superchip(const uint8_t *rom, size_t tam)
{
    if (tam % 0x1000 != 0 || tam < 0x2000)
        return false;
    for (size_t b = 0; b < tam; b += 0x1000) {
        uint8_t primeiro = rom[b];
        for (int i = 1; i < 128; ++i)
            if (rom[b + i] != primeiro)
                return false;
    }
    return true;
}

// O menor pedaço que, repetido, dá o arquivo inteiro. Dumps de ROM de 2 KB e
// de 4 KB aparecem por aí preenchidos até 8, 16 ou 32 KB com cópias de si
// mesmos; sem isto, uma River Raid de 4 KB empacotada em 16 KB é detectada
// como F6 e passa a ter hotspots que o jogo não espera.
static size_t unidade_repetida(const uint8_t *rom, size_t tam)
{
    for (size_t u = 2048; u < tam; u *= 2) {
        if (tam % u != 0)
            continue;
        bool igual = true;
        for (size_t i = u; i < tam && igual; i += u)
            if (memcmp(rom, rom + i, u) != 0)
                igual = false;
        if (igual)
            return u;
    }
    return tam;
}

cart_tipo_t cart_detecta(const uint8_t *rom, size_t tam, bool *superchip)
{
    if (superchip)
        *superchip = false;

    tam = unidade_repetida(rom, tam);

    if (tam == 2048)
        return CART_2K;
    if (tam == 4096)
        return CART_4K;
    if (tam == 12288)
        return CART_FA;

    if (tam == 8192) {
        // A ordem importa: as heurísticas se sobrepõem. Decathlon, por
        // exemplo, tem um `lda $FFF1,x` que cairia numa regra ingênua de
        // "qualquer acesso em $1FE0-$1FF7 é E0" — mas não é E0, é FE.

        // E0: o jogo escreve ou lê nos endereços que movem as janelas. Os
        // espelhos importam: Frogger II usa `sta $FFE9`, não `sta $1FE9`, e a
        // primeira versão desta lista não tinha esse caso — o jogo era
        // detectado como F8 e não passava da primeira tela.
        static const uint8_t E0[][3] = {
            { 0x8D, 0xE0, 0x1F },   // sta $1FE0
            { 0x8D, 0xE0, 0x5F },   // sta $5FE0
            { 0x8D, 0xE9, 0xFF },   // sta $FFE9   <- Frogger II
            { 0x0C, 0xE0, 0xFF },   // nop $FFE0
            { 0xAD, 0xE0, 0x1F },   // lda $1FE0
            { 0xAD, 0xE9, 0xFF },   // lda $FFE9
            { 0xAD, 0xED, 0xFF },   // lda $FFED
            { 0xAD, 0xF3, 0xBF },   // lda $BFF3
        };
        for (size_t i = 0; i < sizeof(E0) / sizeof(E0[0]); ++i)
            if (contem(rom, tam, E0[i], 3))
                return CART_E0;

        // FE: o esquema depende de JSR/JMP para $Dxxx.
        static const uint8_t FE[][3] = {
            { 0x20, 0x00, 0xD0 },   // jsr $D000   <- Decathlon
            { 0x8D, 0x00, 0xD0 },   // sta $D000
            { 0x20, 0xC3, 0xF8 },   // jsr $F8C3
        };
        for (size_t i = 0; i < sizeof(FE) / sizeof(FE[0]); ++i)
            if (contem(rom, tam, FE[i], 3))
                return CART_FE;

        // 3F: o banco é escolhido por `sta $3F`, e $3F não serve para mais
        // nada — é espaço morto da TIA. Duas ocorrências já bastam; River
        // Patrol tem vinte. Faltava esta checagem: o 3F só era considerado
        // para tamanhos fora do comum, e todo Tigervision tem 8 KB.
        if (conta(rom, tam, (const uint8_t[]){ 0x85, 0x3F }, 2) >= 2)
            return CART_3F;

        if (superchip)
            *superchip = parece_superchip(rom, tam);
        return CART_F8;
    }

    if (tam == 16384) {
        if (superchip)
            *superchip = parece_superchip(rom, tam);
        return CART_F6;
    }
    if (tam == 32768) {
        if (superchip)
            *superchip = parece_superchip(rom, tam);
        return CART_F4;
    }

    // 3F aceita tamanhos que não são potência de dois dentro da janela, porque
    // o banco é de 2 KB. É o único esquema que sobra para eles.
    if (tam >= 0x1000 && tam % 0x800 == 0)
        return CART_3F;

    return CART_4K;                                // último recurso
}

// ------------------------------------------------------------ carregamento

bool cart_load_tipo(cart_t *c, const uint8_t *rom, size_t tam,
                    cart_tipo_t tipo, bool superchip)
{
    if (!rom || tam == 0 || tipo < 0 || tipo >= CART_TIPOS)
        return false;

    memset(c, 0, sizeof(*c));
    c->rom = rom;
    c->tam = tam;
    c->tipo = tipo;
    c->superchip = superchip && (tipo == CART_F8 || tipo == CART_F6 ||
                                 tipo == CART_F4);
    // Oito dos dez esquemas ignoram o barramento. Marcar aqui evita uma chamada
    // de função em cada acesso da CPU — sao 8,5 milhoes por segundo de jogo.
    c->escuta = (tipo == CART_3F || tipo == CART_FE);
    cart_reset(c);
    return true;
}

bool cart_load(cart_t *c, const uint8_t *rom, size_t tam)
{
    bool sc = false;
    cart_tipo_t t = cart_detecta(rom, tam, &sc);
    return cart_load_tipo(c, rom, tam, t, sc);
}

// O vetor de reset de um banco aponta para dentro da janela do cartucho?
// Só os 13 bits baixos contam, e A12 tem de estar ligado: qualquer outra coisa
// é a CPU sendo mandada para a TIA ou para a RAM, o que nenhum jogo faz.
static bool vetor_plausivel(const cart_t *c, int banco)
{
    size_t p = (size_t)banco * 0x1000 + 0x0FFC;
    if (p + 1 >= c->tam)
        return false;
    uint16_t v = (uint16_t)(c->rom[p] | (c->rom[p + 1] << 8));
    return (v & 0x1000) != 0;
}

void cart_reset(cart_t *c)
{
    memset(c->ram, 0, sizeof(c->ram));
    c->ultimo_end = 0;

    // Qual banco vale no reset? Em vez de convencionar, dá para **deduzir**:
    // o banco que acorda é o que tem um vetor de reset utilizável em
    // $FFFC-$FFFD. Procura do último para o primeiro, porque é onde a maioria
    // dos cartuchos põe o código de partida.
    //
    // Isto não é preciosismo. O Decathlon (esquema FE) tem $F000 no banco 0 e
    // lixo ($CC4E) no banco 1; com a convenção "sempre o último banco" ele
    // acordava executando lixo. A regra derivada acerta sozinha.
    int n = cart_bancos(c);
    c->banco = (uint8_t)(n > 0 ? n - 1 : 0);
    for (int b = n - 1; b >= 0; --b) {
        if (vetor_plausivel(c, b)) {
            c->banco = (uint8_t)b;
            break;
        }
    }

    if (c->tipo == CART_3F)
        c->banco = 0;                    // a metade de cima já é o último

    // E0: as três janelas móveis começam indefinidas no hardware; zerá-las é
    // arbitrário, mas é determinístico, e todo jogo E0 escolhe as fatias antes
    // de usá-las. A quarta janela é fixa e não depende disto.
    c->fatia[0] = c->fatia[1] = c->fatia[2] = 0;
    c->fatia[3] = 7;
}
