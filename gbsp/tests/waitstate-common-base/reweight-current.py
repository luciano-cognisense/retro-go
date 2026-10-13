#!/usr/bin/env python3
import contextlib,io,json,sys
from pathlib import Path
H=Path(__file__).resolve().parent
sys.path.insert(0,str(H/'analysis'))
saved=sys.argv; sys.argv=['analise.py','base','n2']
with contextlib.redirect_stdout(io.StringIO()): import analise as A
sys.argv=saved
p=json.load(open(H.parent/'gbsp-thumb-handler-profile'/'console-analysis.json'))
out={'source_profile':str(H.parent/'gbsp-thumb-handler-profile'/'console-analysis.json'),'source_assembly':str(H/'assembly-analysis.json'),'games':{}}
for game,d in p['by_game'].items():
 counts={int(k,16):v for k,v in d['bins'].items()}; modeled=sum(n for k,n in counts.items() if k in A.RR['base']['T'] and k in A.RR['n2']['T'])
 vals={}
 for metric in ('instr','saltos','pilha','l32r'):
  vals[metric]=sum(n*(A.get(A.RR['n2']['T'][k],metric)-A.get(A.RR['base']['T'][k],metric)) for k,n in counts.items() if k in A.RR['base']['T'] and k in A.RR['n2']['T'])/modeled
 out['games'][game]={'total':d['total'],'modeled':modeled,'coverage_percent':100*modeled/d['total'],'n2_delta_per_thumb_instruction':vals}
open(H/'thumb-reweight-current.json','w').write(json.dumps(out,indent=2,ensure_ascii=False)+'\n')
print(json.dumps(out,indent=2,ensure_ascii=False))
