#!/usr/bin/env python3
"""Gate 1: acessos a pilha (por slot) no caminho comum de cada instrucao emulada, ponderados pelos perfis reais.

Caminhos (objeto real cpu.cpp, mesmo analisador da frente de despacho):
  ARM executada  = Pass(c) [entrada da condicao c -> jx principal] + A(h) [entrada do handler h -> jx da condicao seguinte]
  ARM rejeitada  = Fail(c) [entrada da condicao c -> jx da condicao seguinte, sem passar pelo despacho principal]
  Thumb          = T(h)    [entrada do handler h -> jx Thumb seguinte]
Pesos: arm-condition-profile.txt (arm_mix_v1: ARM/Thumb; arm_condition: emitidas/executadas por condicao;
arm_handler: execucoes por indice ARM). Thumb nao tem histograma por indice nos perfis: media simples dos handlers,
com mediana e quartis para mostrar a dispersao.
Slots (DWARF da ELF final, frame de 112 B): a1+0 cpu_alert, +8 c_flag, +16 z_flag, +20 n_flag, +24 v_flag;
os demais offsets sao temporarios/constantes guardados pelo alocador (ex.: +4 base de ws_cyc_seq, +44 &idle_loop_target_pc)."""
import collections, heapq, json, os, re, statistics as st, sys
H = os.path.dirname(os.path.abspath(__file__))
TD = os.path.join(H, '..', 'gbsp-threaded-dispatch')
sys.path.insert(0, os.path.join(TD, 'real')); sys.path.insert(0, os.path.join(TD, 'micro'))
from paths import parse, literal_targets, section_words
import real_paths as RP
SLOT = {0: 'cpu_alert', 8: 'c_flag', 16: 'z_flag', 20: 'n_flag', 24: 'v_flag'}

def load(obj):
    ins, order = parse(obj[:-2] + '.asm'); pos = {a: i for i, a in enumerate(order)}
    lits = literal_targets(obj, '.iram1.0.literal'); words = {}
    def tbl(a, size):
        _, b = RP.backtrace(ins, order, pos, a)
        if b is None or ins[b]['op'] != 'l32r' or ins[b]['lit'] not in lits: return None
        sec, off = lits[ins[b]['lit']]
        if sec not in words: words[sec] = section_words(obj, sec)
        try: return [words[sec](off + 4 * n) for n in range(size)]
        except Exception:   # tabela menor que 16 (sub-switch curto): le o que existir
            t = []
            for n in range(size):
                try: t.append(words[sec](off + 4 * n))
                except Exception: break
            return t or None
    jx = {a: RP.kind(ins, order, pos, a) for a in order if ins[a]['op'] == 'jx'}
    tables = {a: tbl(a, 256 if k in ('arm', 'thumb') else 16) for a, k in jx.items()}
    return ins, order, jx, tables

def acc(o):
    m = re.search(r'\ba1, (\d+)$', o['args'])
    if not m: return None
    if o['op'][:3] in ('l32', 'l8u', 'l16'): return ('ld', int(m[1]))
    if o['op'][:3] in ('s32', 's8i', 's16'): return ('st', int(m[1]))
    return None

def path(ins, jx, tables, start, stop, avoid=()):
    """Dijkstra do caminho comum (chamadas pesam 40) de start ate um jx do tipo `stop`; `avoid` = tipos de jx proibidos."""
    def succ(a):
        i = ins[a]; op = i['op']; nxt = a + i['len']
        m = re.search(r'([0-9a-f]+) <', i['args']); tgt = int(m[1], 16) if m else None
        if op == 'j': return [(tgt, True, 0)]
        if op.startswith('ret'): return []
        if op == 'jx':
            k = jx[a]; t = tables.get(a)
            if k == stop or k in avoid: return 'STOP' if k == stop else []
            if k in ('arm', 'thumb'): return []
            if not t: return []
            if k == 'cond': return [(t[14], True, 0)]
            return [(x, True, 0) for x in set(t)]
        if op.startswith('call'): return [(nxt, False, 40)]
        if op.startswith('b') and tgt is not None and op != 'break': return [(nxt, False, 0), (tgt, True, 0)]
        return [(nxt, False, 0)]
    dist = {start: 0}; prev = {}; pq = [(0, start)]
    while pq:
        d, a = heapq.heappop(pq)
        if d > dist.get(a, 1e18) or a not in ins: continue
        s = succ(a)
        if s == 'STOP' and a != start:
            p = [a]; tk = 0
            while p[-1] != start: q, t = prev[p[-1]]; tk += t; p.append(q)
            p.reverse(); return p, tk
        if s == 'STOP': continue
        for b, taken, extra in s:
            nd = d + 1 + extra
            if nd < dist.get(b, 1e18): dist[b] = nd; prev[b] = (a, taken); heapq.heappush(pq, (nd, b))
    return None, None

CUR_SLOT = dict(SLOT)
def set_slots(variant):
    """mapa offset -> variavel do DWARF da variante (dwarf_slots.py); o slot de c_flag e compartilhado com o temporario _sc"""
    global CUR_SLOT
    p = os.path.join(H, 'obj', variant + '.slots.json')
    if os.path.exists(p):
        m = json.load(open(p)); CUR_SLOT = {int(k): ('c_flag' if v == '_sc' else v) for k, v in m.items()}
    else:
        CUR_SLOT = dict(SLOT)

def measure(ins, p, tk):
    c = collections.Counter()
    for a in p:
        x = acc(ins[a])
        if x: c[f'{x[0]} {CUR_SLOT.get(x[1], "outro+" + str(x[1]))}'] += 1
    return dict(instr=len(p), saltos=tk, pilha=sum(c.values()), slots=dict(c))

def analyze(obj):
    ins, order, jx, tables = load(obj)
    cond_jx = next(a for a, k in jx.items() if k == 'cond')
    cond_tab = tables[cond_jx]
    arm_tab = next(tables[a] for a, k in jx.items() if k == 'arm')
    th_tab = next(tables[a] for a, k in jx.items() if k == 'thumb')
    R = {'A': {}, 'Pass': {}, 'Fail': {}, 'T': {}}
    for h, e in enumerate(arm_tab):
        p, tk = path(ins, jx, tables, e, 'cond')
        if p: R['A'][h] = measure(ins, p, tk)
    for c in range(15):
        e = cond_tab[c]
        p, tk = path(ins, jx, tables, e, 'arm')                    # aprovada: ate o despacho principal
        if p: R['Pass'][c] = measure(ins, p, tk)
        if c != 14:
            p, tk = path(ins, jx, tables, e, 'cond', avoid=('arm',))  # rejeitada: volta a condicao sem despachar
            if p: R['Fail'][c] = measure(ins, p, tk)
    for h, e in enumerate(th_tab):
        p, tk = path(ins, jx, tables, e, 'thumb')
        if p: R['T'][h] = measure(ins, p, tk)
    return R

def profiles():
    g = None; P = {}
    for l in open(os.path.join(H, '..', 'arm-condition-profile.txt')):
        if l.startswith('==='): g = 'Mario' if 'Mario' in l else 'FireRed'; P.setdefault(g, dict(mix=[0, 0], cond=collections.defaultdict(lambda: [0, 0]), hand=collections.Counter())); continue
        m = re.match(r'arm_mix_v1 frames=\d+ arm=(\d+) thumb=(\d+)', l)
        if m: P[g]['mix'][0] += int(m[1]); P[g]['mix'][1] += int(m[2])
        m = re.match(r'arm_condition c=([0-9a-f]) issued=(\d+) executed=(\d+)', l)   # c em hexadecimal (a..f: GE..NV, e = AL)
        if m: P[g]['cond'][int(m[1], 16)][0] += int(m[2]); P[g]['cond'][int(m[1], 16)][1] += int(m[3])
        m = re.match(r'arm_handler k=([0-9a-f]{2}) al=(\d+) conditional=(\d+)', l)
        if m: P[g]['hand'][int(m[1], 16)] += int(m[2]) + int(m[3])
    return P

KEYS = ('ld cpu_alert', 'st cpu_alert', 'ld c_flag', 'st c_flag', 'ld z_flag', 'st z_flag', 'ld n_flag', 'st n_flag', 'ld v_flag', 'st v_flag')

def weigh(R, P):
    out = {}
    for g, p in P.items():
        arm, th = p['mix']; fa = arm / (arm + th); ft = 1 - fa
        issued = sum(v[0] for v in p['cond'].values()); execd = sum(v[1] for v in p['cond'].values())
        hw = {h: n for h, n in p['hand'].items() if h in R['A']}; hsum = sum(hw.values())
        def avgA(k): return sum(n * R['A'][h].get(k, 0) if k in ('instr', 'saltos', 'pilha') else n * R['A'][h]['slots'].get(k, 0) for h, n in hw.items()) / hsum
        def get(d, k): return d.get(k, 0) if k in ('instr', 'saltos', 'pilha') else d['slots'].get(k, 0)
        def armcost(k):
            ex = sum(v[1] * get(R['Pass'][c], k) for c, v in p['cond'].items() if c in R['Pass'])
            rj = sum((v[0] - v[1]) * get(R['Fail'][c], k) for c, v in p['cond'].items() if c in R['Fail'])
            return (ex + rj) / issued + (execd / issued) * avgA(k)
        def thcost(k): return st.mean(get(x, k) for x in R['T'].values())
        def split(k):
            # ARM: AL (cond 14), condicional aprovada, condicional rejeitada -- por instrucao ARM daquela classe
            al = p['cond'].get(14, [0, 0])
            cp = sum(v[1] for c, v in p['cond'].items() if c != 14); cr = sum(v[0] - v[1] for c, v in p['cond'].items() if c != 14)
            return dict(AL=get(R['Pass'][14], k) + avgA(k),
                        cond_aprovada=(sum(v[1] * get(R['Pass'][c], k) for c, v in p['cond'].items() if c != 14 and c in R['Pass']) / max(cp, 1)) + avgA(k),
                        cond_rejeitada=sum((v[0] - v[1]) * get(R['Fail'][c], k) for c, v in p['cond'].items() if c in R['Fail']) / max(cr, 1))
        res = {k: dict(ARM=armcost(k), Thumb=thcost(k), total=fa * armcost(k) + ft * thcost(k)) for k in ('instr', 'saltos', 'pilha') + KEYS}
        res['_classes'] = {k: split(k) for k in ('pilha', 'instr')}
        res['_mix'] = dict(ARM=fa, Thumb=ft, AL=p['cond'].get(14, [0, 0])[1] / issued, rejeitadas=(issued - execd) / issued)
        res['_cobertura_arm_16'] = sum(n for h, n in p['hand'].most_common(16)) / sum(p['hand'].values())
        out[g] = res
    return out

if __name__ == '__main__':
    objs = sys.argv[1:] or ['base']
    P = profiles(); allr = {}
    for v in objs:
        set_slots(v)
        R = analyze(os.path.join(H, 'obj', v + '.o'))
        W = weigh(R, P); allr[v] = dict(caminhos={k: {str(i): x for i, x in d.items()} for k, d in R.items()}, ponderado=W)
        for g, w in W.items():
            print(f"{v} {g}: por instrucao emulada  instr {w['instr']['total']:.2f}  saltos {w['saltos']['total']:.2f}  pilha {w['pilha']['total']:.2f} "
                  f"(ARM {w['pilha']['ARM']:.2f} / Thumb {w['pilha']['Thumb']:.2f})  " +
                  ' '.join(f"{k.replace(' ', '_')}={w[k]['total']:.2f}" for k in KEYS if w[k]['total'] > 0.005))
    json.dump(allr, open(os.path.join(H, 'spills-' + '-'.join(objs) + '.json'), 'w'), indent=1)
