#!/usr/bin/env python3
"""Actual baseline/candidate flag macros vs independent 64-bit arithmetic."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2];core=root/'gbsp/components/gbsp-libretro'
old=subprocess.check_output(['git','show','9962eb325:gbsp/components/gbsp-libretro/cpu.cpp'],cwd=root,text=True)
new=(core/'cpu.cpp').read_text()
def macro(text,name):
 lines=text.splitlines(True);i=next(i for i,l in enumerate(lines) if l.startswith('#define '+name+'('));j=i
 while lines[j].rstrip().endswith('\\'):j+=1
 return ''.join(lines[i:j+1])
names=['calculate_z_flag','calculate_n_flag','calculate_c_flag_sub','calculate_v_flag_sub','calculate_v_flag_add','calculate_flags_add','calculate_flags_sub']
src='''#include <stdint.h>
#include <assert.h>
#include <stdio.h>
using u32=uint32_t; using u64=uint64_t; using s64=int64_t;
struct Flags {u32 d,n,z,c,v;};
'''
for label,text in [('baseline',old),('candidate',new)]:
 src+=''.join(macro(text,n) for n in names)
 src+=f'''static Flags {label}(u32 a,u32 b,u32 carry,bool sub) {{
u32 n_flag,z_flag,c_flag,v_flag,d;
if(sub) {{d=a+~b+carry;calculate_flags_sub(d,a,b,carry);}}
else {{d=a+b;c_flag=d<b;d+=carry;c_flag|=d<carry;calculate_flags_add(d,a,b);}}
return {{d,n_flag,z_flag,c_flag,v_flag}};
}}
'''
 src+=''.join('#undef '+n+'\n' for n in names)
src+='''
static unsigned long checks;
static s64 signed_value(u32 x) {return (s64)x-((x>>31)?0x100000000LL:0);}
static void compare(Flags x,Flags y) {assert(x.d==y.d && x.n==y.n && x.z==y.z && x.c==y.c && x.v==y.v);}
static void check(u32 a,u32 b,u32 carry) {
 for(bool sub:{false,true}) {
  s64 signed_result=sub ? signed_value(a)-signed_value(b)-(1-carry) : signed_value(a)+signed_value(b)+carry;
  u64 unsigned_result=sub ? (u64)a-(u64)b-(1-carry) : (u64)a+(u64)b+carry;
  u32 d=(u32)unsigned_result;
  Flags reference={d,d>>31,(u32)(d==0),sub?(u32)((u64)a>=(u64)b+1-carry):(u32)(unsigned_result>>32),
   (u32)(signed_result < -2147483648LL || signed_result > 2147483647LL)};
  compare(baseline(a,b,carry,sub),reference);compare(candidate(a,b,carry,sub),reference);++checks;
 }
}
int main() {
 u32 edge[]={0,1,2,0x7ffffffe,0x7fffffff,0x80000000,0x80000001,0xfffffffe,0xffffffff};
 for(u32 a:edge)for(u32 b:edge)for(u32 c=0;c<2;++c)check(a,b,c);
 for(u32 a=0;a<256;++a)for(u32 b=0;b<256;++b)for(u32 c=0;c<2;++c) {
  check(a,b,c);check(a<<24,b<<24,c);
 }
 u32 seed=0x1234abcd;
 for(unsigned i=0;i<1000000;++i) {
  seed=seed*1664525U+1013904223U;u32 a=seed;
  seed=seed*1664525U+1013904223U;u32 b=seed;
  check(a,b,0);check(a,b,1);
 }
 printf("PASS: %lu arithmetic cases; baseline and candidate vs independent 64-bit N/Z/C/V oracle\\n",checks);
}
'''
src=src.replace('#include <stdio.h>','#include <stdio.h>\n#include <initializer_list>')
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'flags.cpp').write_text(src)
 subprocess.run(['c++','-std=c++11','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'flags.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
