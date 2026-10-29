from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
D=Path(__file__).parent
F='/System/Library/Fonts/Supplemental/'
styles={'pce':('PC ENGINE','Arial Black.ttf','#D9362B'), 'lnx':('ATARI LYNX','Arial Bold Italic.ttf','#202A35'), 'a26':('ATARI 2600','Arial Bold.ttf','#202A35'), 'msx':('MSX','Arial Black.ttf','#262B5C')}
for k,(name,face,color) in styles.items():
 im=Image.new('RGBA',(600,140));d=ImageDraw.Draw(im);font=ImageFont.truetype(F+face,76)
 d.text((300,63),name,font=font,fill=color,anchor='mm',stroke_width=0)
 box=im.getbbox();im.crop(box).save(D/f'{k}-logo.png')
