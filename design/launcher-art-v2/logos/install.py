from PIL import Image
from pathlib import Path
O=Path(__file__).parent; T=O.parents[2]/'themes/hands-on-lab'
keys=['nes','snes','gb','gbc','gba','gw','sms','gg','md','col','pce','lnx','a26','msx']
for key in keys:
 im=Image.open(O/f'{key}-320x240.png').convert('RGB');im.quantize(colors=256).save(T/f'pilot_{key}.png',optimize=True)
 source=Image.open(O/'source'/f'{key}-us.png').convert('RGBA');source=source.crop(source.getbbox());source.thumbnail((82,54),Image.Resampling.LANCZOS)
 side=Image.new('RGB',(88,60),'white');side.paste(source,((88-source.width)//2,(60-source.height)//2),source)
 side.quantize(colors=128).save(T/f'pilot_side_{key}.png',optimize=True)
print('Installed',len(keys),'320x240 plates and side art')
