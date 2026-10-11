# LargeTime

A Pebble Time (basalt, 144x168) watchface: huge time in the middle, the date
on top, weather and battery on the bottom.

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
- Time font: Roboto or Tomorrow; bar text: Gothic or Tomorrow
- Colors for background, time, bars, accent (lines + date box), text, icons
- Weather from [Open-Meteo](https://open-meteo.com) (no API key), GPS or a city name, C/F
- Battery colors by level (green / yellow at 50% / red at 20%), Bluetooth disconnect vibration

## How the big digits work

Pebble's font system can't render glyphs this large, so the digits are
pre-rendered images (`resources/digits`) with 4 alpha levels for smooth edges.
The watch recolors them in place, so any time color works with no runtime cost.
Two time fonts are included, Roboto Bold (Apache 2.0) and Tomorrow SemiBold
(SIL OFL 1.1, https://github.com/MonicaRizzolli/Tomorrow); Tomorrow can also be
used for the bar text. Regenerate the digits with `tools/make_digits.py`:

```bash
python tools/make_digits.py
```

## Build

```bash
pebble build
pebble install --emulator basalt
```

Weather and battery icons are from [TimeStyle](https://github.com/freakified/TimeStylePebble)
by freakified (MIT, see `resources/icons/TIMESTYLE_LICENSE`). `src/pkjs/clay.js` is the
Clay config library (MIT, see `src/pkjs/CLAY_LICENSE`).
