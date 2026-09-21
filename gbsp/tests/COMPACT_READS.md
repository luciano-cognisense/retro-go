# Experimento E — guarda compartilhada nas leituras

Base de integração: `1d324a1a1`, incluindo launcher aprovado e variante C.
A hipótese veio de D1: leituras representam 70,6% / 65,9% das operações de
dados de Fire Red / Mario nas cenas medidas. Frequência não demonstra custo.
D1 não distingue leituras escalares de múltiplas; esses percentuais não são
a fração coberta pela mudança, que afeta somente as escalares.

## Mudança

`CONFIG_GBSP_COMPACT_READS`, desligado por padrão e ignorado com dynarec,
substitui apenas a macro de leitura escalar `fast_read_memory`. O teste de
endereço < 0x10000000 protege tanto a tabela de ciclos quanto o mapa. Dentro
dele o teste de alinhamento usa só os bits baixos, sem repetir a rejeição de
endereços altos. Mantém proteção da BIOS, handlers, casts com sinal e ciclos.
Leituras múltiplas/alinhadas (`load_aligned32`) continuam como antes.

Esse é um experimento de organização do código; o compilador pode produzir
resultado sem ganho. O tamanho e o desempenho no Xtensa decidem sua adoção.
Não altera mapas, buffers, controlador de áudio, renderização ou launcher.

## Testes

`python3 gbsp/tests/test_read_path.py` compara a macro escalar da variante C
com a candidata: 466.560 casos em cada uma de três configurações (desligada,
ligada, dynarec), total 1.399.680. Valida valor, fallback/argumento, ciclos,
categoria/região de estatística e avaliação única do endereço. Inclui 256
regiões, limites de página, todos os alinhamentos, BIOS com PC dentro/fora,
páginas nulas e valores com sinal, com ASan/UBSan.

Handlers são spies: o teste garante escolha/argumento/cast idênticos, não
reimplementa semântica de I/O ou BIOS. Mantêm-se os testes de escrita da
variante C e dos contadores de memória.

## Coleta, se o código gerado justificar testar

Os artefatos ficam em `../outputs/gbsp-compact-reads/`. E liga escritas C e
leituras compactas, desliga `GBSP_MEMORY_PROFILE`: não comparar tempos com D.
Cabeçalho deve mostrar `fast_ram 1 (mapped-v2) | memstats 0 | compact_reads 1`.

Usar C → E → C, mesmos jogos/cenas, oc 0, frameskip fixo 5, cache 32 KB,
PSRAM 80 MHz. Em cada jogo, aguardar 60 s e coletar 120 s parado. Copiar o
arquivo completo `/retro-go/gbsp-perfil.txt` após cada versão, como
`E-base-C1.txt`, `E1.txt`, `E-base-C2.txt`. Conferir IWRAM interna nos três.
Pode começar pelo controle C que já é o firmware de uso normal. Os logs
antigos servem como contexto, não substituem controles desta rodada.

O firmware original C permanece em
`../outputs/gbsp-fast-ram-v2/C-mapped-v2.bin`.

## Resultado de compilação

A primeira organização duplicava o fallback no fonte e aumentou execute_arm
em 1.856 bytes. A forma final usa um bloco do/while local: ao completar uma
leitura mapeada, break sai somente desse bloco; o fallback tem um único ponto.
O interpretador passou de 51.010 bytes em C a 50.914 em E (-96 bytes).
Nenhum vetor de contadores de D está presente no ELF final. O build passou,
sem warnings nos arquivos alterados. Não há medição de velocidade de E ainda.

O padding de alinhamento absorve a redução: IRAM total com vetores/padding
permanece 119.039 bytes. Portanto não se reivindica RAM livre adicional.
