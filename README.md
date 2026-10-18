# LargeTime

A watchface for Pebble Time (basalt, 144x168) and Pebble Time 2 (emery,
200x228): huge time in the middle, the date on top, weather and battery on
the bottom. Time 2 has its own native layout, larger fonts and vector icons,
and digits rendered at 92px high; it does not need compatibility scaling.

```
 THU  [20]  AUG
 ──────────────
     7:51
 ──────────────
 75° ☁     ▭ 80%
```

- Top bar: day, date number in an accent-colored box, month
- Bottom bar: temperature + weather icon on the left, battery icon + percent on the right
- 12/24-hour follows the watch setting; optional leading zero
- Time font: Barlow Condensed (5 weights), Bebas Neue, Roboto or Tomorrow; bar text: Gothic or Tomorrow
- Colors for background, time, bars, accent (lines + date box), text, icons
- Optional custom RGB backlight color on the new Pebble Time 2, with a switch
  to use the watch's default backlight color instead
- Weather from [Open-Meteo](https://open-meteo.com) (no API key), GPS or a city name, C/F
- Battery colors by level (green / yellow at 50% / red at 20%), Bluetooth disconnect vibration

## Pebble Time 2 backlight color

In LargeTime's phone configuration, find **Pebble Time 2 backlight**, enable
**Custom backlight color**, choose **Backlight color**, and save. Turn the
switch off to restore the backlight color selected in the watch's settings.

The color applies whenever the backlight turns on while LargeTime is showing.
The watch still controls brightness, wrist-gesture activation, and timeout.
LargeTime reapplies the tint when returning from a notification and releases
it when you leave the face. This setting only affects RGB-backlight hardware;
the original Pebble Time keeps its normal backlight.

Backlight preferences are saved separately, so existing face colors, fonts,
weather settings and other preferences are preserved when upgrading.

## How the big digits work

Pebble's font system can't render glyphs this large, so the digits are
pre-rendered images (`resources/digits`) with 4 alpha levels for smooth edges.
The watch recolors them in place, so any time color works with no runtime cost.
The SDK selects the `~emery.png` variants for Time 2 and the original 66px
digits for Pebble Time. Both sets are rendered directly from the font files.
Glyphs share a baseline and have transparent vertical padding so their
rounded and antialiased edges are preserved.
Time fonts: Barlow Condensed (SIL OFL 1.1, https://github.com/jpt/barlow),
Bebas Neue (SIL OFL 1.1, https://github.com/dharmatype/Bebas-Neue), Roboto Bold
(Apache 2.0) and Tomorrow SemiBold (SIL OFL 1.1,
https://github.com/MonicaRizzolli/Tomorrow); Tomorrow can also be used for the
bar text. Fonts are only narrowed when a time wouldn't otherwise fit. Regenerate the digits with `tools/make_digits.py`:

```bash
python tools/make_digits.py
python -m unittest discover -s tools
```

## Build

```bash
pebble build
pebble install --emulator basalt
# Or preview the native Pebble Time 2 build:
pebble install --emulator emery
```

The resulting `build/LargeTime.pbw` contains both platforms. Install this new
bundle on Time 2 to use the native version.

Weather and battery icons are from [TimeStyle](https://github.com/freakified/TimeStylePebble)
by freakified (MIT, see `resources/icons/TIMESTYLE_LICENSE`). `src/pkjs/clay.js` is the
Clay config library (MIT, see `src/pkjs/CLAY_LICENSE`).
