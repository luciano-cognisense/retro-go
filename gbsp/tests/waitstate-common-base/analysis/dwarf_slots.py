#!/usr/bin/env python3
"""Slot de pilha dominante (offset a partir de a1) de cada variavel do execute_arm, lido do DWARF do OBJETO
(sem aplicar relocacoes: so as expressoes de localizacao importam, que sao constantes). Frame base = a1 + frame.
Uso: python dwarf_slots.py <objeto.o> <frame>  -> JSON {offset_a1: variavel}"""
import collections, json, re, sys
from elftools.elf.elffile import ELFFile
from elftools.dwarf.descriptions import describe_DWARF_expr
from elftools.dwarf.locationlists import BaseAddressEntry
obj, frame = sys.argv[1], int(sys.argv[2])
f = ELFFile(open(obj, 'rb')); dw = f.get_dwarf_info(relocate_dwarf_sections=False); ll = dw.location_lists()
res = {}
for cu in dw.iter_CUs():
    for die in cu.get_top_DIE().iter_children():
        if die.tag != 'DW_TAG_subprogram' or die.attributes.get('DW_AT_name') is None or die.attributes['DW_AT_name'].value != b'execute_arm': continue
        def walk(d):
            for c in d.iter_children():
                if c.tag == 'DW_TAG_variable' and 'DW_AT_name' in c.attributes and 'DW_AT_location' in c.attributes:
                    name = c.attributes['DW_AT_name'].value.decode(); loc = c.attributes['DW_AT_location']; cnt = collections.Counter()
                    try:
                        ents = ll.get_location_list_at_offset(loc.value, die=c) if loc.form in ('DW_FORM_sec_offset', 'DW_FORM_data4') else []
                    except Exception:
                        ents = []
                    for e in ents:
                        if isinstance(e, BaseAddressEntry): continue
                        m = re.search(r'DW_OP_fbreg:?\s*(-?\d+)', describe_DWARF_expr(e.loc_expr, dw.structs, cu.cu_offset))
                        if m: cnt[frame + int(m[1])] += max(1, e.end_offset - e.begin_offset)
                    if cnt: res.setdefault(name, cnt.most_common(1)[0][0])
                walk(c)
        walk(die)
print(json.dumps({str(v): k for k, v in res.items()}))
