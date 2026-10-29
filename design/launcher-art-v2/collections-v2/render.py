from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
O=Path(__file__).parent
base=O.parent/'logos/nes-320x240.png'
font='/System/Library/Fonts/Supplemental/Arial Bold.ttf'
button_a=Image.open(O.parents[2]/'themes/hands-on-lab/button_a.png').convert('RGBA')
button_a.putalpha(Image.new('L',button_a.size,255))
pixels=button_a.load()
for yy in range(button_a.height):
 for xx in range(button_a.width):
  if pixels[xx,yy][:3] == (255,0,255): pixels[xx,yy]=(255,0,255,0)
for key,title in [('favorite','FAVORITOS'),('recent','RECENTES')]:
 im=Image.open(base).convert('RGB');d=ImageDraw.Draw(im)
 d.rounded_rectangle((73,42,246,183),radius=9,fill='white',outline='#D9DDD9')
 d.rounded_rectangle((139,40,180,43),radius=1,fill='#BD5C23')
 art=Image.open(O/'source'/f'{key}.png').convert('RGBA');art=art.crop(art.getbbox());art.thumbnail((126,88),Image.Resampling.LANCZOS)
 im.paste(art,((320-art.width)//2,53+(88-art.height)//2),art)
 d.text((160,162),title,font=ImageFont.truetype(font,13),fill='#26333F',anchor='mm')
 d.rectangle((15,218,34,236),fill='#F5F3EC')
 im.paste(button_a,(16,218),button_a)
 im.save(O/f'{key}-320x240.png')
 im.resize((960,720),Image.Resampling.NEAREST).save(O/f'{key}-3x.png')
board=Image.new('RGB',(664,276),'#E4E6E8');d=ImageDraw.Draw(board)
for x,key,title in [(12,'favorite','Favoritos'),(344,'recent','Recentes')]:
 d.text((x,6),title,font=ImageFont.truetype(font,13),fill='#26333F')
 board.paste(Image.open(O/f'{key}-320x240.png'),(x,28))
board.save(O/'comparacao.png')
