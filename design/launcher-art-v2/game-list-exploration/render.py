from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).parent
FONT = '/System/Library/Fonts/Supplemental/Arial.ttf'
BOLD = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'
CREAM = '#F5F3EC'
INK = '#26333F'
ORANGE = '#BD5C23'
BUTTON_A = '#E53935'
BUTTON_B = '#FFD21F'
LINE = '#D9DDD9'
THEME = OUT.parents[2]/'themes/hands-on-lab'

def font(size, bold=False):
    return ImageFont.truetype(BOLD if bold else FONT, size)

def a_button(im, x, y, letter, color):
    icon = Image.open(THEME/f'button_{letter.lower()}.png').convert('RGBA')
    icon.putalpha(Image.new('L',icon.size,255))
    pixels=icon.load()
    for yy in range(icon.height):
        for xx in range(icon.width):
            if pixels[xx,yy][:3] == (255,0,255): pixels[xx,yy]=(255,0,255,0)
    im.paste(icon,(x,y),icon)

def shell(im, subtitle):
    d=ImageDraw.Draw(im)
    d.text((16,12),'HANDS ON LAB',font=font(11,True),fill=INK)
    d.text((304,13),'SUPER NINTENDO',font=font(9,True),fill=ORANGE,anchor='ra')
    d.line((16,31,304,31),fill=LINE)
    d.text((16,41),'JOGOS',font=font(13,True),fill=INK)
    d.text((304,43),subtitle,font=font(9),fill='#657079',anchor='ra')
    d.line((16,211,304,211),fill=LINE)
    a_button(im,16,218,'A',BUTTON_A); d.text((36,221),'JOGAR',font=font(10,True),fill=INK)
    a_button(im,196,218,'B',BUTTON_B); d.text((216,221),'CONSOLES',font=font(10),fill=INK)

def savestate(im, box, available=True):
    d=ImageDraw.Draw(im)
    d.rounded_rectangle(box,radius=7,fill='white',outline=LINE)
    x0,y0,x1,y1=box
    if available:
        d.rectangle((x0+7,y0+17,x1-7,y1-29),fill='#A8C7CD')
        d.ellipse((x0+58,y0+25,x0+75,y0+42),fill='#E6BE72')
        d.rectangle((x0+7,y0+61,x1-7,y1-29),fill='#5D8A79')
        for i in range(3):
            d.rectangle((x0+14+i*24,y0+53-i*5,x0+29+i*24,y0+70),fill='#D3AF76',outline='#7D6653')
        d.polygon([(x0+24,y0+51),(x0+33,y0+39),(x0+42,y0+51)],fill='#A65A3A')
    else:
        d.rounded_rectangle((x0+9,y0+18,x1-9,y1-29),radius=3,fill='#F1EFE8',outline=LINE)
        d.text(((x0+x1)//2,y0+60),'SEM REGISTRO',font=font(8,True),fill='#657079',anchor='mm')

games=['Super Mario World','Donkey Kong Country','F-Zero','The Legend of Zelda','Super Metroid','Yoshi’s Island']

def option_a():
    im=Image.new('RGB',(320,240),CREAM);shell(im,'18 JOGOS  ·  3 / 18');d=ImageDraw.Draw(im)
    for i,name in enumerate(games):
        y=68+i*22
        if i==2:
            d.rounded_rectangle((16,y-2,188,y+19),radius=5,fill='white',outline=LINE)
            d.rounded_rectangle((16,y-2,19,y+19),radius=1,fill=ORANGE)
        d.text((27,y+4),name,font=font(10,i==2),fill=INK)
    savestate(im,(205,72,304,190))
    im.save(OUT/'A-lista-e-savestate-320x240.png')

def option_b():
    im=Image.new('RGB',(320,240),CREAM);shell(im,'18 JOGOS  ·  3 / 18');d=ImageDraw.Draw(im)
    savestate(im,(16,68,111,190))
    for i,name in enumerate(games[:5]):
        y=70+i*25
        if i==2:
            d.rounded_rectangle((122,y-2,304,y+22),radius=5,fill='white',outline=LINE)
            d.rectangle((122,y-2,125,y+22),fill=ORANGE)
        d.text((132,y+5),name,font=font(10,i==2),fill=INK)
    im.save(OUT/'B-savestate-e-lista-320x240.png')

def option_c():
    im=Image.new('RGB',(320,240),CREAM);shell(im,'18 JOGOS  ·  3 / 18');d=ImageDraw.Draw(im)
    for i,name in enumerate(games):
        y=67+i*22
        if i==2:
            d.rounded_rectangle((16,y-2,304,y+19),radius=5,fill='white',outline=LINE)
            d.rounded_rectangle((16,y-2,19,y+19),radius=1,fill=ORANGE)
        d.text((27,y+4),name,font=font(11,i==2),fill=INK)
    im.save(OUT/'C-lista-sem-capa-320x240.png')

option_a();option_b();option_c()
Image.open(OUT/'C-lista-sem-capa-320x240.png').save(OUT/'A-sem-savestate-320x240.png')
board=Image.new('RGB',(664,276),'#E4E6E8');d=ImageDraw.Draw(board)
for n,(key,title) in enumerate([('A-lista-e-savestate','Com último savestate'),('A-sem-savestate','Sem savestate')]):
    x=12+n*332
    d.text((x,5),title,font=font(12,True),fill=INK)
    board.paste(Image.open(OUT/f'{key}-320x240.png'),(x,28))
board.save(OUT/'comparacao-A.png')
