from PIL import Image, ImageDraw, ImageFont
from pathlib import Path
O=Path(__file__).parent; root=O.parents[2];theme=root/'themes/hands-on-lab'
F='/System/Library/Fonts/Supplemental/Arial Bold.ttf';REG='/System/Library/Fonts/Supplemental/Arial.ttf'
button_a=Image.open(root/'themes/hands-on-lab/button_a.png').convert('RGBA')
button_a.putalpha(Image.new('L',button_a.size,255))
pixels=button_a.load()
for yy in range(button_a.height):
 for xx in range(button_a.width):
  if pixels[xx,yy][:3] == (255,0,255): pixels[xx,yy]=(255,0,255,0)
items=[('nes','Nintendo Entertainment System','8bit'),('snes','Super Nintendo Entertainment System','16bit'),('gb','Game Boy','portable_vertical'),('gbc','Game Boy Color','portable_vertical'),('gba','Game Boy Advance','portable_horizontal'),('gw','Game & Watch','clock'),('sms','Master System','8bit'),('gg','Game Gear','portable_horizontal'),('md','Mega Drive / Genesis','16bit'),('col','ColecoVision','8bit'),('pce','PC Engine','16bit'),('lnx','Atari Lynx','portable_horizontal'),('a26','Atari 2600','8bit'),('msx','MSX','16bit')]
logos={'nes':O/'nes-logo-reference.png','snes':O/'source/snes-logo.png'}
logos['md']=O/'source/md-logo-user.png'
for key in ['gb','gbc','gba','gw','sms','gg','md','col','pce','lnx','a26','msx']:
 candidate=O/'source'/f'{key}-logo.png'
 if candidate.exists() and key!='md': logos[key]=candidate
def contain(path,size):
 im=Image.open(path).convert('RGBA'); im=im.crop(im.getbbox());im.thumbnail(size,Image.Resampling.LANCZOS);return im
font=ImageFont.truetype(F,12)
for key,title,family in items:
 im=Image.new('RGB',(320,240),'#F5F3EC');d=ImageDraw.Draw(im)
 d.text((16,12),'HANDS ON LAB',font=ImageFont.truetype(F,11),fill='#26333F');d.text((304,13),'CONSOLES',font=ImageFont.truetype(F,9),fill='#BD5C23',anchor='ra');d.line((16,31,304,31),fill='#D9DDD9')
 d.rounded_rectangle((72,41,247,184),radius=10,fill='white',outline='#D9DDD9');d.rounded_rectangle((139,40,180,43),radius=1,fill='#BD5C23')
 for x in (-38,270):d.rounded_rectangle((x,65,x+87,164),radius=6,fill='white',outline='#D9DDD9')
 art_path=O/'source'/f'{key}-us.png'
 if not art_path.exists(): art_path=theme/f'carousel_{family}.png'
 icon=contain(art_path,(128,88));im.paste(icon,((320-icon.width)//2,53),icon)
 if key in logos and logos[key].exists():
  lg=contain(logos[key],(86,24) if key=='msx' else ((124,24) if key=='md' else (108,30)));im.paste(lg,((320-lg.width)//2,146),lg)
 else:
  # Two-line labels keep every system name inside the same safe logo area.
  words=title.split(); lines=[];line=''
  for w in words:
   t=(line+' '+w).strip()
   if d.textbbox((0,0),t,font=font)[2] > 150 and line:lines.append(line);line=w
   else:line=t
  if line:lines.append(line)
  y=151-(len(lines)-1)*7
  for s in lines:d.text((160,y),s,font=font,fill='#26333F',anchor='mm');y+=14
 d.line((62,103,57,108,62,113),fill='#BD5C23',width=2);d.line((257,103,262,108,257,113),fill='#BD5C23',width=2)
 d.line((16,211,304,211),fill='#D9DDD9');im.paste(button_a,(16,218),button_a);d.text((36,221),'JOGOS',font=ImageFont.truetype(F,10),fill='#26333F');d.text((304,222),'<  > ESCOLHER',font=ImageFont.truetype(REG,10),fill='#26333F',anchor='ra')
 im.save(O/f'{key}-320x240.png')
# contact sheets in grouped pages
for start in range(0,len(items),7):
 group=items[start:start+7]; board=Image.new('RGB',(1008,((len(group)+2)//3)*278),'#E4E6E8');d=ImageDraw.Draw(board)
 for n,(key,title,_) in enumerate(group):
  x=n%3*336+8;y=n//3*278+5;d.text((x,y),f'{key.upper()} / {title}',fill='#26333F',font=ImageFont.truetype(F,12));board.paste(Image.open(O/f'{key}-320x240.png'),(x,y+20))
 board.save(O/f'contato-{start//7+1}.png')
print('14 telas 320x240')
