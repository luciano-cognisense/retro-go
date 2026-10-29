from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
O=Path(__file__).parent;T=O.parents[2]/'themes/hands-on-lab'
FONT='/System/Library/Fonts/Supplemental/Arial Bold.ttf'
S=4
for key,title in [('favorite','FAVORITOS'),('recent','RECENTES')]:
 base=Image.open(O/'nes-320x240.png').convert('RGB').resize((320*S,240*S),Image.Resampling.NEAREST)
 d=ImageDraw.Draw(base)
 d.rounded_rectangle((73*S,42*S,246*S,183*S),radius=9*S,fill='white',outline='#D9DDD9',width=S)
 d.rounded_rectangle((139*S,40*S,180*S,43*S),radius=S,fill='#BD5C23')
 # Original, abstract symbols shared with the existing heart and clock semantics.
 if key=='favorite':
  d.polygon([(160*S,139*S),(120*S,106*S),(115*S,99*S),(115*S,86*S),(123*S,78*S),(139*S,78*S),(160*S,94*S),(181*S,78*S),(197*S,78*S),(205*S,86*S),(205*S,99*S),(200*S,106*S)],fill='#BD5C23')
  d.polygon([(160*S,130*S),(125*S,102*S),(123*S,96*S),(123*S,89*S),(129*S,85*S),(139*S,85*S),(160*S,103*S),(181*S,85*S),(191*S,85*S),(197*S,89*S),(197*S,96*S),(195*S,102*S)],fill='white')
  d.rectangle((139*S,105*S,144*S,122*S),fill='#26333F');d.rectangle((133*S,111*S,150*S,116*S),fill='#26333F')
  for x,y in [(178,109),(185,116),(170,116),(178,123)]:d.ellipse(((x-3)*S,(y-3)*S,(x+3)*S,(y+3)*S),fill='#BD5C23')
 else:
  d.arc((111*S,76*S,209*S,148*S),45,350,fill='#BD5C23',width=7*S)
  d.polygon([(111*S,82*S),(112*S,104*S),(129*S,91*S)],fill='#BD5C23')
  d.ellipse((136*S,88*S,184*S,136*S),outline='#26333F',width=5*S)
  d.line((160*S,112*S,160*S,97*S),fill='#26333F',width=5*S)
  d.line((160*S,112*S,174*S,119*S),fill='#26333F',width=5*S)
  d.ellipse((155*S,107*S,165*S,117*S),fill='#BD5C23')
 font=ImageFont.truetype(FONT,13*S)
 d.text((160*S,158*S),title,font=font,fill='#26333F',anchor='mm')
 im=base.resize((320,240),Image.Resampling.LANCZOS)
 im.save(O/f'{key}-320x240.png');im.quantize(colors=256).save(T/f'pilot_{key}.png',optimize=True)
 side=Image.new('RGB',(88*S,60*S),'white');sd=ImageDraw.Draw(side)
 if key=='favorite':
  sd.polygon([(44*S,50*S),(17*S,29*S),(16*S,18*S),(23*S,12*S),(35*S,12*S),(44*S,22*S),(53*S,12*S),(65*S,12*S),(72*S,18*S),(71*S,29*S)],fill='#BD5C23')
  sd.polygon([(44*S,44*S),(23*S,27*S),(22*S,20*S),(27*S,17*S),(35*S,17*S),(44*S,28*S),(53*S,17*S),(61*S,17*S),(66*S,20*S),(65*S,27*S)],fill='white')
 else:
  sd.ellipse((21*S,7*S,67*S,53*S),outline='#BD5C23',width=5*S)
  sd.line((44*S,30*S,44*S,17*S),fill='#26333F',width=4*S);sd.line((44*S,30*S,56*S,35*S),fill='#26333F',width=4*S)
 side=side.resize((88,60),Image.Resampling.LANCZOS)
 side.quantize(colors=128).save(T/f'pilot_side_{key}.png',optimize=True)
print('Collection cards and side symbols created')
