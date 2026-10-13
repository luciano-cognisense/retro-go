#!/usr/bin/env python3
"""Mini ligador para as ROMs de estimulo (nao ha llvm-objcopy/lld nesta maquina): monta com clang -c, coloca .text em
0x08000000, aplica as relocacoes R_ARM_ABS32 (addend em-lugar, REL) e grava a ROM de 1 MiB. Imprime os simbolos
globais (para o harness achar o endereco do laco de espera usado como idle loop).
Uso: python mklink.py fonte.s saida.gba   (rodar no python do ESP-IDF, que tem pyelftools)"""
import struct, subprocess, sys, os, tempfile
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
src, out = sys.argv[1], sys.argv[2]
BASE = 0x08000000
with tempfile.TemporaryDirectory() as td:
    obj = os.path.join(td, 'o.o')
    subprocess.run(['clang', '-target', 'armv4t-none-eabi', '-c', src, '-o', obj], check=True)
    f = ELFFile(open(obj, 'rb'))
    text = f.get_section_by_name('.text'); data = bytearray(text.data())
    tidx = [i for i, s in enumerate(f.iter_sections()) if s.name == '.text'][0]
    symtab = f.get_section_by_name('.symtab')
    for sec in f.iter_sections():
        if isinstance(sec, RelocationSection) and sec.name in ('.rel.text', '.rela.text'):
            for r in sec.iter_relocations():
                t = r['r_info_type']; sym = symtab.get_symbol(r['r_info_sym']); off = r['r_offset']
                if t == 2:     # R_ARM_ABS32
                    if sym['st_shndx'] != tidx: sys.exit(f'simbolo fora de .text: {sym.name}')
                    a = struct.unpack_from('<I', data, off)[0]
                    struct.pack_into('<I', data, off, (BASE + sym['st_value'] + a) & 0xFFFFFFFF)
                elif t in (28, 29, 10):   # CALL/JUMP24/THM_CALL dentro da secao: o assembler ja resolveu se local
                    if sym['st_shndx'] != tidx: sys.exit(f'desvio para fora: {sym.name}')
                    sys.exit(f'relocacao de desvio nao resolvida ({t}) para {sym.name}: use rotulos locais')
                else:
                    sys.exit(f'relocacao nao suportada: tipo {t} em 0x{off:x}')
    syms = {s.name: BASE + s['st_value'] for s in symtab.iter_symbols() if s['st_shndx'] == tidx and s.name and not s.name.startswith('$')}
data += b'\x00' * (1024 * 1024 - len(data))
open(out, 'wb').write(bytes(data))
for k in sorted(syms, key=syms.get):
    print(f'{k}=0x{syms[k]:08x}')
