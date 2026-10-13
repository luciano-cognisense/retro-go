#!/usr/bin/env python3
"""Check iterator exhaustively, then compare real ARM/Thumb transfer templates.
Memory/mode helpers are deterministic spies; not a complete GBA CPU test.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2];core=root/'gbsp/components/gbsp-libretro'
old=subprocess.check_output(['git','show','0698b9b4:gbsp/components/gbsp-libretro/cpu.cpp'],cwd=root,text=True)
new=(core/'cpu.cpp').read_text()
def block(s):return s[s.index('typedef enum { AccLoad'):s.index('#define arm_swap(type)')]
src=r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
using u32=uint32_t; using s32=int32_t; using cpu_alert_type=u32;
static u32 reg[32], bank[7], cpu_modes[32], bit_count[256];
static unsigned mode_calls, event_count; static u32 mode_trace[4];
struct Event {u32 address,value,write;}; static Event events[16];
#define CPU_ALERT_NONE 0
#define REG_PC 15
#define PCR (reg[REG_PC])
#define REG_LR 14
#define REG_CPSR 16
#define MODE_USER 0
#define using_register(a,b,c)
#define using_register_list(a,b,c)
#define arm_pc_offset(v) (reg[REG_PC] += (v))
#define thumb_pc_offset(v) (reg[REG_PC] += (v))
static void set_cpu_mode(u32 mode) {
 assert(mode_calls<4);mode_trace[mode_calls++]=mode;
 for(unsigned i=0;i<7;++i) { u32 tmp=reg[8+i];reg[8+i]=bank[i];bank[i]=tmp; }
}
static u32 read_spy(u32 a,s32 &cycles) {
 assert(event_count<16);cycles-=1+(a>>24)%7;
 u32 v=a*1664525U+event_count*1013904223U;
 events[event_count++]={a,v,0};return v;
}
static u32 write_spy(u32 a,u32 v,s32 &cycles) {
 assert(event_count<16);cycles-=1+(a>>24)%7;
 events[event_count++]={a,v,1};return 1U<<((a>>2)%4);
}
#define load_aligned32(a,d) do { (d)=read_spy((a),cycles_remaining); } while(0)
#define store_aligned32(a,v) do {cpu_alert |= write_spy((a),(v),cycles_remaining);} while(0)
#include "register_list.h"
'''
src+='namespace reference {\n'+block(old)+'}\nnamespace candidate {\n'+block(new)+'}\n'
src+=r'''
static unsigned checks;
struct Snapshot {u32 regs[32],banks[7],modes[4];Event trace[16];unsigned count,mc;u32 alert;s32 cycles;};
static void init(u32 seed,u32 base,u32 rn) {
 for(auto &r:reg){seed=seed*1664525U+1013904223U;r=seed;}
 for(auto &r:bank){seed=seed*1664525U+1013904223U;r=seed;}
 reg[rn]=base;reg[REG_CPSR]=0x10|(seed&15);mode_calls=event_count=0;
 memset(events,0,sizeof(events));memset(mode_trace,0,sizeof(mode_trace));
}
static Snapshot snap(u32 alert,s32 cycles) {
 Snapshot s={};memcpy(s.regs,reg,sizeof(reg));memcpy(s.banks,bank,sizeof(bank));
 memcpy(s.modes,mode_trace,sizeof(mode_trace));memcpy(s.trace,events,sizeof(events));
 s.count=event_count;s.mc=mode_calls;s.alert=alert;s.cycles=cycles;return s;
}
static void compare(const Snapshot&a,const Snapshot&b) {
 assert(memcmp(a.regs,b.regs,sizeof(reg))==0 && memcmp(a.banks,b.banks,sizeof(bank))==0);
 assert(memcmp(a.modes,b.modes,sizeof(mode_trace))==0 && memcmp(a.trace,b.trace,sizeof(events))==0);
 assert(a.count==b.count && a.mc==b.mc && a.alert==b.alert && a.cycles==b.cycles);++checks;
}
static void run(unsigned seed,u32 base,u32 rn,u32 mask) {
'''
for mode in ['AccLoad','AccStore']:
 for addr in ['AddrPreInc','AddrPreDec','AddrPostInc','AddrPostDec']:
  for wb in ['false','true']:
   for sb in ['false','true']:
    args=lambda ns:f'{ns}::{mode},{wb},{sb},{ns}::{addr}'
    src+=f'''{{init(seed,base,rn);s32 cycles=100;u32 alert=reference::exec_arm_block_mem<{args('reference')}>(rn,mask,cycles);Snapshot a=snap(alert,cycles);
init(seed,base,rn);cycles=100;alert=candidate::exec_arm_block_mem<{args('candidate')}>(rn,mask,cycles);compare(a,snap(alert,cycles));}}\n'''
  high='REG_PC' if mode=='AccLoad' else 'REG_LR'
  args=lambda ns:f'{ns}::{mode},{ns}::{addr}'
  src+=f'''{{u32 tm=(mask&255)|((mask&256)?1U<<{high}:0);init(seed,base,rn);s32 cycles=100;u32 alert=reference::exec_thumb_block_mem<{args('reference')}>(rn,tm,cycles);Snapshot a=snap(alert,cycles);
init(seed,base,rn);cycles=100;alert=candidate::exec_thumb_block_mem<{args('candidate')}>(rn,tm,cycles);compare(a,snap(alert,cycles));}}\n'''
src+=r'''
}
int main() {
 for(u32 i=0;i<256;++i)bit_count[i]=__builtin_popcount(i);
 for(u32 i=0;i<32;++i)cpu_modes[i]=i;
 for(u32 mask=0;mask<65536;++mask) {
  unsigned seen=0,last=0;
  gbsp_for_register(i,mask,16) {assert(mask&(1U<<i));assert(!seen || i>last);last=i;seen|=1U<<i;}
  assert(seen==mask);seen=0;last=0;
  gbsp_for_register(i,mask,8) {assert(i<8 && (mask&(1U<<i)));assert(!seen || i>last);last=i;seen|=1U<<i;}
  assert(seen==(mask&255));
 }
 u32 masks[]={0,1,2,3,0x80,0x100,0xff,0x1ff,0x4000,0x8000,0xffff,0x5555,0xaaaa};
 u32 bases[]={0,1,3,0x02000000,0x02007ffc,0x0203fffc,0x03007ffc,0x03fffffc,0x04000000,0x08007ffc,0x0ffffffc,0xfffffffc,0xffffffff};
 for(u32 mask:masks)for(u32 base:bases)for(u32 rn=0;rn<16;++rn)run(mask+base,base,rn,mask);
 u32 seed=1;
 for(unsigned i=0;i<1000;++i) {seed=seed*1664525U+1013904223U;run(seed,seed,seed%16,(seed>>16)&0xffff);}
 printf("PASS: 65536 masks x 2 widths; %u transfer comparisons; active=%d\n",checks,GBSP_SPARSE_REGISTERS_ACTIVE);
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(src)
 for defs in [[],['-DCONFIG_GBSP_SPARSE_REGISTERS=1'],['-DCONFIG_GBSP_SPARSE_REGISTERS=1','-DHAVE_DYNAREC']]:
  subprocess.run(['c++','-std=c++11','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DRETRO_GO',*defs,'-I',str(core),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
