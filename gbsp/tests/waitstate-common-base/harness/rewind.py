#!/usr/bin/env python3
"""Teste diferencial de save/load com WAITCNT diferente.
Para cada ROM e par (N, M), N < M:
  referencia : sim --mark N         (roda direto; zera hashes no inicio do quadro N)
  volta      : sim --rewind N M     (salva no inicio de N, segue ate M -- o WAITCNT muda no caminho --, carrega o
                                     estado de N e zera hashes; os quadros seguintes recebem os rotulos N, N+1, ...)
Restauracao correta  <=>  as linhas apos '# acumuladores zerados' sao identicas (hash do estado inteiro, hash do
rastreio por instrucao -- PC, modo, ciclos restantes, flags e r0-r15 --, alertas e contagens), por K quadros.
Uso: rewind.py <variante> [K]  -> resultados/rewind-<variante>.json"""
import json, os, re, subprocess, sys
H = os.path.dirname(os.path.abspath(__file__)); V = sys.argv[1]; K = int(sys.argv[2]) if len(sys.argv) > 2 else 40
BIN = os.path.join(H, 'bin', f'sim-{V}'); os.makedirs(os.path.join(H, 'resultados'), exist_ok=True)
def sym(rom):
    p = os.path.join(H, 'rom', rom.replace('.gba', '.sym'))
    return {k: int(v, 16) for k, v in (l.strip().split('=') for l in open(p))} if os.path.exists(p) else {}
ROMS = ['waitcnt-1.gba', 'waitcnt-0x2B.gba', 'waitcnt-0x9D.gba', 'alert-1.gba', 'stress-1.gba']
PARES = [(3, 4), (5, 9), (10, 11), (12, 30), (7, 23), (20, 21), (33, 34)]
def run(rom, opts, frames):
    s = sym(rom); idle = ['--idle', hex(s['idle_wait'])] if 'idle_wait' in s else []
    p = subprocess.run([BIN, os.path.join(H, 'rom', rom), str(frames)] + idle + opts, capture_output=True, text=True)
    out = p.stdout.split('# acumuladores zerados\n', 1)
    return p.returncode, (out[1] if len(out) == 2 else None), p.stderr[-500:]
res = []
for rom in ROMS:
    for n, m in PARES:
        ra, a, ea = run(rom, ['--mark', str(n)], n + K)
        rb, b, eb = run(rom, ['--rewind', str(n), str(m)], m + K)
        la = [l for l in (a or '').splitlines() if l[:1].isdigit()]; lb = [l for l in (b or '').splitlines() if l[:1].isdigit()]
        first = next((i for i, (x, y) in enumerate(zip(la, lb)) if x != y), None)
        d = dict(rom=rom, N=n, M=m, rc=[ra, rb], quadros=[len(la), len(lb)], igual=(a is not None and a == b and ra == rb == 0))
        if first is not None:
            x, y = la[first].split(), lb[first].split()
            d['primeiro_quadro_divergente'] = int(x[0])
            d['diverge_em'] = [k for k, i in (('estado', 1), ('rastreio', 2), ('alertas', 3)) if x[i] != y[i]]
            d['instr_ref_x_volta'] = [x[4], y[4]]
        res.append(d)
        print(f"{rom:18s} N={n:2d} M={m:2d} {'IGUAL' if d['igual'] else 'DIVERGE quadro %s em %s' % (d.get('primeiro_quadro_divergente'), d.get('diverge_em'))}")
json.dump(res, open(os.path.join(H, 'resultados', f'rewind-{V}.json'), 'w'), indent=1)
print(V, 'iguais', sum(r['igual'] for r in res), 'de', len(res))
