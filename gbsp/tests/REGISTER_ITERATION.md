# Experimento F — percorrer apenas registradores selecionados

Base de código: `8b4c13ec7`. Base de comportamento/performance: C.
Build F: fast_ram=1, compact_reads=0, memstats=0, sparse_regs=1.
A integração do launcher permanece intacta.

## Hipótese

Os laços de LDM/STM ARM testam 16 posições; os de transferências Thumb testam
8 posições antes de tratar PC/LR separadamente. O candidato percorre somente
os bits ativos da máscara, em ordem crescente, usando ctz e limpeza do bit
mais baixo. Isso evita posições vazias e pode impedir expansão excessiva do
código de transferências Thumb pelo compilador.

A consulta de mapa, handlers e contagem de ciclos de cada transferência são
preservados. Nenhuma página é mantida em cache entre acessos. Não muda cálculo
de endereço, writeback, tratamento de PC/LR, troca de banco ou casos vazios.
`ctz(0)` é evitado pelo curto-circuito na condição do laço.

A opção `CONFIG_GBSP_SPARSE_REGISTERS` é desligada por padrão, e não é ativada
com dynarec. Sem a opção, o laço percorre as mesmas 16/8 posições do original.
No S3, uma sonda compilada mostra ctz como neg/and/nsau/movi/sub, sem chamada
auxiliar. Isso custa instruções por registrador ativo: listas densas podem
não melhorar. Não se conhece ainda a distribuição de máscaras dos jogos.
D1 não separa acessos escalares e múltiplos nem mede tempo por categoria.

## Validação

`python3 gbsp/tests/test_register_list.py`:

- Verifica todas as 65.536 máscaras, em larguras 8 e 16: índices, ordem e
  conjunto exato de bits, inclusive máscara vazia.
- Extrai os templates de transferência reais da variante C e do fonte atual.
- Compara 148.160 transferências por configuração: ligada, desligada e dynarec,
  total 444.480. Cobre todos os modos de endereço, load/store, writeback e S,
  registrador-base na lista, PC/LR, fronteiras e wrap de endereço.
- Compara estado dos registradores, sequência de endereços/valores/leituras e
  escritas, ciclos, alertas e sequência de trocas de modo.
- ASan/UBSan ativos. Os helpers de memória e troca de modo são spies; a
  comparação verifica a sequência preservada, não valida toda a emulação GBA.

O teste diferencial de escritas C continua passando. A semântica preexistente
é a referência; não se pretende corrigir outros defeitos do interpretador.

## Coleta futura, sem urgência

Firmware e logs de build em `../outputs/gbsp-register-iteration/`.
Quando houver tempo, C → F → C nas mesmas cenas estáticas de Fire Red e Mario,
oc 0, frameskip fixo 5, cache 32 KB, PSRAM 80 MHz, 60 s de aquecimento + 120 s
de coleta por jogo. Arquivo `/retro-go/gbsp-perfil.txt`; guardar completos como
`F-base-C1.txt`, `F1-perfil.txt`, `F-base-C2.txt`. Não copiar gbsp-memory.txt:
F não gera esse arquivo, e o existente é de D.

Verificar `fast_ram 1 (mapped-v2) | memstats 0 | compact_reads 0 | sparse_regs 1`
e IWRAM interna no cabeçalho F. Sem controle recente, qualquer comparação com
C histórico será preliminar. Não há ganho de velocidade comprovado antes da
medição no aparelho.

## Resultado da compilação

Build ESP32-S3 concluído sem warnings nos arquivos alterados. execute_arm:
51.010 bytes em C, 50.686 em F (-324). IRAM total com vetores e padding:
119.039 em C, 118.783 em F (-256 bytes). Não há nova tabela nem contadores.
O disassembly contém 38 ocorrências estáticas de nsau no interpretador,
confirmando a seleção de bits por instrução nativa no código efetivo. Isso
não mede quantas vezes o caminho executa ou seu desempenho real.

F ainda não foi executado no aparelho. Não há ganho de velocidade comprovado.
C continua sendo a versão preferida; E não faz parte deste build.
