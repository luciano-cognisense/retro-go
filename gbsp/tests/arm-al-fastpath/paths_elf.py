#!/usr/bin/env python3
"""Shortest common path of every ARM/Thumb handler of execute_arm() to the next main dispatch, read from a linked ELF.
Usage: paths_elf.py <elf> -> JSON on stdout: {arm: [..256], thumb: [..256], head_nonal: n}"""
import glob, heapq, os, json, re, struct, subprocess, sys

T = glob.glob(os.path.expanduser('~/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump'))[0][:-7]
elf = sys.argv[1]

def disasm():
    out = subprocess.run([T + 'objdump', '-d', elf], capture_output=True, text=True).stdout
    ins, order, on = {}, [], False
    for l in out.splitlines():
        if re.match(r'^[0-9a-f]+ <execute_arm>:', l): on = True; continue
        if on and re.match(r'^[0-9a-f]+ <', l): break
        m = re.match(r'^\s*([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(\S+)\t?(.*)$', l)
        if on and m:
            a = int(m[1], 16); ins[a] = dict(len=len(m[2].replace(' ', '')) // 2, op=m[3], args=m[4]); order.append(a)
    return ins, order

# ELF memory reader
hdr = subprocess.run([T + 'readelf', '-SW', elf], capture_output=True, text=True).stdout
secs = []
for l in hdr.splitlines():
    m = re.match(r'^\s*\[\s*\d+\]\s+(\S+)\s+PROGBITS\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', l)
    if m: secs.append((int(m[2], 16), int(m[3], 16), int(m[4], 16)))
blob = open(elf, 'rb').read()
def word(addr):
    for va, off, size in secs:
        if va <= addr < va + size: return struct.unpack_from('<I', blob, off + addr - va)[0]
    raise KeyError(hex(addr))

ins, order = disasm(); pos = {a: i for i, a in enumerate(order)}
regs = lambda s: re.findall(r'\ba\d+\b', s)

def backtrace(a):
    i = pos[a]; want = regs(ins[a]['args'])[0]; stage = 'l32i'; idx = base = None; idx_def = base_val = None
    for k in range(i - 1, max(0, i - 40), -1):
        x = ins[order[k]]; r = regs(x['args'])
        if not r: continue
        if stage == 'l32i' and x['op'] in ('l32i', 'l32i.n') and r[0] == want: want = r[1]; stage = 'addx4'; continue
        if stage == 'addx4' and x['op'] == 'addx4' and r[0] == want: idx, base = r[1], r[2]; stage = 'defs'; continue
        if stage == 'defs':
            if idx_def is None and r[0] == idx: idx_def = x
            if base_val is None and r[0] == base and x['op'] == 'l32r':
                m = re.search(r'\(([0-9a-f]+)', x['args']); base_val = int(m[1], 16) if m else None
            if idx_def is not None and base_val is not None: break
    return idx_def, base_val

def kind(d):
    if d is None: return 'sub'
    if d['op'] == 'extui' and d['args'].endswith(', 28, 4'): return 'cond'
    if d['op'] == 'extui' and d['args'].endswith(', 20, 8'): return 'arm'
    if (d['op'] == 'srli' and d['args'].endswith(', 8')) or (d['op'] == 'extui' and d['args'].endswith(', 8, 8')): return 'thumb'
    return 'sub'

jx, tables = {}, {}
for a in order:
    if ins[a]['op'] != 'jx': continue
    d, b = backtrace(a); k = kind(d); jx[a] = k
    if b is not None:
        n = 256 if k in ('arm', 'thumb') else 16
        try: tables[a] = [word(b + 4 * j) for j in range(n)]
        except KeyError: tables[a] = None
mains = {k: next(tables[a] for a, kk in jx.items() if kk == k and tables.get(a)) for k in ('arm', 'thumb')}

def succ(a):
    i = ins[a]; op = i['op']; nxt = a + i['len']
    m = re.search(r'\b([0-9a-f]{6,}) <', i['args']); tgt = int(m[1], 16) if m else None
    if op in ('j', 'j.l'): return [(tgt, True, 0)]
    if op.startswith('ret'): return []
    if op == 'jx':
        k = jx[a]; t = tables.get(a)
        if k in ('arm', 'thumb'): return 'STOP:' + k
        if not t: return []
        if k == 'cond': return [(t[14], True, 0)]          # AL entry
        return [(x, True, 0) for x in set(t)]
    if op.startswith('call'): return [(nxt, False, 40)]
    if op.startswith('b') and tgt is not None and op != 'break':
        return [(nxt, False, 0), (tgt, True, 0)]
    return [(nxt, False, 0)]

STACK = re.compile(r'\ba1, \d+$')
def path(entry, mode):
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
    if found is None: return None
    p = [found]; tk = 0
    while p[-1] != entry:
        q, t = prev[p[-1]]; tk += t; p.append(q)
    ops = [ins[a] for a in reversed(p)]
    return dict(instr=len(p), saltos=tk, jx=sum(o['op'] == 'jx' for o in ops),
                cargas=sum(o['op'] in ('l32i', 'l32i.n', 'l8ui', 'l16ui', 'l16si', 'l32r') for o in ops),
                pilha=sum(bool(STACK.search(o['args'])) and o['op'][:3] in ('l32', 's32', 'l8u', 's8i', 'l16', 's16') for o in ops),
                chamadas=sum(o['op'].startswith('call') for o in ops))

res = {m: [path(e, m) for e in mains[m]] for m in ('arm', 'thumb')}
res['frame'] = next(int(re.search(r'a1, (\d+)', ins[a]['args'])[1]) for a in order if ins[a]['op'] == 'entry')
json.dump(res, sys.stdout)
