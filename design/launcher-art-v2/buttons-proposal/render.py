from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).parent
ART = OUT.parent
FONT = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'
PAPER = '#F5F3EC'


def button(letter, fill, ink):
    # Centre the ink mass on the circle at high resolution, then downsample.
    scale = 8
    size = 18 * scale
    im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse((2 * scale, 2 * scale, size - 2 * scale - 1, size - 2 * scale - 1), fill=fill)
    font = ImageFont.truetype(FONT, 10 * scale)
    glyph_canvas = Image.new('L', (size, size), 0)
    ImageDraw.Draw(glyph_canvas).text((0, 0), letter, fill=255, font=font)
    glyph = glyph_canvas.crop(glyph_canvas.getbbox())
    pixels = glyph.load()
    mass = sum(pixels[x, y] for y in range(glyph.height) for x in range(glyph.width))
    cx = sum(x * pixels[x, y] for y in range(glyph.height) for x in range(glyph.width)) / mass
    cy = sum(y * pixels[x, y] for y in range(glyph.height) for x in range(glyph.width)) / mass
    mask = Image.new('L', (size, size), 0)
    mask.paste(glyph, (round((size - 1) / 2 - cx), round((size - 1) / 2 - cy)))
    im.paste(ink, (0, 0, size, size), mask)
    return im.resize((18, 18), Image.Resampling.LANCZOS)


a = button('A', '#E53935', '#FFFFFF')
b = button('B', '#FFD21F', '#26333F')
a.save(OUT / 'button-a-proposta.png')
b.save(OUT / 'button-b-proposta.png')


def replace(source, dest, positions):
    im = Image.open(source).convert('RGB')
    d = ImageDraw.Draw(im)
    for x, y, icon in positions:
        d.rectangle((x, y, x + 17, y + 17), fill=PAPER)
        im.paste(icon, (x, y), icon)
    if dest.name in ('console-320x240.png', 'favoritos-320x240.png'):
        d.rectangle((234, 216, 305, 239), fill=PAPER)
        d.text((304, 222), '<  > ESCOLHER', font=ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 10), fill='#26333F', anchor='ra')
    im.save(dest)
    im.resize((960, 720), Image.Resampling.NEAREST).save(dest.with_name(dest.stem + '-3x.png'))


replace(ART / 'logos/nes-320x240.png', OUT / 'console-320x240.png', [(16, 218, a)])
replace(ART / 'collections-v2/favorite-320x240.png', OUT / 'favoritos-320x240.png', [(16, 218, a)])
replace(ART / 'game-list-exploration/A-lista-e-savestate-320x240.png',
        OUT / 'jogos-320x240.png', [(16, 218, a), (196, 218, b)])

board = Image.new('RGB', (1008, 278), '#E4E6E8')
d = ImageDraw.Draw(board)
for n, (name, label) in enumerate((('console', 'Console'), ('favoritos', 'Favoritos'), ('jogos', 'Lista de jogos'))):
    x = 8 + 336 * n
    d.text((x, 5), label, font=ImageFont.truetype(FONT, 12), fill='#26333F')
    board.paste(Image.open(OUT / f'{name}-320x240.png'), (x, 25))
board.save(OUT / 'comparacao.png')
