#!/usr/bin/env python3
"""Caminho comum de cada handler (ARM e Thumb) ate o PROXIMO despacho principal do mesmo modo, no objeto real cpu.cpp
(base e variantes). Reaproveita o parser do microprototipo. Despachos: jx precedido de extui ..,20,8 (ARM) ou
srli ..,8 / extui ..,8,8 (Thumb); o jx da condicao ARM (extui ..,28,4) segue para a entrada AL; outros jx (sub-tabelas
internas) seguem para a entrada mais barata. Chamadas pesam 40 para o caminho comum evitar os caminhos lentos; o mesmo
peso vale para base e variantes, entao a DIFERENCA entre elas e o custo do despacho."""
import heapq, json, os, re, struct, subprocess, sys, tempfile
H = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(H, '..', 'micro'))
from paths import parse, literal_targets, section_words, T

def regs(args): return re.findall(r'\ba\d+\b', args)

def backtrace(ins, order, pos, a):
    """jx aT <- l32i aT, aA, 0 <- addx4 aA, aI, aB : devolve (instrucao que define aI, posicao do l32r de aB ou None)"""
    i = pos[a]; want = regs(ins[a]['args'])[0]; stage = 'l32i'; idx = base = None; idx_def = base_def = None
    for k in range(i - 1, max(0, i - 40), -1):
        x = ins[order[k]]; r = regs(x['args'])
        if not r: continue
        if stage == 'l32i' and x['op'] in ('l32i', 'l32i.n') and r[0] == want:
            want = r[1]; stage = 'addx4'; continue
        if stage == 'addx4' and x['op'] == 'addx4' and r[0] == want:
            idx, base = r[1], r[2]; stage = 'defs'; continue
        if stage == 'defs':
            if idx_def is None and r[0] == idx and x['op'] in ('extui', 'srli', 'and', 'l32i.n', 'l32i', 'slli', 'mov.n', 'or'):
                idx_def = x
            if base_def is None and r[0] == base and x['op'] in ('l32r', 'l32i', 'l32i.n', 'mov.n', 'or'):
                base_def = order[k]
            if idx_def is not None and base_def is not None: break
    return idx_def, base_def

def kind(ins, order, pos, a):
    d, _ = backtrace(ins, order, pos, a)
    if d is None: return 'sub'
    if d['op'] == 'extui' and d['args'].endswith(', 28, 4'): return 'cond'
    if d['op'] == 'extui' and d['args'].endswith(', 20, 8'): return 'arm'
    if (d['op'] == 'srli' and d['args'].endswith(', 8')) or (d['op'] == 'extui' and d['args'].endswith(', 8, 8')): return 'thumb'
    return 'sub'

def analyze(obj):
    asm = obj[:-2] + '.asm'
    ins, order = parse(asm); pos = {a: i for i, a in enumerate(order)}
    lits = literal_targets(obj, '.iram1.0.literal'); words = {}
    def tbl(a, size):
        _, b = backtrace(ins, order, pos, a)
        if b is None or ins[b]['op'] != 'l32r' or ins[b]['lit'] not in lits: return None
        sec, off = lits[ins[b]['lit']]
        if not sec.startswith('.rodata'): return None
        if sec not in words: words[sec] = section_words(obj, sec)
        try: return [words[sec](off + 4 * n) for n in range(size)]
        except struct.error: return None
    jx = {a: kind(ins, order, pos, a) for a in order if ins[a]['op'] == 'jx'}
    tables = {}
    for a, k in jx.items():
        size = 256 if k in ('arm', 'thumb') else 16
        t = tbl(a, size)
        if t is None and k in ('arm', 'thumb'):     # tabela nomeada (computed goto)
            for sec in {s for s, _ in lits.values() if f'{k}_dispatch_table' in s}:
                w = section_words(obj, sec); t = [w(4 * n) for n in range(256)]
        tables[a] = t
    mains = {k: next((tables[a] for a, kk in jx.items() if kk == k and tables[a]), None) for k in ('arm', 'thumb')}
    def succ(a):
        i = ins[a]; op = i['op']; nxt = a + i['len']
        m = re.search(r'([0-9a-f]+) <', i['args']); tgt = int(m[1], 16) if m else None
        if op == 'j': return [(tgt, True, 0)]
        if op.startswith('ret'): return []
        if op == 'jx':
            k = jx[a]; t = tables.get(a)
            if k in ('arm', 'thumb'): return 'STOP:' + k
            if not t: return []
            if k == 'cond': return [(t[14], True, 0)]
            return [(x, True, 0) for x in set(t)]
        if op.startswith('call'): return [(nxt, False, 40)]
        if op.startswith('b') and tgt is not None and op != 'break': return [(nxt, False, 0), (tgt, True, 0)]
        return [(nxt, False, 0)]
    res = {}
    for mode in ('arm', 'thumb'):
        out = []
        for h, entry in enumerate(mains[mode]):
            dist = {entry: 0}; prev = {}; pq = [(0, entry)]; found = None
            while pq:
                d, a = heapq.heappop(pq)
                if d > dist.get(a, 1e18) or a not in ins: continue
                s = succ(a)
                if isinstance(s, str):
                    if a != entry and s == 'STOP:' + mode: found = a; break
                    continue
                for b, taken, extra in s:
                    nd = d + 1 + extra
                    if nd < dist.get(b, 1e18): dist[b] = nd; prev[b] = (a, taken); heapq.heappush(pq, (nd, b))
            if found is None: out.append(None); continue
            path = [found]; tk = 0
            while path[-1] != entry:
                p, t = prev[path[-1]]; tk += t; path.append(p)
            path.reverse(); ops = [ins[a] for a in path]
            out.append(dict(instr=len(path), saltos=tk, chamadas=sum(o['op'].startswith('call') for o in ops),
                            cargas=sum(o['op'] in ('l32i', 'l32i.n', 'l8ui', 'l16ui', 'l16si', 'l32r') for o in ops),
                            pilha=sum(bool(re.search(r'\ba1, \d+$', o['args'])) and o['op'][:3] in ('l32', 's32', 'l8u', 's8i', 'l16', 's16') for o in ops)))
        res[mode] = out
    return res

if __name__ == '__main__':
    R = {v: analyze(f'{H}/obj/cpu_{v}.o') for v in sys.argv[1:] or ('base', 'thumb', 'arm', 'both')}
    json.dump(R, open(f'{H}/real_paths.json', 'w'))
    for v, r in R.items():
        for mode in ('arm', 'thumb'):
            ok = [x for x in r[mode] if x]
            print(v, mode, len(ok), {k: round(sum(x[k] for x in ok) / len(ok), 2) for k in ('instr', 'saltos', 'cargas', 'pilha', 'chamadas')})
