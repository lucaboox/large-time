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

DIGIT_H = 74     # ink height of the digits in pixels
SQUEEZE = 0.62   # horizontal scale, so "12:59" fits the 144px screen
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'resources', 'digits')


def font_for_height(path):
    size = DIGIT_H
    for _ in range(8):
        f = ImageFont.truetype(path, size)
        bb = f.getbbox('0')
        size = max(1, int(round(size * DIGIT_H / (bb[3] - bb[1]))))
    return ImageFont.truetype(path, size)


def render(f, ch):
    ref = f.getbbox('0')
    bb = f.getbbox(ch)
    im = Image.new('L', (bb[2] - bb[0] + 8, DIGIT_H + 8), 0)
    ImageDraw.Draw(im).text((4 - bb[0], 4 - ref[1]), ch, font=f, fill=255)
    im = im.crop((0, 4, im.width, 4 + DIGIT_H))  # keep the shared baseline/height
    ink = im.getbbox()
    im = im.crop((ink[0], 0, ink[2], DIGIT_H))
    w = max(1, int(round(im.width * SQUEEZE)))
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
    for ch, name in [(str(d), 'digit_%d.png' % d) for d in range(10)] + [(':', 'colon.png')]:
        im = render(f, ch)
        save_2bit(im, os.path.join(OUT, name))
        print(name, im.size)


if __name__ == '__main__':
    main()
