from pathlib import Path
from PIL import Image

here = Path(__file__).parent
theme = here.parents[2] / 'themes/hands-on-lab'

for key in ('favorite', 'recent'):
    Image.open(here / f'{key}-320x240.png').convert('RGB').quantize(colors=256).save(
        theme / f'pilot_{key}.png', optimize=True)
    art = Image.open(here / 'source' / f'{key}.png').convert('RGBA')
    art = art.crop(art.getbbox())
    art.thumbnail((82, 54), Image.Resampling.LANCZOS)
    side = Image.new('RGB', (88, 60), 'white')
    side.paste(art, ((88 - art.width) // 2, (60 - art.height) // 2), art)
    side.quantize(colors=128).save(theme / f'pilot_side_{key}.png', optimize=True)
