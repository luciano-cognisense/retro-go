# Campanha de arte do launcher — 2026-10-28

Base: 103fb83f (configuration/gbsp-defaults). Branch isolada: design/launcher-art-v2.
61317ef2 é ancestral da base; gui.c, gui.h, images.c e tema sem diferenças contra esse baseline visual.
Nenhum asset ou código de produto alterado. Sem merge, push ou flash.

## Auditoria preliminar
Inspeção do código e dos assets, ainda sem fotos do LCD nem captura da composição em execução.
- Paleta azul escuro, creme e laranja; trilhas eletrônicas conectam splash e fundos.
- Splash claro e navegação escura: avaliar continuidade percebida e transição de brilho no painel.
- Hierarquia central preservada: arte 128x88 dentro de moldura 176x148; laterais 88x60 com somente 47 pixels visíveis. Conferir reconhecimento nas duas bordas.
- Arte compartilhada entre sistemas da mesma família: SNES, Mega Drive e PC Engine usam a mesma imagem 16-bit. O texto é essencial para diferenciá-los.
- Nome central desenhado duas vezes com deslocamento horizontal de 1 px. Conferir fechamento de letras e nomes longos no LCD.
- Pontos de navegação variam conforme sistemas habilitados; validar com o cartão real.
- Preservar dimensões, formatos e transparência magenta onde exigida. O validador existente verifica dimensões de um subconjunto e presença de magenta, não substitui revisão visual.
- Tentativa do validador no Python padrão impedida por ausência de Pillow (PIL); não é uma falha constatada nos assets.

## Contexto de produto
Documento consultado: Hands on Lab — Conceito da Experiência de Montagem Digital (v0.1).
https://docs.google.com/document/d/1Z2kmw8q1UWvum6N1d5CO2MN5Aupo6JkbolobnYz54CQ/edit
Aprender construindo, descoberta de eletrônica, montagem entre pais e filhos e conquista ao chegar ao launcher. Inferência para design: clareza, acolhimento e identidade eletrônica própria. O documento trata da experiência web e não impõe novas funções ao firmware.

## Próxima etapa
Recolher fotos frontais do splash, carrossel e lista de jogos; uma foto à distância de uso; referências de marca e preferências. Vídeo se houver problema de navegação.
Após alinhamento, criar três mockups 320x240 separados dos assets de produção:
A. Refinamento da identidade atual.
B. Laboratório acessível: superfícies simples e pistas eletrônicas discretas.
C. Retrô técnico: formas pixeladas e paleta controlada.
Manter o carrossel em todas as propostas. Escolher uma direção antes de implementar uma tela e uma categoria; expandir somente após revisão.

## Vídeo e orientação recebida
Vídeo local: /Users/lucianocaminhajunior/Downloads/console.mp4 (111,56 s).
Inspecionados oito quadros distribuídos pelo vídeo; esta amostragem não permite concluir sobre fluidez ou cintilação.
Carrossel visível aproximadamente aos 14 s e 70 s; listas GBA e GBC em outros quadros. Molduras laranja têm grande peso visual. Nas listas, fundo ilustrado, preview e nomes longos aumentam densidade. Cor do painel não calibrada pela gravação.
Preferência explícita: ícones distintos e semelhantes aos consoles reais; cores mais claras, apresentação limpa e minimalista.

Três estudos em mockups/, sem modificar firmware: A Essencial (creme e laranja discreto), B Laboratório (verde suave e seleção contornada), C Galeria retrô (lavanda e geometria mais reta). Todos 320x240, mesma navegação e silhuetas próprias para NES, SNES norte-americano e Game Boy. Ilustrações e tipografia são estudos; ainda não são assets aprovados nem captura do firmware. Confirmar versão regional desejada dos consoles antes de finalizar desenhos. Cores claras e fontes precisam de validação no LCD.
Validador existente executado com sucesso usando Python do runtime Codex com Pillow. Nenhum asset de produção alterado.
