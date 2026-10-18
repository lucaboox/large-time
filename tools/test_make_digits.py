"""Regression checks for clipped digit edges. Run with python -m unittest discover -s tools."""
import os
import unittest

from PIL import Image, ImageDraw

import make_digits as digits


class DigitRenderingTests(unittest.TestCase):
    def test_complete_glyphs_fit_with_vertical_padding(self):
        for tag, height, _, _ in digits.DISPLAYS:
            for prefix, filename in digits.FONTS:
                font = digits.font_for_height(os.path.join(digits.HERE, 'fonts', filename), height)
                top, bottom = digits.digit_bounds(font)
                self.assertLessEqual(bottom - top, height)
                for ch in '0123456789:':
                    with self.subTest(display=tag, font=prefix, glyph=ch):
                        actual = digits.render(font, ch, height)
                        # Render on a roomy canvas, then compare the entire ink
                        # shape. This catches even a single missing edge row.
                        bounds = font.getbbox(ch)
                        reference = Image.new('L', (bounds[2] - bounds[0] + 16,
                                                    bounds[3] - bounds[1] + 16), 0)
                        ImageDraw.Draw(reference).text((8 - bounds[0], 8 - bounds[1]),
                                                       ch, font=font, fill=255)
                        expected_ink = reference.crop(reference.getbbox())
                        actual_ink = actual.crop(actual.getbbox())
                        self.assertEqual(actual_ink.size, expected_ink.size)
                        self.assertEqual(actual_ink.tobytes(), expected_ink.tobytes())
                        self.assertGreaterEqual(actual.getbbox()[1], digits.VERTICAL_PAD)
                        self.assertLessEqual(actual.getbbox()[3], actual.height - digits.VERTICAL_PAD)


if __name__ == '__main__':
    unittest.main()
