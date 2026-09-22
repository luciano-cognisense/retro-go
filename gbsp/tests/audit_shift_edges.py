#!/usr/bin/env python3
"""Reproduce pre-existing shift findings; EXPECTED failures, no emulator edit."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
base='9962eb325'
def read(name):return subprocess.check_output(['git','show',f'{base}:gbsp/components/gbsp-libretro/{name}'],cwd=root,text=True)
def macro(s,name):
 lines=s.splitlines(True);i=next(i for i,l in enumerate(lines) if l.startswith('#define '+name+'('));j=i
 while lines[j].rstrip().endswith('\\'):j+=1
 return ''.join(lines[i:j+1])
s=read('cpu.cpp');common=read('common.h')
src='''#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
using u32=uint32_t;using s32=int32_t;
static u32 reg[16];
'''+macro(common,'ror')
for op in ['lsl','lsr','asr','ror']:
 src+=macro(s,f'thumb_shift_{op}_reg')
 src+=f'''static void run_{op}(u32 count) {{u32 rd=0,rs=1,c_flag=1;reg[rd]=0x80000001;reg[rs]=count;
thumb_shift_{op}_reg();printf("{op} count=%u value=%08x carry=%u\\n",count,dest,c_flag);}}
'''
src+='''int main(int argc,char**argv){if(argc!=3)return 2;u32 count=(u32)strtoul(argv[2],nullptr,0);
switch(argv[1][0]){case 'l':run_lsl(count);break;case 'r':run_lsr(count);break;case 'a':run_asr(count);break;case 'o':run_ror(count);break;} }
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'shifts.cpp').write_text(src)
 subprocess.run(['c++','-std=c++11','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=undefined',str(p/'shifts.cpp'),'-o',str(p/'test')],check=True)
 for op in ['l','r','a']:
  got=subprocess.check_output([str(p/'test'),op,'256'],text=True).strip();print('PRE-EXISTING:',got,'; reference for low byte zero: value=80000001 carry=1')
  assert 'value=80000001 carry=1' not in got
 for count in [32,33,255]:
  r=subprocess.run([str(p/'test'),'o',str(count)],capture_output=True,text=True)
  assert r.returncode!=0 and 'shift exponent' in r.stderr
  print(f'PRE-EXISTING: ROR count={count}:',r.stderr.splitlines()[0])
print('Reproducers confirmed expected baseline defects; source left unchanged.')
