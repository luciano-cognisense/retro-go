#!/usr/bin/env python3
"""Gates 0-1 (frente nseq): custo das bases de ws_cyc_nseq e ws_cyc_seq no caminho comum de cada instrucao.
Derivado de gbsp-seq-cycles/analise.py: cada literal (simbolo+addend) e cada slot de pilha que recebe um desses literais
e classificado como base de 'seq', 'nseq' ou 'comum' pelo mapa variantes.CLASSES da variante.
Usa o mesmo analisador de caminhos das frentes anteriores (spills.py: ARM = Pass(c)/Fail(c) + A(h); Thumb = T(h)),
no objeto Xtensa real compilado com o comando da v4, e acrescenta por caminho:
  - ws_pilha : leituras do slot de pilha que guarda o endereco da tabela (slot achado no proprio objeto: s32i em a1
               do registrador definido por l32r de um literal que aponta para a tabela);
  - ws_l32r  : l32r de literal que aponta para a tabela (rematerializacao do endereco);
  - l32r     : total de l32r no caminho; pilha por slot (DWARF da propria variante).
Pesos ARM: arm-condition-profile.txt (parser hexadecimal). Thumb: so distribuicao estrutural (sem histograma).
Uso: analise.py [variante...]  -> analise.json, analise.md"""
import collections, json, os, re, statistics as st, subprocess, sys
H = os.path.dirname(os.path.abspath(__file__)); args = sys.argv[1:]; sys.argv = sys.argv[:1]
import spills as S
from paths import literal_targets
PY = os.path.join(os.environ.get('IDF_PYTHON_ENV_PATH', ''), 'bin', 'python')
import variantes as VG
VARS = args or ['base'] + [v for v in VG.V if v != 'N1_controle_macro'] + ['N1_controle_macro']
CUR = {}

def prep(v):
    o = os.path.join(H, 'obj', v + '.o'); asm = open(os.path.join(H, 'obj', v + '.asm')).read()
    fr = int(re.search(r'<execute_arm>:\n\s*\S+:\s+[0-9a-f ]+\s+entry\s+a1, (\d+)', asm).group(1))
    sl = subprocess.run([PY, os.path.join(H, 'dwarf_slots.py'), o, str(fr)], capture_output=True, text=True, check=True).stdout
    open(os.path.join(H, 'obj', v + '.slots.json'), 'w').write(sl)
    lits = literal_targets(o, '.iram1.0.literal')
    ins, order, jx, tables = S.load(o)
    # slots que recebem o endereco de uma tabela ws (l32r -> s32i a1 em ate 6 instrucoes, mesmo registrador)
    def symadd(sym, add):   # objdump escreve o addend no nome (reg+0x00000081) ou no dado em-lugar
        m = re.match(r'^(.+?)\+0x([0-9a-f]+)$', sym); return (m.group(1), int(m.group(2), 16) + add) if m else (sym, add)
    def cls(sym, add):
        s_, a_ = symadd(sym, add)
        for name, lo, hi, c in VG.CLASSES[v]:
            if s_ == name and lo <= a_ <= hi: return c
        return None
    litcls = {off: cls(sym, add) for off, (sym, add) in lits.items() if cls(sym, add)}
    slotcls = {}
    for i, a in enumerate(order):
        x = ins[a]
        if x['op'] == 'l32r' and x['lit'] in litcls:
            r = x['args'].split(',')[0].strip()
            for b in order[i + 1:i + 7]:
                m = re.match(r's32i(?:\.n)?\s+(a\d+), a1, (\d+)', ins[b]['op'] + ' ' + ins[b]['args'])
                if m and m.group(1) == r: slotcls[int(m.group(2))] = litcls[x['lit']]
    CUR.update(lits=lits, litcls=litcls, slotcls=slotcls)
    return dict(frame=fr, slots=json.loads(sl), slots_base={str(k): c for k, c in sorted(slotcls.items())},
                l32r_estaticos={c: sum(1 for a in order if ins[a]['op'] == 'l32r' and litcls.get(ins[a]['lit']) == c) for c in ('seq', 'nseq', 'comum')})

_orig = S.measure
def measure(ins, p, tk):
    r = _orig(ins, p, tk); c = collections.Counter()
    for a in p:
        x = ins[a]
        if x['op'] == 'l32r':
            c['l32r'] += 1
            k = CUR['litcls'].get(x['lit'])
            if k: c[f'{k}_l32r'] += 1; c[f'{k}_base'] += 1
        acc = S.acc(x)
        if acc and acc[0] == 'ld' and acc[1] in CUR['slotcls']:
            k = CUR['slotcls'][acc[1]]; c[f'{k}_pilha'] += 1; c[f'{k}_base'] += 1
    r.update(c)
    return r
S.measure = measure

KEYS = ('instr', 'saltos', 'pilha', 'l32r', 'seq_base', 'seq_pilha', 'seq_l32r', 'nseq_base', 'nseq_pilha', 'nseq_l32r', 'comum_base')
def get(d, k):
    if k in d and not isinstance(d[k], dict): return d[k]
    return d['slots'].get(k, 0)

def weigh_arm(R, p, f):
    issued = sum(v[0] for v in p['cond'].values()); execd = sum(v[1] for v in p['cond'].values())
    hw = {h: n for h, n in p['hand'].items() if h in R['A']}; hs = sum(hw.values())
    a = sum(n * f(R['A'][h]) for h, n in hw.items()) / hs
    ex = sum(v[1] * f(R['Pass'][c]) for c, v in p['cond'].items() if c in R['Pass'])
    rj = sum((v[0] - v[1]) * f(R['Fail'][c]) for c, v in p['cond'].items() if c in R['Fail'])
    return (ex + rj) / issued + (execd / issued) * a

def dist(xs):
    xs = sorted(xs); q = st.quantiles(xs, n=4)
    return dict(min=xs[0], q1=q[0], mediana=st.median(xs), media=st.mean(xs), q3=q[2], max=xs[-1])

P = S.profiles(); out = {}; RR = {}
for v in VARS:
    info = prep(v); S.set_slots(v); R = S.analyze(os.path.join(H, 'obj', v + '.o')); RR[v] = R
    size = {l.split()[0]: int(l.split()[1]) for l in open(os.path.join(H, 'obj', v + '.size')) if l.startswith('.') and len(l.split()) >= 2}
    msize = {l.split()[0]: int(l.split()[1]) for l in open(os.path.join(H, 'obj', v + '.mem.size')) if l.startswith('.') and len(l.split()) >= 2}
    slotsum = collections.Counter()
    arm = {}
    for g, p in P.items():
        arm[g] = {k: weigh_arm(R, p, lambda d, k=k: get(d, k)) for k in KEYS}
        allslots = set(k for part in R.values() for d in part.values() for k in d['slots'])
        arm[g]['slots'] = {k: weigh_arm(R, p, lambda d, k=k: d['slots'].get(k, 0)) for k in allslots}
        arm[g]['slots'] = {k: x for k, x in sorted(arm[g]['slots'].items(), key=lambda t: -t[1]) if x >= 0.01}
    th = {k: dist([get(d, k) for d in R['T'].values()]) for k in KEYS}
    tslots = collections.Counter()
    for d in R['T'].values():
        for k, n in d['slots'].items(): tslots[k] += n
    th['slots'] = {k: n / len(R['T']) for k, n in tslots.most_common() if n / len(R['T']) >= 0.01}
    masm = open(os.path.join(H, 'obj', v + '.mem.asm')).read()
    rt = re.search(r'<reload_timing_info>:\n(.*?)\n\n', masm, re.S)
    rt_ins = [l for l in rt.group(1).splitlines() if re.match(r'^\s*[0-9a-f]+:\t', l)] if rt else []
    tails = collections.Counter()
    for m in re.finditer(r'extui\ta\d+, a\d+, 24, [48]\n(?:.*\n){0,3}?.*l8ui\ta\d+, a\d+, (\d+)', open(os.path.join(H, 'obj', v + '.asm')).read()):
        tails['ARM' if m.group(1) in ('1', '129') else 'Thumb' if m.group(1) in ('0', '128') else 'outro_' + m.group(1)] += 1
    out[v] = dict(info, reload_timing_info_instr=len(rt_ins), caudas_estaticas=dict(tails), iram1_0=size.get('.iram1.0'), literal=size.get('.iram1.0.literal'), rodata=size.get('.rodata'),
                  dram=size.get('.dram0.data'), mem_text=sum(x for k, x in msize.items() if k.startswith(('.text', '.literal', '.iram'))),
                  mem_data=sum(x for k, x in msize.items() if k.startswith(('.data', '.dram', '.bss', '.rodata'))),
                  arm=arm, thumb=th, thumb_handlers=len(R['T']))
    print(v, 'frame', info['frame'], 'slots_base', info['slots_base'], 'iram', size.get('.iram1.0'),
          ' '.join(f"{g}: instr {arm[g]['instr']:.2f} saltos {arm[g]['saltos']:.2f} pilha {arm[g]['pilha']:.2f} seq {arm[g]['seq_base']:.2f} nseq {arm[g]['nseq_base']:.2f} comum {arm[g]['comum_base']:.2f}" for g in arm),
          f"| Thumb instr {th['instr']['media']:.2f} pilha {th['pilha']['media']:.2f} seq {th['seq_base']['media']:.2f} nseq {th['nseq_base']['media']:.2f} comum {th['comum_base']['media']:.2f}", flush=True)
# deltas por handler Thumb contra a base
for v in VARS:
    if v == 'base': continue
    hs = sorted(set(RR['base']['T']) & set(RR[v]['T']))
    out[v]['thumb_delta'] = {k: dict(dist([get(RR[v]['T'][h], k) - get(RR['base']['T'][h], k) for h in hs]),
                                     melhor=sum(get(RR[v]['T'][h], k) < get(RR['base']['T'][h], k) for h in hs),
                                     pior=sum(get(RR[v]['T'][h], k) > get(RR['base']['T'][h], k) for h in hs)) for k in ('instr', 'saltos', 'pilha', 'seq_base', 'nseq_base', 'comum_base')}
json.dump(out, open(os.path.join(H, 'analise.json'), 'w'), indent=1)
