# Diagnóstico de acessos de dados do interpretador

Base: variante C, commit `0698b9b4`. Opção `CONFIG_GBSP_MEMORY_PROFILE`,
desligada por padrão e ignorada com dynarec. Não é otimização.

## O que mede

Reutiliza os oito vetores e quatro pontos `STATS_MEMORY_ACCESS` existentes no
interpretador. Conta operações, por região GBA e largura/semântica:
`r8`, `rs8`, `r16`, `rs16`, `r32`, `w8`, `w16`, `w32`.
`rs8` e `rs16` são leituras com extensão de sinal, não categorias adicionais
para o mesmo acesso. Para total de leitura de 8 bits, somar r8 + rs8.

Cada bloco `mem_window frames=N` tem 16 linhas e contém totais brutos dos N
quadros da linha de perfil imediatamente anterior. Dividir por N para comparar
janelas de tamanhos diferentes. Todos os contadores são zerados nos mesmos
pontos que o perfil, inclusive ao sair de menus. O escritor do log roda na
mesma tarefa do emulador; nenhuma thread emula durante a leitura/reset.

Regiões: 00 BIOS, 01 não usada, 02 EWRAM, 03 IWRAM, 04 I/O, 05 paleta,
06 VRAM, 07 OAM, 08–0D ROM/janelas de cartucho, 0E–0F backup/área de cartucho.
Região é classificação por endereço, não prova de qual dispositivo respondeu.
Os hooks excluem endereços >= 0x10000000.

Não conta fetch de instruções, DMA, acessos da PPU, tráfego físico na PSRAM,
cache misses ou ciclos gastos por região. Quantidade de acessos não é fração
do tempo: serve para selecionar hipóteses, não prever ganho de uma otimização.

## Custo e validade

O diagnóstico adiciona incrementos no caminho quente, 512 bytes de contadores,
código e saída no cartão. Seus tempos não são comparáveis aos builds A/B/C.
O cabeçalho traz `memstats 1`; o log é `/retro-go/gbsp-memory.txt` no cartão.
A variante C continua arquivada em `../outputs/gbsp-fast-ram-v2/`.

Os contadores originais são unsigned de 32 bits. São reiniciados normalmente a
cada 300 quadros (cerca de 84 milhões de ciclos GBA, muito abaixo de 2^32
operações por contador). Falhas repetidas de escrita no cartão invalidam a
coleta; não interpretar uma coleta sem blocos completos como contagem confiável.
Impressão usa `%u` com cast unsigned, compatível com Newlib Nano.

## Teste no aparelho — uma gravação

1. Gravar `../outputs/gbsp-memory-profile/D-memory-profile.bin`.
2. Usar oc 0 e frameskip fixo 5. Manter cache 32 KB / PSRAM 80 MHz.
3. Fire Red e Mario: usar as mesmas cenas da comparação anterior. Aguardar
   60 segundos e deixar mais 120 segundos em cada cena, sem abrir menus.
4. Sair normalmente para descarregar a janela final. Copiar o arquivo completo
   `/retro-go/gbsp-memory.txt` para `../outputs/D1-memory.txt`.
5. Conferir `fast_ram 1 (mapped-v2)`, `memstats 1`, IWRAM interna e blocos
   `mem_window` seguidos pelas 16 regiões. Não precisa repetir A/B/A.

Em seguida, voltar ao firmware C para uso normal. Esse build mais lento é
apenas diagnóstico; não avaliar regressão de desempenho por ele.

## Validação local

`python3 gbsp/tests/test_memory_profile.py` compila os contadores reais em C++
e consome a interface em C, verificando linkage, oito categorias nas 16
regiões, serialização, UINT32_MAX, reset e ausência de símbolos/saída com a
opção desativada ou dynarec. Usa AddressSanitizer/UndefinedBehaviorSanitizer.
O teste diferencial `test_ram_store.py` continua obrigatório para o atalho C.
