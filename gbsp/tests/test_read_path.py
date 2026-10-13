#!/usr/bin/env python3
"""Differential test of scalar read macro against C (0698b9b4).

Handlers are spies: verifies identical dispatch/arguments, not the GBA devices.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]; core=root/'gbsp/components/gbsp-libretro'
base=subprocess.check_output(['git','show','0698b9b4:gbsp/components/gbsp-libretro/cpu.cpp'],cwd=root,text=True)
def macro(text,name):
 lines=text.splitlines(True); first=next(i for i,l in enumerate(lines) if l.startswith('#define '+name+'(')); end=first
 while lines[end].rstrip().endswith('\\'):end+=1
 return ''.join(lines[first:end+1])
common=(core/'common.h').read_text();access=common[common.index('#define address8('):common.index('#define read_ioreg(')]
src=r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
using s8=int8_t; using s16=int16_t;
alignas(4) static u8 data[0x10000];
static u8 *memory_map_read[8192];
static u8 ws_cyc_nseq[16][2];
#define WS_NSEQ(r,m) (ws_cyc_nseq[(r)][(m)])
static u32 reg[16], seen, lastRegion, typeId;
static int cycles_remaining, calls, stats, evaluations;
#define REG_PC 15
#define aligned_address_mask8 0xF0000000
#define aligned_address_mask16 0xF0000001
#define aligned_address_mask32 0xF0000003
#define TYPE_u8 1
#define TYPE_s8 2
#define TYPE_u16 3
#define TYPE_s16 4
#define TYPE_u32 5
#define STATS_MEMORY_ACCESS(op,type,region) do { ++stats; lastRegion=region; typeId=TYPE_##type; } while(0)
static u32 handler(u32 address) { ++calls; seen=address; return address ^ 0x8a80ff81; }
static u32 signed_handler(u32 address) { ++calls; seen=address; return address ^ 0xfe12ab81; }
static u32 once(u32 address) { ++evaluations; return address; }
struct Result { u32 value,address,region,type; int cycles,calls,stats,evaluations; };
'''+access+macro(base,'fast_read_memory')+'\n#include "read_path.h"\n'
types=[(8,'u8','handler'),(8,'s8','handler'),(16,'u16','handler'),(16,'s16','signed_handler'),(32,'u32','handler')]
for name,load in [('old','fast_read_memory'),('candidate','candidate_read')]:
 if name=='candidate':src+='''
#if GBSP_COMPACT_READS_ACTIVE
#define candidate_read gbsp_compact_read
#else
#define candidate_read fast_read_memory
#endif
'''
 src+=f'static Result {name}(u32 address, u32 pc, int kind) {{\n'
 src+='u32 value=0; cycles_remaining=100; calls=stats=evaluations=0; seen=lastRegion=typeId=0; reg[REG_PC]=pc;\nswitch(kind) {\n'
 for i,(size,t,h) in enumerate(types):src+=f'case {i}: {{ {load}({size}, {t}, once(address), value, {h}); break; }}\n'
 src+='} return {value,seen,lastRegion,typeId,cycles_remaining,calls,stats,evaluations}; }\n'
src+=r'''
static unsigned tests;
static void check(u32 addr,u32 pc) {
 for(int kind=0;kind<5;++kind) {
  Result a=old(addr,pc,kind),b=candidate(addr,pc,kind);
  assert(a.value==b.value && a.address==b.address && a.region==b.region && a.type==b.type);
  assert(a.cycles==b.cycles && a.calls==b.calls && a.stats==b.stats && a.evaluations==1 && b.evaluations==1);
  ++tests;
 }
}
int main() {
 for(unsigned i=0;i<sizeof(data);++i)data[i]=(i*37+129)&255;
 for(unsigned i=0;i<8192;++i)memory_map_read[i]=(i%3==0)?nullptr:data+(i%2)*0x8000;
 for(unsigned i=0;i<16;++i)for(unsigned j=0;j<2;++j)ws_cyc_nseq[i][j]=i+j+1;
 u32 pcs[]={0,0x3fff,0x4000,0x8000000};
 for(u32 pc:pcs) {
  for(u32 region=0;region<256;++region) {
   u32 offsets[]={0,1,2,3,0x3fff,0x4000,0x7ffc,0x7ffd,0x7ffe,0x7fff,0x8000,0xfffffc,0xffffff};
   for(u32 offset:offsets)check((region<<24)|offset,pc);
  }
  u32 state=0xabc123;
  for(unsigned i=0;i<10000;++i) { state=state*1664525+1013904223;check(state,pc);check(state&0x0fffffff,pc); }
 }
 printf("PASS: %u differential scalar reads; active=%d\n",tests,GBSP_COMPACT_READS_ACTIVE);
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(src)
 for defs in [[],['-DCONFIG_GBSP_COMPACT_READS=1'],['-DCONFIG_GBSP_COMPACT_READS=1','-DHAVE_DYNAREC']]:
  subprocess.run(['c++','-std=c++11','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DRETRO_GO',*defs,'-I',str(core),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
