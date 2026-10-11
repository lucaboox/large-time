"""Generate the big time digits for LargeTime.

Renders 0-9 and ':' from a TTF as 2-bit palettized PNGs (transparent plus
three alpha levels of white) so the watch can recolor them at runtime and
keep smooth anti-aliased edges. Pebble's font system caps glyph size, which
is why the large digits are images.

Usage: python tools/make_digits.py path/to/Roboto-Bold.ttf
Requires Pillow.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

DIGIT_H = 66     # ink height of the digits in pixels
# Horizontal scale per digit set: 12-hour times never start with a wide digit,
# 24-hour times like "23:48" need a narrower set to fit the 144px screen.
SETS = [('digit', 0.71), ('digitn', 0.65)]
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'resources', 'digits')


def font_for_height(path):
    size = DIGIT_H
    for _ in range(8):
        f = ImageFont.truetype(path, size)
        bb = f.getbbox('0')
        size = max(1, int(round(size * DIGIT_H / (bb[3] - bb[1]))))
    return ImageFont.truetype(path, size)


def render(f, ch, squeeze):
    ref = f.getbbox('0')
    bb = f.getbbox(ch)
    im = Image.new('L', (bb[2] - bb[0] + 8, DIGIT_H + 8), 0)
    ImageDraw.Draw(im).text((4 - bb[0], 4 - ref[1]), ch, font=f, fill=255)
    im = im.crop((0, 4, im.width, 4 + DIGIT_H))  # keep the shared baseline/height
    ink = im.getbbox()
    im = im.crop((ink[0], 0, ink[2], DIGIT_H))
    w = max(1, int(round(im.width * squeeze)))
    return im.resize((w, DIGIT_H), Image.LANCZOS)


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
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    f = font_for_height(sys.argv[1])
    os.makedirs(OUT, exist_ok=True)
    for prefix, squeeze in SETS:
        for ch, name in [(str(d), '%s_%d.png' % (prefix, d)) for d in range(10)] + [(':', '%s_colon.png' % prefix)]:
            im = render(f, ch, squeeze)
            save_2bit(im, os.path.join(OUT, name))
            print(name, im.size)


if __name__ == '__main__':
    main()
