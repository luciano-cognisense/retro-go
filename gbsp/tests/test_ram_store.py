#!/usr/bin/env python3
"""Differential host test of the real store macros vs frozen baseline 35e4294.

Extracts original RAM switch cases as oracle; non-RAM handlers are spies.
Does not emulate a game or claim timing/performance accuracy on Xtensa.
"""
from pathlib import Path
import os
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[2]
CORE = REPO / 'gbsp/components/gbsp-libretro'
BASE = '35e4294'

def baseline(name):
    return subprocess.check_output(['git', 'show', f'{BASE}:gbsp/components/gbsp-libretro/{name}'], cwd=REPO, text=True)

def macro(source, name):
    lines = source.splitlines(True)
    start = next(i for i, line in enumerate(lines) if line.startswith('#define ' + name + '('))
    end = start
    while lines[end].rstrip().endswith('\\'):
        end += 1
    return ''.join(lines[start:end + 1])

common = (CORE / 'common.h').read_text()
access = common[common.index('#define address8('):common.index('#define  readaddress8(')]
mem = baseline('gba_memory.c')
cases = mem[mem.index('    case 0x02:', mem.index('#define write_memory(type)')):mem.index('    case 0x04:', mem.index('#define write_memory(type)'))]
cases = cases.replace('\\\n', '\n')
old = baseline('cpu.cpp')
new = (CORE / 'cpu.cpp').read_text()
source = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
alignas(4) static u8 ewram[0x80000], iwram[0x10000];
static u8 *memory_map_read[8192];
static u8 expectedE[sizeof ewram], expectedI[sizeof iwram];
static int cpu_alert, cycles_remaining, calls, evaluations, stats;
static u32 seenAddress, seenValue; static int seenSize;
static u8 ws_cyc_nseq[16][2], ws_cyc_seq[16][2];
#define STATS_MEMORY_ACCESS(op,type,region) (++stats)
#define aligned_address_mask8 0xF0000000
#define aligned_address_mask16 0xF0000001
#define aligned_address_mask32 0xF0000003
'''
source += access
source += macro(mem, 'map_region')
for size in (8,16,32):
    body=cases.replace('address##type', f'address{size}').replace('eswap##type', f'eswap{size}')
    source += f'''static int write_memory{size}(u32 address, u{size} value) {{
++calls; seenAddress=address; seenValue=value; seenSize={size};
switch(address >> 24) {{
{body}
default: return 8;
}} return 0;
}}
'''
source += 'static u32 next_value(u32 v) { ++evaluations; return v; }\n'
for name, cpu in [('reference',old),('candidate',new)]:
    if name=='candidate': source += f'#include "{CORE / "ram_store.h"}"\n'
    for m in ['fast_write_memory','store_aligned32']:
        source += macro(cpu,m)
    source += f'''static void {name}(int size, bool seq, u32 address, u32 value) {{
if (seq) {{ store_aligned32(address, next_value(value)); }}
else switch(size) {{
case 8: fast_write_memory(8,u8,address,next_value(value)); break;
case 16: fast_write_memory(16,u16,address,next_value(value)); break;
case 32: fast_write_memory(32,u32,address,next_value(value)); break;
}}
}}
#undef fast_write_memory
#undef store_aligned32
'''
source += r'''
static unsigned checked=0;
static void reset() {
memset(ewram,0xA5,sizeof ewram); memset(iwram,0x5A,sizeof iwram);
cpu_alert=2; cycles_remaining=100; calls=evaluations=stats=0;
seenAddress=seenValue=0; seenSize=0;
}
static void check(int size, bool seq, u32 address, u32 value) {
reset(); reference(size,seq,address,value);
memcpy(expectedE,ewram,sizeof ewram); memcpy(expectedI,iwram,sizeof iwram);
const int expectedAlert=cpu_alert, expectedCycles=cycles_remaining, expectedStats=stats;
const u32 ea=seenAddress, ev=seenValue; const int es=seenSize;
assert(calls==1 && evaluations==1);
reset(); candidate(size,seq,address,value);
assert(memcmp(expectedE,ewram,sizeof ewram)==0);
assert(memcmp(expectedI,iwram,sizeof iwram)==0);
assert(cpu_alert==expectedAlert && cycles_remaining==expectedCycles && stats==expectedStats);
assert(evaluations==1);
const bool bypass=GBSP_FAST_RAM_WRITES_ACTIVE && ((address>>24)==2 || (address>>24)==3);
assert(calls==(bypass ? 0 : 1));
if (!bypass) assert(seenAddress==ea && seenValue==ev && seenSize==es);
++checked;
}
int main() {
unsigned map_offset;
map_region(read, 0x2000000, 0x3000000, 8, ewram);
map_region(read, 0x3000000, 0x4000000, 1, &iwram[0x8000 * SMC_DETECTION]);
for(int r=0;r<16;++r) for(int s=0;s<2;++s) {
ws_cyc_nseq[r][s]=1+r+s; ws_cyc_seq[r][s]=2+r+2*s;
}
const u32 offsets[]={0,1,2,3,0x7ffc,0x7fff,0x8000,0x8001,0x3fffc,0x3ffff,0x40000,0xfffffc,0xffffff};
const u32 regions[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,0xff};
for(u32 r:regions) for(u32 off:offsets) for(int size:{8,16,32}) {
u32 address=(r<<24)|off;
check(size,false,address,0xfedcba98);
if(size==32) check(32,true,address&~3U,0xffffffff);
}
u32 rng=0x12345678;
for(int i=0;i<2048;++i) {
rng=rng*1664525U+1013904223U;
u32 address=((i%2+2)<<24)|(rng&0xffffff);
check(8,false,address,rng); check(16,false,address,rng); check(32,false,address,rng);
check(32,true,address&~3U,rng);
}
printf("PASS: %u differential stores; active=%d, SMC=%d\n",checked,GBSP_FAST_RAM_WRITES_ACTIVE,SMC_DETECTION);
}
'''
source = '#include <initializer_list>\n' + source
with tempfile.TemporaryDirectory(prefix='gbsp-ram-test-') as tmp:
    path=Path(tmp)/'test.cpp'; path.write_text(source)
    for label, flags in [('off',['-DSMC_DETECTION=0']),('on',['-DSMC_DETECTION=0','-DCONFIG_GBSP_FAST_RAM_WRITES=1']),('smc',['-DSMC_DETECTION=1','-DCONFIG_GBSP_FAST_RAM_WRITES=1']),('dynarec',['-DSMC_DETECTION=0','-DHAVE_DYNAREC=1','-DCONFIG_GBSP_FAST_RAM_WRITES=1'])]:
        exe=Path(tmp)/label
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DRETRO_GO=1',*flags,str(path),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
