from pathlib import Path
from urllib.parse import urlencode
from urllib.request import Request, urlopen
import json
D=Path(__file__).parent/'source';D.mkdir(exist_ok=True)
names={
'gb':'Gameboy logo.svg','gbc':'Game Boy Color logo.svg','gba':'Game Boy Advance logo.svg',
'gw':'Game and watch logo.svg','sms':'Sega-master-system-logo.png',
'gg':'Game Gear logo Sega.png','md':'MegaDriveJPLogo.svg',
'col':'COLECO VISION LOGO (SINGLE LINE).svg','pce':'PC engine logo red.svg',
'lnx':'Atari Lynx logo.svg','a26':'Atari2600logo.svg','msx':'MSX-Logo.svg'}
for key,name in names.items():
 if key not in ('pce','lnx','a26','msx'): continue
 query=urlencode({'action':'query','format':'json','prop':'imageinfo','iiprop':'url','titles':'File:'+name})
 req=Request('https://commons.wikimedia.org/w/api.php?'+query,headers={'User-Agent':'HandsOnLabLauncherDesign/0.1 (art research)'})
 try:
  data=json.load(urlopen(req,timeout=20));page=next(iter(data['query']['pages'].values()));url=page['imageinfo'][0]['url'];ext='svg' if name.endswith('.svg') else 'png'
  target=D/f'{key}-logo.{ext}';target.write_bytes(urlopen(Request(url,headers={'User-Agent':'HandsOnLabLauncherDesign/0.1'}),timeout=20).read());print(key,len(target.read_bytes()),url)
 except Exception as e:print('FAILED',key,str(e))
