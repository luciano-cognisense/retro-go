# Resultado no console — base comum das tabelas de waitstate

## Coletas

- Controle: `outputs/gbsp-waitstate-common-base-controle.txt`, 76 linhas; Mario 24 janelas completas e Fire Red 43.
- Candidata: `outputs/gbsp-waitstate-common-base-candidata.txt`, 62 linhas; Mario 17 janelas completas e Fire Red 37.
- Ambas em OC 0, frameskip 0, sem panic, assert, backtrace ou watchdog.

Os percursos em movimento não são idênticos. A decisão usa primeiro as sequências paradas do início de cada jogo, que têm render, entrada e ROM comparáveis. As distribuições completas e o movimento servem como confirmação secundária.

## Sequências estáveis comparáveis

| jogo | controle fora | candidata fora | delta fora | controle emu | candidata emu | delta emu | total |
|---|---:|---:|---:|---:|---:|---:|---:|
| Mario, 7 janelas iniciais | 20.824,9 us | 20.602,7 us | **−222,1 us (−1,07%)** | 23.983,3 | 23.838,6 | **−0,60%** | −0,22% |
| Fire Red, 5 controle / 3 candidata | 16.161,6 us | 15.965,3 us | **−196,3 us (−1,21%)** | 22.150,0 | 21.935,0 | **−0,97%** | −0,89% |

No Mario, a primeira janela é aquecimento e perde 0,72% em `fora`; as seis seguintes ganham de 1,03% a 1,80%. No Fire Red, as três janelas pareadas ganham 0,50%, 1,35% e 1,57%. Portanto, a melhora não depende de uma única janela extrema.

O render do Mario fica 78 us mais caro em média nessa sequência, absorvendo parte do ganho de CPU. Sistema fica invariável. O áudio oscila por causa do controlador de relógio e não mede esta otimização.

## Movimento

No subconjunto de Fire Red com render baixo e atividade semelhante, a candidata reduz `fora` em 2,71% e `emu` em 3,06%. Como os roteiros não foram idênticos, isso confirma o sinal, mas não deve ser tomado como tamanho exato do ganho.

No conjunto bruto, misturar transições, render adaptativo, áudio e faults produz medianas enganosas. Mario em movimento tem poucas janelas na candidata; não é usado para quantificar.

## Relação com o offline

O assembly ponderado previa melhora no Thumb e pequeno custo no ARM. Um ganho líquido perto de 1% nas cenas estáveis é coerente com o mix ARM/Thumb medido nos dois jogos. A candidata também passou antes por:

- 35/35 cenários de save/load com WAITCNT;
- 33/33 regressões diferenciais;
- 33/33 sob ASan/UBSan;
- builds diferenciados apenas por `CONFIG_GBSP_WAITSTATE_COMMON_BASE`.

## Decisão

**Candidata aceita para consolidação como próxima versão de performance.** O ganho é pequeno, porém repetível nos dois jogos, sem custo de DRAM, sem regressão funcional observada e com apenas +88 B de IRAM no ELF. Não é necessária nova coleta antes da consolidação.
