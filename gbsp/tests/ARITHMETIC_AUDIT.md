# Frente Codex — aritmética/flags/shifts, sem console

Base: `9962eb325`. Checkout isolado: `retro-go-arithmetic`, branch
`experiment/gbsp-arithmetic-audit`. A branch principal não foi alterada.
Configuração de controle e candidata: C (fast_ram ligado, D/E/F desligados),
cache 32 KB, PSRAM 80 MHz. IDF 5.3.1, GCC Xtensa 13.2.0, -O3 como no núcleo.

## Candidata escolhida: overflow de subtração

A única mudança de execução é uma linha em calculate_v_flag_sub:

```diff
- v_flag = (((src_a ^ src_b) & (~src_b ^ dest)) >> 31)
+ v_flag = (((src_a ^ src_b) & (src_a ^ dest)) >> 31)
```

Prova bit a bit: onde a XOR b é zero, ambos os resultados são zero.
Onde a XOR b é um, a = NOT b, portanto a XOR d = NOT b XOR d.
A identidade vale para quaisquer palavras, não depende de carry ou resultado
válido de subtração. Os chamadores usam valores sem efeitos colaterais; a ordem
de writeback e o cálculo de carry não mudam. A mudança alcança SUB/SBC/RSB/RSC,
CMP e NEG pelas macros existentes, sem tocar representação ou sincronização CPSR.

No wrapper isolado, a expressão original produz 5 operações lógicas/extração,
e a candidata produz 4: elimina um XOR. Wrapper não prova ganho no laço real.

### Validação

`python3 gbsp/tests/test_arithmetic_flags.py` extrai as macros do baseline e da
candidata; verifica ambas contra referência independente de 64 bits para N/Z/C/V.
Passaram **4.524.612 casos** de adição/subtração, carry-in 0/1, limites e valores
pseudoaleatórios, com ASan/UBSan. Não é emulação de ROM nem teste de hardware.

### Compilação do interpretador completo

Foi compilado um controle de cpu.cpp do baseline com exatamente o comando e a
configuração da candidata. Para obter controle ELF comparável, uma cópia da
biblioteca do núcleo recebeu somente esse objeto de controle; o link usou os
mesmos demais objetos, opções e scripts. O build candidato não foi sobrescrito.

| Medida | Controle | Candidata |
|---|---:|---:|
| execute_arm | 51.010 B | 50.982 B |
| .iram0.text | 117.967 B | 117.939 B |
| padding .iram0.text_end | 45 B | 73 B |
| IRAM total com vetores/padding | 119.039 B | 119.039 B |
| stack frame execute_arm | 112 B | 112 B |
| .dram0.data | 9.796 B | 9.796 B |
| .dram0.bss | 166.224 B | 166.224 B |

Mesmos símbolos externos nos dois objetos CPU: nenhuma dependência auxiliar
nova. Build passou, warnings preservados nos artefatos. Os tamanhos do controle
coincidem com C arquivada. Não há economia de RAM total: padding absorve 28 B.

**Cuidado metodológico para a frente Claude:** execute_arm contém tabelas de
salto inline. objdump -d pode decodificar esses dados como instruções, inclusive
chamadas espúrias. Contagens ingênuas de mnemonics/spills/calls no bloco inteiro
foram descartadas. Não usar esse método para afirmar redução de chamadas.

## Outras fórmulas examinadas

Comparei sondas Xtensa de carry por comparações, forma booleana e forma bit a
bit para ADC/SBC. Há trocas entre branches e operações adicionais; não selecionei
essas alternativas para esta rodada. As sondas estão em carry-probes.cpp/.s.
Não foi demonstrado ganho dinâmico ou validado um patch integrado para elas.

## Achados preexistentes — não corrigidos nesta proposta

Reprodutor separado: `python3 gbsp/tests/audit_shift_edges.py`. Ele espera e
confirma problemas existentes no commit-base; não é um teste verde de correção.

1. **LSL/LSR/ASR Thumb por registrador usam reg[rs] inteiro**, sem &0xFF.
   Exemplo: entrada 0x80000001, Rs=256, C=1. Byte baixo de Rs=0 deveria preservar
   valor/C; baseline retorna zero para LSL/LSR, e 0xFFFFFFFF para ASR.
2. **ROR por registrador pode executar shifts C fora da largura de 32 bits.**
   UBSan acusa erro com contagens 32, 33 e 255 no Thumb. O código ARM de ROR
   também passa o contador de 8 bits sem reduzir/ramificar o rotate para 0..31;
   o mesmo risco aparece por inspeção, mas o reprodutor executado é Thumb.

A documentação ARM DDI 0029E define o byte menos significativo como contador e
mapeia as operações Thumb de formato 4 às equivalentes ARM:
[ARM7TDMI Data Sheet — shifts por registrador e formato Thumb 4](https://blogs.igalia.com/aboya/files/2025/08/ARM7TDMI-Manual-1995.pdf).

Esses achados dizem respeito a correção/portabilidade. Não sabemos se os jogos
medidos acionam os casos, e não os relacionamos à lentidão observada. A prova
UBSan no host não é reprodução de falha visível no ESP32. Correção deve ser
outro patch, com escopo explícito e testes de arquitetura, sem misturar à flag V.

## Entrega / decisão proposta

Proposta de uma linha pronta para revisão cruzada; não aplicada à branch de
trabalho principal e não promovida a versão preferida. A frequência de SUB/CMP
com flags não foi medida; nenhum percentual de ganho pode ser prometido.
Não exige coleta imediata. Revisar com Claude, comparar com sua descoberta e
escolher uma única candidata antes de voltar ao hardware.

Artefatos em `../outputs/gbsp-arithmetic-audit/`: patch, testes/logs,
controle/candidata ELF, binário candidato, configurações, comandos de controle,
assembly e manifesto. Não usar como firmware adotado sem revisão/medição.
