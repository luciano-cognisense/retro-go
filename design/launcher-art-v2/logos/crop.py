from PIL import Image,ImageChops
from pathlib import Path
P=Path(__file__).parent/'source';im=Image.open(P/'consoles-contact.png').convert('RGBA');w,h=im.size
keys=['gb','gbc','gba','gw','sms','gg','md','col','pce','lnx','a26','msx']
for i,key in enumerate(keys):
 tile=im.crop((i%4*w//4,i//4*h//3,(i%4+1)*w//4,(i//4+1)*h//3)).convert('RGB')
 # Remove white matte to preserve existing white card background.
 mask=ImageChops.difference(tile,Image.new('RGB',tile.size,'white')).convert('L').point(lambda p: 255 if p>28 else 0)
 bbox=mask.getbbox();tile=tile.crop(bbox);mask=mask.crop(bbox);tile.putalpha(mask)
 tile.save(P/f'{key}-us.png')
