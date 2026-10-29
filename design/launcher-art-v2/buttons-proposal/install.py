from pathlib import Path
from PIL import Image

here = Path(__file__).parent
theme = here.parents[2] / 'themes/hands-on-lab'
paper = (245, 243, 236)

for letter in ('a', 'b'):
    icon = Image.open(here / f'button-{letter}-proposta.png').convert('RGBA')
    result = Image.new('RGB', icon.size, (255, 0, 255))
    src = icon.load()
    dst = result.load()
    for y in range(icon.height):
        for x in range(icon.width):
            r, g, b, alpha = src[x, y]
            if alpha > 8:
                t = alpha / 255
                dst[x, y] = tuple(round(fg * t + bg * (1 - t))
                                  for fg, bg in zip((r, g, b), paper))
    result.save(theme / f'button_{letter}.png', optimize=True)
