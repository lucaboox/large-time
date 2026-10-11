module.exports = [
  { type: 'heading', defaultValue: 'LargeTime' },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Colors' },
      { type: 'color', messageKey: 'BgColor', label: 'Background', defaultValue: '000000' },
      { type: 'color', messageKey: 'TimeColor', label: 'Time', defaultValue: 'FFFFFF' },
      { type: 'color', messageKey: 'BarColor', label: 'Top and bottom bars', defaultValue: '000000' },
      { type: 'color', messageKey: 'AccentColor', label: 'Accent (lines and date box)', defaultValue: '0055FF' },
      { type: 'color', messageKey: 'TextColor', label: 'Bar text', defaultValue: 'FFFFFF' },
      { type: 'color', messageKey: 'BoxTextColor', label: 'Date number (in the box)', defaultValue: 'FFFFFF' },
      {
        type: 'color', messageKey: 'IconColor', label: 'Icons', defaultValue: 'FFFFFF',
        description: 'Used on a black bar. On other bar colors icons are drawn black on white, like TimeStyle.'
      },
      { type: 'toggle', messageKey: 'ShowLines', label: 'Divider lines', defaultValue: true }
    ]
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Time' },
      {
        type: 'select', messageKey: 'TimeFont', label: 'Time font', defaultValue: '0',
        options: [{ label: 'Roboto', value: '0' }, { label: 'Tomorrow', value: '1' }]
      },
      {
        type: 'select', messageKey: 'BarFont', label: 'Date, weather and battery font', defaultValue: '0',
        options: [{ label: 'Gothic (watch default)', value: '0' }, { label: 'Tomorrow', value: '1' }]
      },
      {
        type: 'toggle', messageKey: 'LeadingZero', label: 'Leading zero on hours',
        description: 'Shows 07:51 instead of 7:51. 12/24-hour follows your watch setting.', defaultValue: false
      }
    ]
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Weather' },
      {
        type: 'radiogroup', messageKey: 'TempUnit', label: 'Temperature', defaultValue: '0',
        options: [{ label: 'Celsius', value: '0' }, { label: 'Fahrenheit', value: '1' }]
      },
      {
        type: 'toggle', messageKey: 'WeatherColors', label: 'Colored weather icons',
        description: 'Yellow sun, grey moon, white clouds, blue raindrops, yellow lightning.', defaultValue: true
      },
      {
        type: 'select', messageKey: 'WeatherOutline', label: 'Colored icon outlines (black bar)', defaultValue: '0',
        description: 'On a black bar, outline each shape in its own color (full-size shapes) or in the Icons color.',
        options: [
          { label: 'Same color as each shape', value: '0' },
          { label: 'Icons color', value: '1' }
        ]
      },
      {
        type: 'select', messageKey: 'WeatherInterval', label: 'Update every', defaultValue: '30',
        options: [
          { label: '15 minutes', value: '15' },
          { label: '30 minutes', value: '30' },
          { label: '1 hour', value: '60' },
          { label: '2 hours', value: '120' }
        ]
      },
      {
        type: 'input', messageKey: 'WeatherLocation', label: 'Location override',
        description: 'Leave empty to use GPS. Or type a city, e.g. "Chicago".',
        defaultValue: '', attributes: { placeholder: 'Use GPS' }
      }
    ]
  },

  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Battery' },
      {
        type: 'select', messageKey: 'BattColors', label: 'Battery icon color', defaultValue: '2',
        options: [
          { label: 'Same as icon color', value: '0' },
          { label: 'Icon color, red at 20% or less', value: '1' },
          { label: 'By level: green / yellow (50%) / red (20%)', value: '2' }
        ]
      },
      { type: 'toggle', messageKey: 'BtVibe', label: 'Vibrate on Bluetooth disconnect', defaultValue: true }
    ]
  },

  { type: 'submit', defaultValue: 'Save' }
];
