#!/usr/bin/env python3
import hashlib,json,re,subprocess
from pathlib import Path
H=Path(__file__).resolve().parent; C=H/'console'/'controle'; A=H/'console'/'candidata'; checks={}; info={}
def ck(n,v):
 checks[n]=bool(v); print(('ok    ' if v else 'FALHA ')+n)
def lines(p,prefix='CONFIG_'):
 return {x for x in p.read_text().splitlines() if x.startswith(prefix) or x.startswith('# CONFIG_')}
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def section_sizes(p):
 o=subprocess.check_output(['/Users/lucianocaminhajunior/.espressif/tools/xtensa-esp-elf/esp-13.2.0_20240530/xtensa-esp-elf/bin/xtensa-esp32s3-elf-size','-A',str(p)],text=True)
 return {x.split()[0]:int(x.split()[1]) for x in o.splitlines() if len(x.split())>=2 and x.split()[1].isdigit()}
head='afeda48a'; ck('mesmo_head',C.joinpath('HEAD.txt').read_text().startswith(head) and A.joinpath('HEAD.txt').read_text().startswith(head))
sc,sa=lines(C/'sdkconfig'),lines(A/'sdkconfig'); ck('sdkconfig_difere_so_opcao',sc^sa=={'# CONFIG_GBSP_WAITSTATE_COMMON_BASE is not set','CONFIG_GBSP_WAITSTATE_COMMON_BASE=y'})
hc={x for x in (C/'sdkconfig.h').read_text().splitlines() if x.startswith('#define CONFIG_')}; hs={x for x in (A/'sdkconfig.h').read_text().splitlines() if x.startswith('#define CONFIG_')}; ck('sdkconfig_h_difere_so_opcao',hc^hs=={'#define CONFIG_GBSP_WAITSTATE_COMMON_BASE 1'})
ck('v7_preservada',all(x in sa for x in ['CONFIG_GBSP_CPU_ALERT_PER_INSTRUCTION=y','CONFIG_GBSP_THUMB_RESIDENT_PC=y','CONFIG_GBSP_ROM_SECOND_CHANCE=y','CONFIG_GBSP_AUDIO_CLOCK_COMP=y','CONFIG_GBSP_AUDIO_LIVE_RATE=y']))
ck('sem_stats',not any('STATS=y' in x or 'PROFILE=y' in x for x in sa))
for tag,d in [('controle',C),('candidata',A)]:
 log=(d/'build.log').read_text(errors='replace'); ck(tag+'_build_limpo',not re.search(r'(^|\n)(error:|FAILED:|Task failed)',log))
 ck(tag+'_artefatos',all((d/f).is_file() for f in ['gbsp.bin','gbsp.elf','gbsp.map','sdkconfig','sdkconfig.h','cpu.cpp.obj','gba_memory.c.obj']))
 ck(tag+'_particao',len((d/'gbsp.bin').read_bytes())<=0xd0000)
ss_c,ss_a=section_sizes(C/'gbsp.elf'),section_sizes(A/'gbsp.elf'); info['sections']={'controle':ss_c,'candidata':ss_a}
ck('iram_delta_esperado',ss_a.get('.iram0.text')-ss_c.get('.iram0.text')==88)
ck('dram_data_igual',ss_a.get('.dram0.data')==ss_c.get('.dram0.data'))
ck('dram_bss_igual',ss_a.get('.dram0.bss')==ss_c.get('.dram0.bss'))
ck('bin_delta_80',len((A/'gbsp.bin').read_bytes())-len((C/'gbsp.bin').read_bytes())==80)
asm=(H/'analysis'/'obj'/'n2.asm').read_text(); ck('frame_112',bool(re.search(r'<execute_arm>:\n\s*\S+:\s+[0-9a-f ]+\s+entry\s+a1, 112',asm)))
for tag,d in [('controle',C),('candidata',A)]: info[tag]={'bin_size':(d/'gbsp.bin').stat().st_size,'bin_sha256':sha(d/'gbsp.bin'),'elf_sha256':sha(d/'gbsp.elf')}
r=json.loads((H/'thumb-reweight-current.json').read_text()); ck('perfil_cobre_mais_99_9',all(g['coverage_percent']>99.9 for g in r['games'].values()))
ck('thumb_reduz_instrucao',all(g['n2_delta_per_thumb_instruction']['instr']<-1.1 for g in r['games'].values()))
for p in [H/'harness'/'resultados'/'regressao.json',H/'harness'/'resultados'/'regressao-san.json']:
 d=json.loads(p.read_text()); ck('regressao_'+p.stem, len(d)==33 and all(x['igual'] for x in d) and not any(x['asan'] for x in d))
for p in [H/'harness'/'resultados'/'rewind-base.json',H/'harness'/'resultados'/'rewind-on.json']:
 d=json.loads(p.read_text()); ck('rewind_'+p.stem,len(d)==35 and all(x['igual'] for x in d))
out={'tudo_ok':all(checks.values()),'criterios':checks,'info':info}; (H/'verification-console.json').write_text(json.dumps(out,indent=2,ensure_ascii=False)+'\n'); print('tudo_ok',out['tudo_ok']); raise SystemExit(0 if out['tudo_ok'] else 1)
