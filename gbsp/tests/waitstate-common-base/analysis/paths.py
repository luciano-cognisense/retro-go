#!/usr/bin/env python3
"""Caminho mais curto (caso comum) de cada handler ate o PROXIMO despacho principal, nos objetos dos microprototipos.
Grafo de fluxo a partir da desmontagem (offsets da secao de codigo); entradas dos handlers pelas relocacoes da tabela.
ARM: o jx da condicao segue para a entrada AL (caso 0xE) da tabela de condicao; o despacho principal e o jx da tabela 256.
Peso = instrucoes; chamadas custam 40 (para o caso comum nao passar por load_page/update_gba). Mede, no caminho escolhido:
instrucoes, saltos tomados (j, desvios tomados), jx, cargas, acessos a pilha (a1), l32r."""
import re, sys, heapq, json, subprocess, os
H = os.path.dirname(os.path.abspath(__file__))
T = os.path.expanduser('~/.espressif/tools/xtensa-esp-elf/esp-13.2.0_20240530/xtensa-esp-elf/bin/xtensa-esp32s3-elf-')

def parse(asm_path):
    ins = {}; order = []; last = None
    for l in open(asm_path):
        m = re.match(r'^\s*([0-9a-f]+):\t([0-9a-f]+)\s*\t(\S+)\t?(.*)$', l)
        if m:
            a = int(m[1], 16); ins[a] = dict(len=len(m[2]) // 2, op=m[3], args=m[4], lit=None); order.append(a); last = a
            continue
        m = re.match(r'^\s*[0-9a-f]+: R_XTENSA_SLOT0_OP\s+(\S+?)(?:\+0x([0-9a-f]+))?\s*$', l)
        if m and last is not None and ins[last]['op'] == 'l32r' and '.literal' in m[1]:
            ins[last]['lit'] = int(m[2] or '0', 16)
    return ins, order

def section_words(obj, sec):
    import tempfile, struct
    with tempfile.NamedTemporaryFile() as f:
        subprocess.run([T + 'objcopy', '-O', 'binary', '--only-section=' + sec, obj, f.name], check=True)
        data = open(f.name, 'rb').read()
    return lambda o: struct.unpack_from('<I', data, o)[0]

def literal_targets(obj, litsec):
    """offset no literal -> (secao alvo, deslocamento em-lugar)"""
    out = subprocess.run([T + 'objdump', '-r', '-j', litsec, obj], capture_output=True, text=True).stdout
    w = section_words(obj, litsec); res = {}
    for l in out.splitlines():
        m = re.match(r'^([0-9a-f]+) R_XTENSA_32\s+(\S+)\s*$', l)
        if m: res[int(m[1], 16)] = (m[2], w(int(m[1], 16)))
    return res

def table_entries(obj, sec_prefix):
    """offsets das entradas de cada tabela: relocacoes R_XTENSA_32 contra a secao de codigo; o deslocamento fica
    no proprio dado da secao (addend em-lugar no Xtensa), entao le os bytes da secao."""
    import tempfile, struct
    out = subprocess.run([T + 'objdump', '-r', obj], capture_output=True, text=True).stdout
    tabs = {}; cur = None
    for l in out.splitlines():
        m = re.match(r'RELOCATION RECORDS FOR \[(.+)\]:', l)
        if m: cur = m[1]; continue
        m = re.match(r'^([0-9a-f]+) R_XTENSA_32\s+(\S+)\s*$', l)
        if m and cur and cur.startswith('.rodata') and m[2].startswith(sec_prefix):
            tabs.setdefault(cur, []).append(int(m[1], 16))
    res = {}
    for sec, offs in tabs.items():
        with tempfile.NamedTemporaryFile() as f:
            subprocess.run([T + 'objcopy', '-O', 'binary', '--only-section=' + sec, obj, f.name], check=True)
            data = open(f.name, 'rb').read()
        res[sec] = {o: struct.unpack_from('<I', data, o)[0] for o in offs}
    return res

def analyze(obj, asm, thumb):
    ins, order = parse(asm)
    mode = 'thumb' if thumb else 'arm'
    lits = literal_targets(obj, f'.iram1.proto_{mode}.literal')
    words = {}
    def table_at(i_jx, size):
        # l32r da base da tabela nas instrucoes anteriores ao jx; com computed goto a tabela 256 e o simbolo 'table'
        k = order.index(i_jx)
        for j in range(k - 1, max(0, k - 12), -1):
            x = ins[order[j]]
            if x['op'] == 'l32r' and x['lit'] is not None and x['lit'] in lits:
                sec, off = lits[x['lit']]
                named = any('table' in s2 for s2, _ in lits.values())
                if size == 256 and named and 'table' not in sec: continue
                if size == 16 and 'table' in sec: continue
                if sec.startswith('.rodata'):
                    if sec not in words: words[sec] = section_words(obj, sec)
                    return [words[sec](off + 4 * n) for n in range(size)]
        return None
    jxs = [a for a in order if ins[a]['op'] == 'jx']
    main = al = None
    for a in jxs:
        if is_main_jx(ins, order, a, thumb) and main is None:
            main = table_at(a, 256)
        elif not thumb and al is None:
            k = order.index(a)
            if any(ins[order[j]]['op'] == 'extui' and ins[order[j]]['args'].endswith(', 28, 4') for j in range(k - 1, max(0, k - 9), -1)):
                t = table_at(a, 16); al = t[14] if t else None
    # sitios jx: classificar pelo destino possivel -- jx que alcanca entradas da tabela principal = despacho
    def succ(a):
        i = ins[a]; op = i['op']; nxt = a + i['len']; tgt = None
        m = re.search(r'([0-9a-f]+) <', i['args'])
        if m: tgt = int(m[1], 16)
        if op == 'j': return [(tgt, True)]
        if op in ('ret', 'ret.n', 'retw', 'retw.n'): return []
        if op == 'jx': return 'JX'
        if op.startswith('b') and tgt is not None and op not in ('break',):
            return [(nxt, False), (tgt, True)]
        return [(nxt, False)]
    res = []
    if main is None:   # threaded: a base da tabela fica num registrador; ler a tabela nomeada inteira
        named = [s2 for s2, _ in lits.values() if 'table' in s2]
        if named:
            sec = named[0]
        else:   # switch com a base hoisted: a unica .rodata de 1 KB e a tabela
            out = subprocess.run([T + 'objdump', '-h', obj], capture_output=True, text=True).stdout
            sec = next(l.split()[1] for l in out.splitlines() if len(l.split()) > 6 and l.split()[1].startswith('.rodata') and int(l.split()[2], 16) == 1024)
        w = section_words(obj, sec); main = [w(4 * n) for n in range(256)]
    if not thumb and al is None:
        for a in jxs:
            k = order.index(a)
            if any(ins[order[j]]['op'] == 'extui' and ins[order[j]]['args'].endswith(', 28, 4') for j in range(k - 1, max(0, k - 9), -1)):
                t = table_at(a, 16)
                if t: al = t[14]; break
    cond_al = {}
    for h, entry in enumerate(main):
        # dijkstra a partir da entrada; estado = endereco
        dist = {entry: 0}; prev = {}; pq = [(0, entry)]; found = None; seen_cond = False
        while pq:
            d, a = heapq.heappop(pq)
            if d > dist.get(a, 1e9) or a not in ins: continue
            s = succ(a)
            if s == 'JX':
                # despacho: e o principal se a sequencia anterior carrega da tabela 256; senao (ARM) condicao -> AL
                if a != entry and is_main_jx(ins, order, a, thumb):
                    found = a; break
                if not thumb and a != entry:
                    if a not in cond_al:
                        t = table_at(a, 16); cond_al[a] = t[14] if t else None
                    tgt = cond_al[a]
                    if tgt is not None:
                        nd = d + 1
                        if nd < dist.get(tgt, 1e9): dist[tgt] = nd; prev[tgt] = (a, True); heapq.heappush(pq, (nd, tgt))
                continue
            for b, taken in s:
                w = 1 + (40 if ins[a]['op'].startswith('call') else 0)
                nd = d + w
                if nd < dist.get(b, 1e9): dist[b] = nd; prev[b] = (a, taken); heapq.heappush(pq, (nd, b))
        if found is None: res.append(None); continue
        path = [found]; taken_n = 0
        while path[-1] != entry:
            p, tk = prev[path[-1]]; taken_n += tk; path.append(p)
        path.reverse()
        ops = [ins[a] for a in path]
        res.append(dict(instr=len(path), saltos_tomados=taken_n, jx=sum(o['op'] == 'jx' for o in ops),
                        cargas=sum(o['op'] in ('l32i', 'l32i.n', 'l8ui', 'l16ui', 'l16si', 'l32r') for o in ops),
                        pilha=sum(bool(re.search(r'\ba1, \d+$', o['args'])) and o['op'][:3] in ('l32', 's32', 'l8u', 's8i', 'l16', 's16') for o in ops)))
    return res

JX_MAIN = {}
def is_cond_jx(ins, order, a):
    i = order.index(a)
    return any(ins[order[k]]['op'] == 'extui' and ins[order[k]]['args'].endswith(', 28, 4') for k in range(i - 1, max(0, i - 13), -1))

def is_main_jx(ins, order, a, thumb):
    # Thumb: todo jx do prototipo e despacho principal; ARM: todo jx que nao e o da condicao
    return thumb or not is_cond_jx(ins, order, a)

if __name__ == '__main__':
    out = {}
    for m in ('arm', 'thumb'):
        for v in ('S', 'G', 'T'):
            r = analyze(f'{H}/obj/{m}_{v}.o', f'{H}/obj/{m}_{v}.asm', m == 'thumb')
            ok = [x for x in r if x]
            mean = lambda k: sum(x[k] for x in ok) / len(ok)
            out[f'{m}_{v}'] = dict(handlers=len(ok), **{k: round(mean(k), 2) for k in ('instr', 'saltos_tomados', 'jx', 'cargas', 'pilha')},
                                   por_classe={c: round(sum(x['instr'] for i, x in enumerate(r) if x and i % 8 == c) / max(1, sum(1 for i, x in enumerate(r) if x and i % 8 == c)), 1) for c in range(8)})
            print(m, v, out[f'{m}_{v}'])
    json.dump(out, open(f'{H}/paths.json', 'w'), indent=1)
