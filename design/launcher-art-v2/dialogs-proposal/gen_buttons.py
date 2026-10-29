from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[3]
theme = root / 'themes/hands-on-lab'
output = root / 'components/retro-go/bitmaps/hol_buttons.h'
paper = (245, 243, 236)

lines = ['#pragma once', '#include <stdint.h>', '']
for letter in ('a', 'b'):
    im = Image.open(theme / f'button_{letter}.png').convert('RGB')
    assert im.size == (18, 18)
    values = []
    for r, g, b in im.get_flattened_data():
        if (r, g, b) == (255, 0, 255):
            r, g, b = paper
        values.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    lines.append(f'static const uint16_t hol_button_{letter}[18 * 18] = {{')
    for i in range(0, len(values), 18):
        lines.append('    ' + ', '.join(f'0x{x:04X}' for x in values[i:i + 18]) + ',')
    lines.extend(['};', ''])

output.write_text('\n'.join(lines))
