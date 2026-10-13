# Caminho rápido AL na condição ARM — resultado offline

Opção `CONFIG_GBSP_ARM_AL_FASTPATH` (`default n`), commit `26c0668d` sobre a v8.
Antes do `switch` de 16 condições, `execute_arm()` testa AL com uma comparação;
as demais condições continuam na tabela de salto. Sem mudança de comportamento.

## Builds

ESP-IDF 5.3.1, `gbsp/profiles/build-performance-v8.sh`, mesmo HEAD; a candidata
só acrescenta `CONFIG_GBSP_ARM_AL_FASTPATH=y` ao perfil v8. O `execute_arm` do
controle é idêntico (instrução a instrução, a menos de endereços) ao objeto da
candidata v8 analisado no pacote `waitstate-common-base`.

| | controle (v8) | candidata |
|---|---:|---:|
| bin | 665.392 B | 664.960 B |
| .iram0.text | 117.359 B | 116.927 B (−432) |
| .dram0.data / .bss | 13.244 / 166.520 B | iguais |
| frame de `execute_arm` | 112 B | 96 B |
| SHA-256 do bin | `48088dbe…e145` | `e96a0f13…c35d` |

## Assembly

Caminho de uma instrução AL no controle: `l32r`, `extui`, `addx4`, `l32i`,
`jx` para a entrada 0xE, que repete a sequência para o despacho do opcode
(dois `jx` e duas cargas de tabela). Na candidata: `movi`, `extui`, `beq`
tomado, e só o despacho do opcode.

`paths_elf.py` mede o caminho comum de cada handler até o próximo despacho, no
ELF ligado (`paths-controle.json`, `paths-candidata.json`). Ponderado pelo
histograma `arm_handler` (AL) de `../waitstate-common-base/arm-condition-profile.txt`
(cobertura 100%), por instrução ARM AL:

| | instr | saltos tomados | jx | cargas | pilha |
|---|---:|---:|---:|---:|---:|
| Fire Red | −2,41 | +0,46 | −1,24 | −1,22 | +0,83 |
| Mario | −3,12 | +0,30 | −1,33 | −1,58 | +0,84 |

Thumb (sem pesos; 217 handlers com caminho nos dois): média −0,47 instrução e
+0,41 acesso à pilha. A alocação de registradores mudou: 148 handlers ficam
iguais; `str/ldr` com imediato e relativos a SP (0x60, 0x90) perdem 6–7
instruções e ganham 4 acessos à pilha; `stmia/ldmia` (0xC0) ganham 4 instruções
e 3 acessos. Sem o histograma Thumb (`gbsp-thumb-handler-profile/console-analysis.json`,
fora do backup) o saldo Thumb não pode ser ponderado.

Instruções não-AL pagam `movi` + `beq` não tomado + `j` antes da tabela.

## Conclusão provisória

ARM melhora de forma clara (cerca de 2,4 instruções e 1,2 salto indireto a
menos por instrução AL no Fire Red, onde ~77% das ARM são AL e ARM é ~54% do
total). O efeito no Thumb é misto e pode anular parte do ganho. Precisa de A/B
no aparelho, com o mesmo protocolo de `../waitstate-common-base/console/LEIA-ME.md`,
priorizando o Fire Red.
