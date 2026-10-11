"""Generate the big time digits for LargeTime.

Renders 0-9 and ':' for each time font as 2-bit palettized PNGs (transparent
plus three alpha levels of white) so the watch can recolor them at runtime
and keep smooth anti-aliased edges. Pebble's font system caps glyph size,
which is why the large digits are images.

Each font gets two sets:
  <prefix>_*   for 12-hour times (never start with a wide digit)
  <prefix>n_*  narrower, for 24-hour or leading-zero times like "23:48"
Each set is squeezed horizontally only as much as needed for every possible
time to fit the 144px screen.

Usage: python tools/make_digits.py
Requires Pillow. Fonts live in tools/fonts.
"""
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'resources', 'digits')

DIGIT_H = 66        # ink height of the digits in pixels
MAX_W = 140         # widest allowed time, in pixels
FIT_GAP = 2         # digit spacing assumed when fitting (the watch tightens to 1 if needed)
MAX_SQUEEZE = 1.0   # natural width; fonts are only narrowed if a time wouldn't fit

# (file prefix, font file)
# Order matches the TimeFont setting values in src/pkjs/config.js
FONTS = [
    ('roboto', 'Roboto-Bold.ttf'),
    ('tomorrow', 'Tomorrow-SemiBold.ttf'),
    ('barlowl', 'BarlowCondensed-Light.ttf'),
    ('barlowr', 'BarlowCondensed-Regular.ttf'),
    ('barlowm', 'BarlowCondensed-Medium.ttf'),
    ('barlows', 'BarlowCondensed-SemiBold.ttf'),
    ('barlowb', 'BarlowCondensed-Bold.ttf'),
    ('bebas', 'BebasNeue-Regular.ttf'),
]

TIMES_12H = ['%d:%02d' % (h, m) for h in range(1, 13) for m in range(60)]
TIMES_24H = ['%02d:%02d' % (h, m) for h in range(24) for m in range(60)]


def font_for_height(path):
    size = DIGIT_H
    for _ in range(8):
        f = ImageFont.truetype(path, size)
        bb = f.getbbox('0')
        size = max(1, int(round(size * DIGIT_H / (bb[3] - bb[1]))))
    return ImageFont.truetype(path, size)


def render(f, ch):
    """Unsqueezed glyph, cropped to its ink horizontally, DIGIT_H tall."""
    ref = f.getbbox('0')
    bb = f.getbbox(ch)
    im = Image.new('L', (bb[2] - bb[0] + 8, DIGIT_H + 8), 0)
    ImageDraw.Draw(im).text((4 - bb[0], 4 - ref[1]), ch, font=f, fill=255)
    im = im.crop((0, 4, im.width, 4 + DIGIT_H))  # shared baseline and height
    ink = im.getbbox()
    return im.crop((ink[0], 0, ink[2], DIGIT_H))


def squeezed(glyphs, squeeze):
    # The colon keeps its natural width so its dots stay round
    return {ch: im if ch == ':' else im.resize((max(1, int(round(im.width * squeeze))), DIGIT_H), Image.LANCZOS)
            for ch, im in glyphs.items()}


def widest(glyphs, times):
    return max(sum(glyphs[c].width for c in t) + FIT_GAP * (len(t) - 1) for t in times)


def fit(glyphs, times):
    squeeze = MAX_SQUEEZE
    while squeeze > 0.3 and widest(squeezed(glyphs, squeeze), times) > MAX_W:
        squeeze -= 0.01
    return squeeze


def save_2bit(im, path):
    # index 0 = transparent, 1..3 = white at increasing alpha
    p = Image.new('P', im.size, 0)
    p.putpalette([0, 0, 0] + [255, 255, 255] * 3 + [0, 0, 0] * 252)
    src = im.load()
    dst = p.load()
    for y in range(im.height):
        for x in range(im.width):
            dst[x, y] = min(3, (src[x, y] + 42) // 85)
    p.save(path, transparency=bytes([0, 85, 170, 255]), bits=2, optimize=False)


def main():
    os.makedirs(OUT, exist_ok=True)
    for prefix, font_file in FONTS:
        f = font_for_height(os.path.join(HERE, 'fonts', font_file))
        glyphs = {ch: render(f, ch) for ch in '0123456789:'}
        for suffix, times in [('', TIMES_12H), ('n', TIMES_24H)]:
            sq = fit(glyphs, times)
            g = squeezed(glyphs, sq)
            for ch, im in g.items():
                name = '%s%s_%s.png' % (prefix, suffix, 'colon' if ch == ':' else ch)
                save_2bit(im, os.path.join(OUT, name))
            print('%-10s squeeze %.2f  widest %dpx' % (prefix + suffix, sq, widest(g, times)))


if __name__ == '__main__':
    main()
