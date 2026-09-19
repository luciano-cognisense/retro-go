"""Exercise the real counters, C/C++ linkage, reset and log schema."""
from pathlib import Path
import os, subprocess, tempfile

core = Path(__file__).resolve().parents[1] / 'components/gbsp-libretro'
columns = [('read', t) for t in ['u8', 's8', 'u16', 's16', 'u32']]
columns += [('write', t) for t in ['u8', 'u16', 'u32']]
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    increments = '\n'.join(
        f'for (unsigned i=0; i < {index + 1} + region; ++i) {{ STATS_MEMORY_ACCESS({op}, {kind}, region); }}'
        for index, (op, kind) in enumerate(columns))
    (tmp / 'counters.cpp').write_text('''#include <stdint.h>
typedef unsigned int u32;
extern "C" {
#include "cpu_instrument.h"
void populate(void) {
 for (unsigned region=0; region<16; ++region) {
''' + increments + '''
 }
}
}
''')
    (tmp / 'main.c').write_text('''#include <stdint.h>
typedef unsigned int u32;
#include "memory_profile.h"
extern void populate(void);
int main(void) {
 gbsp_memory_profile_reset();
 populate();
 gbsp_memory_profile_write(stdout, 300);
#if GBSP_MEMORY_PROFILE_ACTIVE
 memory_region_access_write_u32[15] = UINT32_MAX;
#endif
 gbsp_memory_profile_write(stdout, 1);
 gbsp_memory_profile_reset();
 gbsp_memory_profile_write(stdout, 0);
 return 0;
}
''')
    for name, defines, active in [
        ('off', [], False),
        ('on', ['-DRETRO_GO', '-DCONFIG_GBSP_MEMORY_PROFILE=1'], True),
        ('zero', ['-DRETRO_GO', '-DCONFIG_GBSP_MEMORY_PROFILE=0'], False),
        ('dynarec', ['-DRETRO_GO', '-DCONFIG_GBSP_MEMORY_PROFILE=1', '-DHAVE_DYNAREC'], False),
    ]:
        opts = ['-O2', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-I', str(core), *defines]
        subprocess.run(['cc', *opts, '-c', str(tmp/'main.c'), '-o', str(tmp/'main.o')], check=True)
        subprocess.run(['c++', *opts, str(tmp/'counters.cpp'), str(tmp/'main.o'), '-o', str(tmp/'test')], check=True)
        text = subprocess.check_output([str(tmp/'test')], text=True)
        if active:
            lines = text.splitlines()
            assert len(lines) == 51
            for offset, frames in [(0, 300), (17, 1), (34, 0)]:
                assert lines[offset] == f'mem_window frames={frames} scope=interpreter_data timing=diagnostic'
                for region in range(16):
                    fields = dict(field.split('=') for field in lines[offset+1+region].split()[1:])
                    assert int(fields.pop('region'), 16) == region
                    assert list(fields) == ['r8','rs8','r16','rs16','r32','w8','w16','w32']
                    expected = [i+1+region for i in range(8)] if frames else [0]*8
                    if frames == 1 and region == 15:
                        expected[-1] = 2**32-1
                    assert list(map(int, fields.values())) == expected
        else:
            assert text == ''
            symbols = subprocess.check_output(['nm', str(tmp/'test')], text=True)
            assert 'memory_region_access_' not in symbols
        print(f'PASS {name}: counts, schema, reset, max-u32 and disabled elision' if active else f'PASS {name}: no output or counter symbols')
