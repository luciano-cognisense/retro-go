from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT=Path(__file__).parent
BUTTONS=OUT.parent/'buttons-proposal'
PAPER='#F5F3EC'
INK='#26333F'
ORANGE='#BD5C23'
RULE='#D9DDD9'
MUTED='#66747E'
WHITE='#FFFFFF'
FONT='/System/Library/Fonts/Supplemental/Arial.ttf'
BOLD='/System/Library/Fonts/Supplemental/Arial Bold.ttf'

def font(size,bold=False):
    return ImageFont.truetype(BOLD if bold else FONT,size)

def game_background():
    scene=Image.new('RGB',(80,60),'#9AC6D0')
    d=ImageDraw.Draw(scene)
    d.ellipse((59,6,68,15),fill='#F2CE75')
    d.polygon([(0,42),(20,24),(37,42)],fill='#74978B')
    d.polygon([(23,42),(48,18),(77,42)],fill='#638783')
    d.rectangle((0,42,79,59),fill='#5F896D')
    for x in range(4,80,13):
        d.rectangle((x,44,x+7,48),fill='#C9AA75')
        d.rectangle((x+2,45,x+5,47),fill='#DCC093')
    d.rectangle((37,35,41,41),fill='#B95F3C')
    d.rectangle((38,32,40,35),fill='#E4B480')
    scene=scene.resize((320,240),Image.Resampling.NEAREST)
    return Image.blend(scene,Image.new('RGB',(320,240),'#1B2729'),0.38)

def shell(title,context,box,background=None):
    im=background.copy() if background is not None else game_background()
    d=ImageDraw.Draw(im)
    x0,y0,x1,y1=box
    d.rounded_rectangle((x0+2,y0+3,x1+2,y1+3),radius=9,fill='#273039')
    d.rounded_rectangle(box,radius=9,fill=PAPER,outline=RULE,width=1)
    d.text((x0+14,y0+9),'HANDS ON LAB',font=font(10,True),fill=INK)
    d.text((x1-14,y0+10),context,font=font(8,True),fill=ORANGE,anchor='ra')
    d.line((x0+14,y0+27,x1-14,y0+27),fill=RULE)
    d.text((x0+14,y0+34),title,font=font(11,True),fill=INK)
    d.line((x0+14,y1-30,x1-14,y1-30),fill=RULE)
    a=Image.open(BUTTONS/'button-a-proposta.png').convert('RGBA')
    b=Image.open(BUTTONS/'button-b-proposta.png').convert('RGBA')
    im.paste(a,(x0+14,y1-26),a);im.paste(b,(x1-100,y1-26),b)
    d.text((x0+34,y1-23),'SELECIONAR',font=font(9,True),fill=INK)
    d.text((x1-80,y1-23),'VOLTAR',font=font(9),fill=INK)
    return im

def menu():
    x0,y0,x1,y1=36,18,284,222
    im=shell('MENU','JOGO PAUSADO',(x0,y0,x1,y1));d=ImageDraw.Draw(im)
    rows=['Salvar e continuar','Salvar e sair','Carregar jogo','Reiniciar','Opções','Sair do jogo']
    for i,label in enumerate(rows):
        y=78+i*18
        if i==0:
            d.rounded_rectangle((x0+14,y-2,x1-14,y+16),radius=5,fill=WHITE,outline=RULE)
            d.rounded_rectangle((x0+14,y-2,x0+17,y+16),radius=1,fill=ORANGE)
        d.text((x0+25,y+2),label,font=font(10,i==0),fill=INK)
    im.save(OUT/'menu-320x240.png')
    im.resize((960,720),Image.Resampling.NEAREST).save(OUT/'menu-3x.png')

def options():
    x0,y0,x1,y1=31,13,289,227
    im=shell('OPÇÕES','AJUSTES DO JOGO',(x0,y0,x1,y1));d=ImageDraw.Draw(im)
    rows=[('Brilho','75%'),('Volume','60%'),('Saída de áudio','Alto-falante'),('Escala','Ajustar'),('Filtro','Padrão'),('Velocidade','Normal'),('Opções do emulador','›')]
    for i,(label,value) in enumerate(rows):
        y=76+i*18
        if i==1:
            d.rounded_rectangle((x0+14,y-2,x1-14,y+16),radius=5,fill=WHITE,outline=RULE)
            d.rounded_rectangle((x0+14,y-2,x0+17,y+16),radius=1,fill=ORANGE)
        d.text((x0+25,y+2),label,font=font(9,i==1),fill=INK)
        d.text((x1-24,y+2),value,font=font(9,i==1),fill=INK if i==1 else MUTED,anchor='ra')
    im.save(OUT/'opcoes-320x240.png')
    im.resize((960,720),Image.Resampling.NEAREST).save(OUT/'opcoes-3x.png')

def launch():
    source=Image.open(OUT.parent/'game-list-exploration/A-lista-e-savestate-320x240.png').convert('RGB')
    background=Image.blend(source,Image.new('RGB',(320,240),'#1B2729'),0.38)
    x0,y0,x1,y1=36,25,284,215
    im=shell('INICIAR JOGO','SELEÇÃO DE JOGO',(x0,y0,x1,y1),background);d=ImageDraw.Draw(im)
    rows=[('Resume game',False),('New game',True),('Add favorite',True),('Delete save',False),('Properties',True)]
    for i,(label,enabled) in enumerate(rows):
        y=y0+60+i*18
        if i==1:
            d.rounded_rectangle((x0+14,y-2,x1-14,y+16),radius=5,fill=WHITE,outline=RULE)
            d.rounded_rectangle((x0+14,y-2,x0+17,y+16),radius=1,fill=ORANGE)
        d.text((x0+25,y+2),label,font=font(10,i==1),fill=INK if enabled else MUTED)
    im.save(OUT/'iniciar-jogo-320x240.png')
    im.resize((960,720),Image.Resampling.NEAREST).save(OUT/'iniciar-jogo-3x.png')

def resume():
    x0,y0,x1,y1=36,25,284,215
    im=shell('RETOMAR','ESTADOS SALVOS',(x0,y0,x1,y1));d=ImageDraw.Draw(im)
    for i,label in enumerate(('Slot 0','Slot 1','Slot 2','Slot 3')):
        y=y0+60+i*18
        if i==1:
            d.rounded_rectangle((x0+14,y-2,x1-14,y+16),radius=5,fill=WHITE,outline=RULE)
            d.rounded_rectangle((x0+14,y-2,x0+17,y+16),radius=1,fill=ORANGE)
        d.text((x0+25,y+2),label,font=font(10,i==1),fill=INK)
    im.save(OUT/'retomar-320x240.png')

def properties():
    source=Image.open(OUT.parent/'game-list-exploration/A-lista-e-savestate-320x240.png').convert('RGB')
    im=Image.blend(source,Image.new('RGB',(320,240),'#1B2729'),0.38)
    d=ImageDraw.Draw(im);x0,y0,x1,y1=40,26,280,214
    d.rounded_rectangle((x0+2,y0+3,x1+2,y1+3),radius=8,fill='#273039')
    d.rounded_rectangle((x0,y0,x1,y1),radius=8,fill=PAPER,outline=RULE)
    d.text((160,y0+13),'PROPRIEDADES',font=font(11,True),fill=INK,anchor='mm')
    d.line((x0+13,y0+29,x1-13,y0+29),fill=RULE)
    rows=[('Name','Tetris.nes'),('Folder','/sd/roms/nes'),('Size','24 KB'),('CRC32','A1B2C3D4'),('Delete file',''),('Close','')]
    for i,(label,value) in enumerate(rows):
        y=y0+39+i*23
        if i==5:
            d.rounded_rectangle((x0+12,y-2,x1-12,y+19),radius=4,fill=WHITE,outline=RULE)
            d.rectangle((x0+12,y-2,x0+15,y+19),fill=ORANGE)
        d.text((x0+22,y+3),label,font=font(10,i==5),fill=INK)
        if value:d.text((x1-20,y+3),value,font=font(9),fill=MUTED,anchor='ra')
    im.save(OUT/'propriedades-320x240.png')

menu();options();launch();resume();properties()
board=Image.new('RGB',(1008,552),'#E4E6E8');d=ImageDraw.Draw(board)
for x,y,label,name in [(8,5,'Menu do jogo','menu'),(344,5,'Opções','opcoes'),(680,5,'Seleção de jogo','iniciar-jogo'),(8,281,'Retomar jogo','retomar'),(344,281,'Propriedades','propriedades')]:
    d.text((x,y),label,font=font(12,True),fill=INK)
    board.paste(Image.open(OUT/f'{name}-320x240.png'),(x,y+23))
board.save(OUT/'comparacao.png')
