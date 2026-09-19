# Experimento: escritas diretas na RAM

Base: `35e4294b` (instrumento aprovado). Branch: `experiment/gbsp-fast-ram-writes`.

## Hipótese

Evitar a chamada a `write_memory8/16/32` nas escritas do interpretador em
EWRAM/IWRAM pode diminuir o campo `fora`. O teste não presume que chamadas sejam
o gargalo e não promete 100% de velocidade.

`CONFIG_GBSP_FAST_RAM_WRITES` é uma opção do componente gpSP, desligada por
padrão. O cabeçalho de perfil mostra `fast_ram 0 (original)` ou `fast_ram 1 (mapped-v2)`. As medições
por quadro, áudio, frameskip e configurações compartilhadas permanecem iguais.

## Implementação

- Regiões 0x02 e 0x03 usam as páginas de 32 KB já presentes em
  `memory_map_read`. `init_memory()` mapeia todos os espelhos de EWRAM/IWRAM.
  O teste de região restringe esse uso às duas RAMs; não é um mapa genérico
  de escrita. A conversão de endian permanece igual à original.
- As macros de escrita simples e múltipla preservam a contagem de ciclos e
  estatísticas. Alinhamento continua sob responsabilidade dos mesmos chamadores.
- I/O, VRAM, paleta, OAM, backup e demais endereços seguem pelos manipuladores
  originais, inclusive seus alertas. Alertas já pendentes não são apagados.
- O atalho não é ativado com dynarec ou detecção de código automodificado.
- Nenhum buffer ou mapa adicional. Há possível aumento de código em IRAM e
  overhead nas escritas fora da RAM: ambos precisam entrar na avaliação.

## Validação no computador

Execute `python3 gbsp/tests/test_ram_store.py` na raiz do repositório.

O teste extrai as macros reais de `cpu.cpp`, as definições de acesso/endian e os
casos de escrita de RAM do baseline Git. Compara o caminho novo ao antigo em
9.128 operações por configuração, com AddressSanitizer e UndefinedBehaviorSanitizer.
São quatro configurações: desligado, ligado, SMC e dynarec.

Cobre valores truncados, regiões espelhadas, limites de páginas/buffers,
alinhamento das escritas simples, escritas sequenciais alinhadas, ciclos,
alertas prévios, avaliação única do valor e encaminhamento para handlers.
Os handlers fora de RAM são substitutos que registram argumentos/retorno:
isso verifica o encaminhamento, não toda a semântica de hardware do GBA.
Não é teste de jogo, de performance Xtensa ou de estabilidade física.

## Medição no kit

Os artefatos A e B ficam em `../outputs/gbsp-fast-ram/`, com configurações,
logs e hashes. A é o caminho original; B ativa apenas esta opção. Voltar a A
usa o mesmo binário A, sem recompilar.

1. A → B → A, 240 MHz (`oc 0`), cache 32 KB, PSRAM 80 MHz, frameskip fixo 0.
2. Fire Red e Mario: mesma cena/save por jogo e versão, 60 s de aquecimento e
   pelo menos 120 s de coleta, sem menus durante a coleta.
3. Conferir `fast_ram`, `iwram interna`, cache de ROM e RAM livre no cabeçalho.
   Se a localização da IWRAM mudar, a comparação mistura dois efeitos.
4. Guardar logs integrais por versão e jogo. Comparar `fora`, `emu`, `total`,
   estabilidade das bases e dispersão; não usar somente velocidade limitada
   pelo DAC. `envio` pode aumentar porque a emulação passou a esperar mais.
5. Ganho próximo da dispersão é inconclusivo. Verificar também movimento,
   transições, batalha e save/load antes de considerar a mudança adotada.

Não há gravação automática. O offset da aplicação deve corresponder à tabela
de partições do aparelho usado. Não substituir a imagem completa do produto.

Para builds próprios, a opção aparece em `idf.py menuconfig` no menu
`gpSP experiments`. O `gbsp/sdkconfig` existente prevalece sobre defaults;
`rg_tool.py clean` remove esse arquivo. Os valores de cada artefato estão
arquivados para reprodução. Não alterar o sdkconfig compartilhado dos outros apps.

## Rodada inicial e variante compacta

A primeira variante (buffers diretos, dois ramos) apresentou redução preliminar
em `fora` de aproximadamente 2,8% nos dois jogos. Os logs usaram frameskip
automático; a análise em `../outputs/gbsp-fast-ram/analysis/` seleciona janelas
com 50 quadros desenhados em 300 e mantém os controles A1/A2. Isso não substitui
validação final com configuração fixa.

A variante `mapped-v2` unifica os dois ramos usando o mapa existente. Em relação
ao original, `.iram0.text` cresce 3.020 bytes, contra 8.820 na primeira variante.
Incluindo padding e vetores, a redução entre variantes é 5.888 bytes. Não há
nova alocação de dados. A consulta ao mapa adiciona uma leitura: desempenho
precisa ser medido, não pode ser deduzido do tamanho menor.

Firmware C, ELF, configurações, testes e build completo ficam em
`../outputs/gbsp-fast-ram-v2/`. Os binários A/B anteriores foram preservados.
Para comparar B → C → B, use a mesma cena, oc 0 e frameskip **fixo 5** em todas
as etapas, 60 s de aquecimento e 120 s de coleta por jogo. Isso evita repetir
agora a série com frameskip 0. Registre logs integrais separados e confira
`fast_ram 1 (mapped-v2)` em C; B antigo mostra apenas `fast_ram 1`.
Antes da adoção final, confira também frameskip 0 e correção visual/save/load.

## Validação funcional C2

C2 contém aproximadamente 310 s de Mario e 273 s de Fire Red contabilizados
pelas fases do perfil. Frameskip fixo 5; Mario em oc 2, Fire Red em oc 0/2.
RAM interna livre de 143 KB, maior bloco 52 KB. O usuário confirmou gráficos,
áudio e salvar/carregar funcionando, inclusive após reiniciar. Esse teste
limitado aos dois jogos não constitui garantia de compatibilidade geral.

B2, com frameskip fixo 5, não revelou perda consistente de C: nas primeiras
10 janelas elegíveis por jogo, após aquecimento, o tempo de emulação de C foi
0,68% menor em Fire Red e 0,75% menor em Mario. As diferenças são pequenas e
não comprovam superioridade de velocidade. C foi escolhida pela economia de
IRAM, com ganho inicial contra A e funcionamento confirmados nesta amostra.
Análises reproduzíveis: `../outputs/gbsp-fast-ram-v2/analysis/`.
