#!/usr/bin/env python3
"""Regressao base x correcao sem volta no tempo: a saida inteira (hash de estado, rastreio por instrucao, alertas e
contagens a cada quadro) tem de ser identica. Inclui save/load no mesmo quadro (o WAITCNT do estado e o vigente: a
correcao recalcula as mesmas tabelas), reset e bloco longo. Uso: regressao.py [prefixo] -> resultados/regressao*.json"""
import json, os, subprocess, sys
H = os.path.dirname(os.path.abspath(__file__)); P = sys.argv[1] if len(sys.argv) > 1 else ''
def sym(rom):
    p = os.path.join(H, 'rom', rom.replace('.gba', '.sym'))
    return {k: int(v, 16) for k, v in (l.strip().split('=') for l in open(p))} if os.path.exists(p) else {}
ROMS = ['waitcnt-1.gba', 'waitcnt-0x2B.gba', 'waitcnt-0x9D.gba', 'alert-1.gba', 'alert-0x2B.gba', 'stress-1.gba', 'stress-0x11.gba', 'stress-0x5F.gba', 'stress-0xE7.gba']
SC = []
for r in ROMS:
    idle = ['--idle', hex(sym(r)['idle_wait'])] if 'idle_wait' in sym(r) else []
    SC += [(f'{r}:normal', r, 300, idle), (f'{r}:sem-idle', r, 300, []) if idle else None,
           (f'{r}:save-load', r, 300, idle + ['--save-load', '17']), (f'{r}:reset', r, 300, idle + ['--reset', '23'])]
SC = [s for s in SC if s]
SC.append(('waitcnt-1.gba:longo', 'waitcnt-1.gba', 6000 if not P else 1000, ['--idle', hex(sym('waitcnt-1.gba')['idle_wait'])]))
def run(v, s):
    p = subprocess.run([os.path.join(H, 'bin', f'sim-{P}{v}'), os.path.join(H, 'rom', s[1]), str(s[2])] + s[3], capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr
out = []
for s in SC:
    ra, a, ea = run('base', s); rb, b, eb = run('fix', s)
    ub = lambda e: sorted(set(l.split('runtime error')[0].split('src-')[-1].split('/', 1)[-1] + 'runtime error' + l.split('runtime error')[1] for l in e.splitlines() if 'runtime error' in l))
    out.append(dict(cenario=s[0], igual=(a == b and ra == rb == 0), final=a.splitlines()[-1] if a else '', rc=[ra, rb],
                    asan=('AddressSanitizer' in ea + eb), ub_base=ub(ea), ub_fix=ub(eb)))
    print(f"{s[0]:28s} {'IGUAL' if out[-1]['igual'] else 'DIFERE'}  {out[-1]['final'][:70]}")
json.dump(out, open(os.path.join(H, 'resultados', f'regressao{("-" + P.rstrip("-")) if P else ""}.json'), 'w'), indent=1)
print('iguais', sum(o['igual'] for o in out), 'de', len(out))
