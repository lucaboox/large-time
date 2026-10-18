#include <pebble.h>

// ---------------------------------------------------------------------------
// LargeTime: huge time in the middle, date bar on top, weather + battery bar
// on the bottom. Native layouts for Pebble Time and Pebble Time 2.
//
// Performance: one canvas layer, minute ticks only, strings formatted when
// their data changes, digits are pre-rendered 2-bit images (recolored once
// per settings change), icons are TimeStyle's line-art draw commands.
// ---------------------------------------------------------------------------

#define KEY_SETTINGS 1
#define KEY_WEATHER  2
#define KEY_BACKLIGHT 3
#define SETTINGS_VERSION 6
#define DEG "\xc2\xb0"

// Native display layouts. Emery uses separately rendered 92px digit resources.
#if defined(PBL_PLATFORM_EMERY)
#define SCREEN_W 200
#define TOP_H 38
#define BOT_Y 188
#define LINE_W 5
#define DIGIT_GAP 4
#define ICON 34
#define EDGE 5
#define BAR_GOTHIC FONT_KEY_GOTHIC_28_BOLD
#define BAR_TOMORROW RESOURCE_ID_FONT_TOMORROW_24
#define GOTHIC_OFF 10
#define GOTHIC_CAP 20
#define TOMORROW_OFF 5
#define TOMORROW_CAP 18
#define AMPM_FONT FONT_KEY_GOTHIC_24_BOLD
#define AMPM_OFF 9
#define AMPM_CAP 16
#else
#define SCREEN_W 144
#define TOP_H 28        // top bar height
#define BOT_Y 138       // bottom bar starts here (line + 26px for the 25px icons)
#define LINE_W 4        // divider line thickness
#define DIGIT_GAP 3     // preferred space between digits (tightens for wide times)
#define ICON 25         // native size of the line-art icons
#define EDGE 4          // side margin in the bars
#define BAR_GOTHIC FONT_KEY_GOTHIC_24_BOLD
#define BAR_TOMORROW RESOURCE_ID_FONT_TOMORROW_18
#define GOTHIC_OFF 9
#define GOTHIC_CAP 16
#define TOMORROW_OFF 4
#define TOMORROW_CAP 14
#define AMPM_FONT FONT_KEY_GOTHIC_18_BOLD
#define AMPM_OFF 6
#define AMPM_CAP 12
#endif

// PDC coordinates are authored on a 25px canvas. Resize vectors once on load.
#define ICON_PX(v) (((v) * ICON + 12) / 25)
#define BATTERY_W ICON_PX(26)

// Weather icon ids (must match src/pkjs/index.js)
enum {
  I_CLEAR_DAY = 0, I_CLEAR_NIGHT, I_PARTLY_DAY, I_PARTLY_NIGHT, I_CLOUDY,
  I_RAIN, I_SNOW, I_THUNDER, I_FOG, I_UNKNOWN,
  I_HEAVY_RAIN = 18, I_HEAVY_SNOW, I_SLEET,
  I_WX_MAX
};

typedef struct {
  uint8_t version;
  GColor bg, time, bar, accent, text, icon, box_text;
  bool show_lines, leading_zero, temp_f, wx_colors, bt_vibe;
  uint8_t batt_colors;       // 0 = icon color, 1 = red when low, 2 = green/yellow/red by level
  uint8_t weather_interval;  // minutes
  uint8_t wx_outline;        // colored weather outlines on a dark bar: 0 = match each shape, 1 = icon color
  uint8_t time_font;         // index into DIGIT_RES (see the TimeFont setting)
  uint8_t bar_font;          // 0 = Gothic (system), 1 = Tomorrow
  uint8_t batt_display;      // 0 = icon + percent, 1 = percent only, 2 = icon only
  uint8_t wx_order;          // 0 = temperature then icon, 1 = icon then temperature
  bool show_ampm;            // AM/PM under the time (12-hour mode only)
  GColor ampm;
} Settings;

#if defined(PBL_RGB_BACKLIGHT)
// Kept separate so upgrading preserves the existing Settings v6 record.
typedef struct {
  uint8_t version;
  bool custom;
  uint32_t rgb;
} BacklightSettings;

static BacklightSettings s_backlight;
#endif

typedef struct {
  int16_t temp10;  // tenths of a degree Celsius
  uint8_t icon;
  time_t ts;       // 0 = never fetched
} Weather;

static Window *s_window;
static Layer *s_canvas;
static Settings s;
static Weather s_wx;
static GFont s_font;
static GFont s_ampm_font;
static GFont s_tomorrow;       // custom bar font, loaded only when selected
static int s_font_off, s_font_cap;

static GBitmap *s_digit[11];  // 0-9, 10 = colon
static bool s_narrow;          // which digit set is loaded
static uint8_t s_digit_font;
static bool s_digits_loaded;
static GDrawCommandImage *s_wx_img, *s_batt_img, *s_charge_img;
static uint8_t s_wx_img_icon = 0xFF;

static char s_time[6];
static char s_day[8], s_daynum[4], s_month[8];
static char s_temp[8], s_pct[6];
static struct tm s_tm;
static BatteryChargeState s_batt;
static bool s_bt = true;
static time_t s_last_wx_req;

// Normal digits fit any 12-hour time; 24-hour or leading-zero times
// ("23:48", "09:44") start with a wide digit and use the narrower set.
#define DIGIT_SET(P) { RESOURCE_ID_##P##_0, RESOURCE_ID_##P##_1, RESOURCE_ID_##P##_2, \
  RESOURCE_ID_##P##_3, RESOURCE_ID_##P##_4, RESOURCE_ID_##P##_5, RESOURCE_ID_##P##_6, \
  RESOURCE_ID_##P##_7, RESOURCE_ID_##P##_8, RESOURCE_ID_##P##_9, RESOURCE_ID_##P##_COLON }

// [font][narrow][glyph]; font order matches the TimeFont setting
#define NUM_TIME_FONTS 8
static const uint32_t DIGIT_RES[NUM_TIME_FONTS][2][11] = {
  { DIGIT_SET(ROBOTO), DIGIT_SET(ROBOTON) },
  { DIGIT_SET(TOMORROW), DIGIT_SET(TOMORROWN) },
  { DIGIT_SET(BARLOWL), DIGIT_SET(BARLOWLN) },
  { DIGIT_SET(BARLOWR), DIGIT_SET(BARLOWRN) },
  { DIGIT_SET(BARLOWM), DIGIT_SET(BARLOWMN) },
  { DIGIT_SET(BARLOWS), DIGIT_SET(BARLOWSN) },
  { DIGIT_SET(BARLOWB), DIGIT_SET(BARLOWBN) },
  { DIGIT_SET(BEBAS), DIGIT_SET(BEBASN) },
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int div_round(int n, int d) { return n >= 0 ? (n + d / 2) / d : (n - d / 2) / d; }

static void upcase(char *p) {
  for (; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
}

static int32_t tuple_int(Tuple *t) {
  if (t->type == TUPLE_CSTRING) return atoi(t->value->cstring);
  switch (t->length) {
    case 1: return t->type == TUPLE_INT ? t->value->int8 : t->value->uint8;
    case 2: return t->type == TUPLE_INT ? t->value->int16 : t->value->uint16;
    default: return t->value->int32;
  }
}

// Digit images carry alpha levels only; swap their color, keep the alpha
static void recolor_digits(void);

static void load_digits(void) {
  bool narrow = clock_is_24h_style() || s.leading_zero;
  uint8_t font = s.time_font < NUM_TIME_FONTS ? s.time_font : 0;
  if (s_digits_loaded && narrow == s_narrow && font == s_digit_font) return;
  for (int i = 0; i < 11; i++) {
    if (s_digit[i]) gbitmap_destroy(s_digit[i]);
    s_digit[i] = gbitmap_create_with_resource(DIGIT_RES[font][narrow][i]);
  }
  s_narrow = narrow;
  s_digit_font = font;
  s_digits_loaded = true;
  recolor_digits();
}

static void recolor_digits(void) {
  for (int i = 0; i < 11; i++) {
    if (!s_digit[i]) continue;
    GColor *pal = gbitmap_get_palette(s_digit[i]);
    if (!pal) continue;
    int n;
    switch (gbitmap_get_format(s_digit[i])) {
      case GBitmapFormat1BitPalette: n = 2; break;
      case GBitmapFormat2BitPalette: n = 4; break;
      case GBitmapFormat4BitPalette: n = 16; break;
      default: n = 0; break;
    }
    for (int j = 0; j < n; j++) {
      pal[j].argb = (pal[j].argb & 0xC0) | (s.time.argb & 0x3F);
    }
  }
}

// ---------------------------------------------------------------------------
// Line-art icons (TimeStyle, MIT license)
// ---------------------------------------------------------------------------

static uint32_t wx_resource(uint8_t icon) {
  switch (icon) {
    case I_CLEAR_DAY: return RESOURCE_ID_PDC_CLEAR_DAY;
    case I_CLEAR_NIGHT: return RESOURCE_ID_PDC_CLEAR_NIGHT;
    case I_PARTLY_DAY: return RESOURCE_ID_PDC_PARTLY_CLOUDY;
    case I_PARTLY_NIGHT: return RESOURCE_ID_PDC_PARTLY_CLOUDY_NIGHT;
    case I_CLOUDY:
    case I_FOG: return RESOURCE_ID_PDC_CLOUDY_DAY;
    case I_RAIN: return RESOURCE_ID_PDC_LIGHT_RAIN;
    case I_HEAVY_RAIN: return RESOURCE_ID_PDC_HEAVY_RAIN;
    case I_SNOW: return RESOURCE_ID_PDC_LIGHT_SNOW;
    case I_HEAVY_SNOW: return RESOURCE_ID_PDC_HEAVY_SNOW;
    case I_SLEET: return RESOURCE_ID_PDC_RAINING_AND_SNOWING;
    case I_THUNDER: return RESOURCE_ID_PDC_THUNDERSTORM;
    default: return RESOURCE_ID_PDC_WEATHER_GENERIC;
  }
}

static bool resize_icon_command(GDrawCommand *cmd, uint32_t index, void *context) {
  for (uint16_t i = 0; i < gdraw_command_get_num_points(cmd); i++) {
    GPoint p = gdraw_command_get_point(cmd, i);
    gdraw_command_set_point(cmd, i, GPoint(ICON_PX(p.x), ICON_PX(p.y)));
  }
  if (gdraw_command_get_type(cmd) == GDrawCommandTypeCircle) {
    gdraw_command_set_radius(cmd, ICON_PX(gdraw_command_get_radius(cmd)));
  }
  gdraw_command_set_stroke_width(cmd, ICON_PX(gdraw_command_get_stroke_width(cmd)));
  return true;
}

static GDrawCommandImage *load_icon(uint32_t resource_id) {
  GDrawCommandImage *img = gdraw_command_image_create_with_resource(resource_id);
  if (img && ICON != 25) {
    gdraw_command_list_iterate(gdraw_command_image_get_command_list(img), resize_icon_command, NULL);
    GSize size = gdraw_command_image_get_bounds_size(img);
    gdraw_command_image_set_bounds_size(img, GSize(ICON_PX(size.w), ICON_PX(size.h)));
  }
  return img;
}

static void load_wx_icon(void) {
  uint8_t icon = s_wx.ts ? s_wx.icon : I_UNKNOWN;
  if (s_wx_img && icon == s_wx_img_icon) return;
  if (s_wx_img) gdraw_command_image_destroy(s_wx_img);
  s_wx_img = load_icon(wx_resource(icon));
  s_wx_img_icon = icon;
}

static bool color_cb(GDrawCommand *cmd, uint32_t index, void *context) {
  GColor *c = context;
  gdraw_command_set_fill_color(cmd, c[0]);
  gdraw_command_set_stroke_color(cmd, c[1]);
  return true;
}

static void draw_pdc(GContext *ctx, GDrawCommandImage *img, int x, int y, GColor fill, GColor stroke) {
  if (!img) return;
  GColor c[2] = { fill, stroke };
  gdraw_command_list_iterate(gdraw_command_image_get_command_list(img), color_cb, c);
  gdraw_command_image_draw(ctx, img, GPoint(x, y));
}

// Like TimeStyle: on a black bar icons get black centers and colored lines;
// on any other bar, white centers with black lines.
static bool dark_bar(void) { return gcolor_equal(s.bar, GColorBlack); }
static GColor icon_fill(void) { return dark_bar() ? GColorBlack : GColorWhite; }
static GColor icon_stroke(GColor c) { return dark_bar() ? c : GColorBlack; }

// Colored weather icons: every shape inside the icon gets its own color.
// One letter per draw command, in file order:
//   S sun (yellow fill)   R sun ray (yellow line)   M moon (grey fill)
//   C cloud (white fill)  B back cloud (light grey) K storm cloud (grey)
//   L lightning (yellow)  D raindrop (blue line)    F snowflake line
//   x detail line inside a shape (dark)
static const char *wx_parts(uint8_t icon) {
  switch (icon) {
    case I_CLEAR_DAY: return "SRRRRRRRR";
    case I_CLEAR_NIGHT: return "M";
    case I_PARTLY_DAY: return "SRRCxRR";
    case I_PARTLY_NIGHT: return "MCx";
    case I_CLOUDY:
    case I_FOG: return "CBxx";
    case I_RAIN:
    case I_HEAVY_RAIN: return "CxDDDD";
    case I_SNOW: return "CxFFFF";
    case I_HEAVY_SNOW: return "CxFFFFFF";
    case I_SLEET: return "CxDDFF";
    case I_THUNDER: return "KLx";
    default: return "SRRCxDDD";
  }
}

typedef struct { const char *parts; uint8_t len; } WxPaint;

static bool wx_color_cb(GDrawCommand *cmd, uint32_t index, void *context) {
  WxPaint *wp = context;
  char part = index < wp->len ? wp->parts[index] : 'x';
  GColor fill = GColorClear, stroke = GColorBlack;
  switch (part) {
    case 'S': fill = GColorYellow; break;
    case 'R': stroke = GColorYellow; break;
    case 'M': fill = GColorLightGray; break;
    case 'C': fill = GColorWhite; break;
    case 'B': fill = GColorLightGray; break;
    case 'K': fill = GColorLightGray; break;
    case 'L': fill = GColorYellow; break;
    case 'D': stroke = GColorVividCerulean; break;
    case 'F': stroke = dark_bar() ? GColorWhite : GColorVividCerulean; break;
    default: break;
  }
  // A black outline vanishes on a dark bar and eats into the shape, so
  // filled shapes get a visible outline there (their own color by default)
  if (dark_bar() && fill.argb != GColorClear.argb) stroke = s.wx_outline ? s.icon : fill;
  gdraw_command_set_fill_color(cmd, fill);
  gdraw_command_set_stroke_color(cmd, stroke);
  return true;
}

static void draw_weather_icon(GContext *ctx, int x, int y) {
  if (!s_wx_img) return;
  if (!s.wx_colors) {
    draw_pdc(ctx, s_wx_img, x, y, icon_fill(), icon_stroke(s.icon));
    return;
  }
  WxPaint wp = { wx_parts(s_wx_img_icon), 0 };
  wp.len = strlen(wp.parts);
  gdraw_command_list_iterate(gdraw_command_image_get_command_list(s_wx_img), wx_color_cb, &wp);
  gdraw_command_image_draw(ctx, s_wx_img, GPoint(x, y));
}

static GColor batt_level_color(void) {
  uint8_t pct = s_batt.charge_percent;
  if (s.batt_colors == 2) return pct <= 20 ? GColorRed : pct <= 50 ? GColorYellow : GColorGreen;
  if (s.batt_colors == 1 && pct <= 20) return GColorRed;
  return s.icon;
}

static void draw_battery(GContext *ctx, int x, int y) {
  GColor level = batt_level_color();
  GColor fill = icon_fill();
  GColor outline = icon_stroke(s.batt_colors == 2 ? level : s.icon);
  if (!dark_bar() && s.batt_colors == 0) level = GColorBlack;
  if (s.batt_colors == 2) {
    // Level colors wash out on a light center; use a dark one so they pop
    fill = GColorBlack;
    outline = level;
  }
  draw_pdc(ctx, s_batt_img, x, y, fill, outline);
  // TimeStyle's nub is a 2x3px stub; draw a bigger one so it reads as a battery
  graphics_context_set_fill_color(ctx, outline);
  graphics_fill_rect(ctx, GRect(x + ICON_PX(23), y + ICON_PX(8), ICON_PX(3), ICON_PX(7)), 0, GCornerNone);
  if (s_batt.is_charging || s_batt.is_plugged) {
    draw_pdc(ctx, s_charge_img, x, y, outline, fill);  // bolt uses inverted colors
    return;
  }
  int pct = s_batt.charge_percent > 0 ? s_batt.charge_percent : 5;
  graphics_context_set_fill_color(ctx, level);
  graphics_fill_rect(ctx, GRect(x + ICON_PX(3), y + ICON_PX(8), ICON_PX(18) * pct / 100, ICON_PX(8)), 0, GCornerNone);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static int text_width(const char *str) {
  return graphics_text_layout_get_content_size(str, s_font, GRect(0, 0, SCREEN_W, 40),
                                               GTextOverflowModeFill, GTextAlignmentLeft).w;
}

// Where capitals start below the text box top, and their height, per bar font
#define FONT_OFF s_font_off
#define FONT_CAP s_font_cap

static void load_bar_font(void) {
  if (s.bar_font == 1) {
    if (!s_tomorrow) s_tomorrow = fonts_load_custom_font(resource_get_handle(BAR_TOMORROW));
    s_font = s_tomorrow;
    s_font_off = TOMORROW_OFF;
    s_font_cap = TOMORROW_CAP;
  } else {
    if (s_tomorrow) {
      fonts_unload_custom_font(s_tomorrow);
      s_tomorrow = NULL;
    }
    s_font = fonts_get_system_font(BAR_GOTHIC);
    s_font_off = GOTHIC_OFF;
    s_font_cap = GOTHIC_CAP;
  }
}

static void text_left(GContext *ctx, const char *str, int x, int cap_top) {
  graphics_draw_text(ctx, str, s_font, GRect(x, cap_top - FONT_OFF, 100, 32),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

static void text_center(GContext *ctx, const char *str, int cx, int cap_top) {
  graphics_draw_text(ctx, str, s_font, GRect(cx - 40, cap_top - FONT_OFF, 80, 32),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void draw_time(GContext *ctx, GRect b) {
  GBitmap *g[5];
  int n = 0, w = 0;
  for (const char *p = s_time; *p && n < 5; p++) {
    GBitmap *bmp = *p == ':' ? s_digit[10] : s_digit[*p - '0'];
    if (!bmp) continue;
    g[n++] = bmp;
    w += gbitmap_get_bounds(bmp).size.w;
  }
  if (!n) return;
  int gap = DIGIT_GAP;
  while (gap > 1 && w + gap * (n - 1) > b.size.w - 4) gap--;
  w += gap * (n - 1);
  int digit_h = gbitmap_get_bounds(g[0]).size.h;
  int x = (b.size.w - w) / 2;

  // The time stays centered; AM/PM sits centered in the gap below it
  int y = TOP_H + LINE_W + (BOT_Y - TOP_H - LINE_W - digit_h) / 2;
  if (s.show_ampm && !clock_is_24h_style()) {
    int below = y + digit_h;
    int cap_top = below + (BOT_Y - below - AMPM_CAP) / 2;
    graphics_context_set_text_color(ctx, s.ampm);
    graphics_draw_text(ctx, s_tm.tm_hour < 12 ? "AM" : "PM", s_ampm_font,
                       GRect(0, cap_top - AMPM_OFF, b.size.w, 32),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  for (int i = 0; i < n; i++) {
    GSize sz = gbitmap_get_bounds(g[i]).size;
    graphics_draw_bitmap_in_rect(ctx, g[i], GRect(x, y, sz.w, sz.h));
    x += sz.w + gap;
  }
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int w = b.size.w;

  graphics_context_set_fill_color(ctx, s.bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Bars and divider lines
  graphics_context_set_fill_color(ctx, s.bar);
  graphics_fill_rect(ctx, GRect(0, 0, w, TOP_H), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(0, BOT_Y, w, b.size.h - BOT_Y), 0, GCornerNone);
  if (s.show_lines) {
    graphics_context_set_fill_color(ctx, s.accent);
    graphics_fill_rect(ctx, GRect(0, TOP_H, w, LINE_W), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(0, BOT_Y, w, LINE_W), 0, GCornerNone);
  }

  // Top: THU [20] AUG
  int cap = (TOP_H - FONT_CAP) / 2;
  graphics_context_set_text_color(ctx, s.text);
  text_center(ctx, s_day, w / 6, cap);
  text_center(ctx, s_month, w * 5 / 6, cap);
  int bw = text_width(s_daynum) + 12;
  graphics_context_set_fill_color(ctx, s.accent);
  graphics_fill_rect(ctx, GRect(w / 2 - bw / 2, 0, bw, TOP_H), 0, GCornerNone);
  graphics_context_set_text_color(ctx, s.box_text);
  text_center(ctx, s_daynum, w / 2, cap);

  // Time
  draw_time(ctx, b);

  // Bottom: [temp][weather icon] ........ [battery icon][percent]
  int bar_top = BOT_Y + (s.show_lines ? LINE_W : 0);
  int bar_h = b.size.h - bar_top;
  int bcap = bar_top + (bar_h - FONT_CAP) / 2;
  int iy = bar_top + (bar_h - ICON) / 2;

  graphics_context_set_text_color(ctx, s.text);
  if (s.wx_order == 1) {
    draw_weather_icon(ctx, EDGE, iy);
    text_left(ctx, s_temp, EDGE + ICON + ICON_PX(3), bcap);
  } else {
    text_left(ctx, s_temp, EDGE, bcap);
    draw_weather_icon(ctx, EDGE + text_width(s_temp) + ICON_PX(2), iy);
  }

  // Include the battery nub when aligning the icon.
  if (s.batt_display == 2) {
    draw_battery(ctx, w - EDGE - BATTERY_W, iy);
  } else {
    int pw = text_width(s_pct);
    text_left(ctx, s_pct, w - EDGE - pw, bcap);
    if (s.batt_display == 0) draw_battery(ctx, w - EDGE - pw - ICON_PX(4) - BATTERY_W, iy);
  }
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

static void update_time(void) {
  int hour = s_tm.tm_hour;
  if (!clock_is_24h_style()) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  snprintf(s_time, sizeof(s_time), s.leading_zero ? "%02d:%02d" : "%d:%02d", hour, s_tm.tm_min);
}

static void update_date(void) {
  strftime(s_day, sizeof(s_day), "%a", &s_tm);
  strftime(s_month, sizeof(s_month), "%b", &s_tm);
  snprintf(s_daynum, sizeof(s_daynum), "%d", s_tm.tm_mday);
  upcase(s_day);
  upcase(s_month);
}

static void update_weather_text(void) {
  if (!s_wx.ts) {
    snprintf(s_temp, sizeof(s_temp), "--");
  } else {
    int t = s.temp_f ? div_round(s_wx.temp10 * 9 + 1600, 50) : div_round(s_wx.temp10, 10);
    snprintf(s_temp, sizeof(s_temp), "%d" DEG, t);
  }
  load_wx_icon();
}

static void request_weather(void) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_uint8(it, MESSAGE_KEY_RequestWeather, 1);
  app_message_outbox_send();
  s_last_wx_req = time(NULL);
}

static void maybe_request_weather(void) {
  if (!s_bt) return;
  time_t now = time(NULL);
  int interval = (s.weather_interval ? s.weather_interval : 30) * 60;
  if (now - s_wx.ts >= interval && now - s_last_wx_req >= 600) request_weather();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  s_tm = *tick_time;
  load_digits();  // no-op unless 12/24-hour changed
  update_time();
  if (units_changed & DAY_UNIT) update_date();
  maybe_request_weather();
  layer_mark_dirty(s_canvas);
}

static void battery_handler(BatteryChargeState state) {
  s_batt = state;
  snprintf(s_pct, sizeof(s_pct), "%d%%", state.charge_percent);
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void bt_handler(bool connected) {
  if (!connected && s_bt && s.bt_vibe) vibes_double_pulse();
  s_bt = connected;
  if (connected) maybe_request_weather();
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

static void load_backlight_settings(void) {
#if defined(PBL_RGB_BACKLIGHT)
  s_backlight = (BacklightSettings){ .version = 1, .custom = false, .rgb = 0xFFFFFF };
  BacklightSettings saved;
  if (persist_read_data(KEY_BACKLIGHT, &saved, sizeof(saved)) == (int)sizeof(saved) && saved.version == 1) {
    s_backlight = saved;
    s_backlight.rgb &= 0xFFFFFF;
  }
#endif
}

static void apply_backlight_color(void) {
#if defined(PBL_RGB_BACKLIGHT)
  if (s_backlight.custom) light_set_color_rgb888(s_backlight.rgb);
  else light_set_system_color();
#endif
}

static void app_focus_changed(bool focused) {
  // A notification/modal temporarily returns the light to the system color.
  if (focused) apply_backlight_color();
}

static void default_settings(void) {
  s = (Settings){
    .version = SETTINGS_VERSION,
    .bg = GColorBlack,
    .time = GColorWhite,
    .bar = GColorBlack,
    .accent = GColorBlueMoon,
    .text = GColorWhite,
    .icon = GColorWhite,
    .box_text = GColorWhite,
    .show_lines = true,
    .leading_zero = false,
    .temp_f = false,
    .wx_colors = true,
    .bt_vibe = true,
    .batt_colors = 2,
    .weather_interval = 30,
    .wx_outline = 0,
    .time_font = 5,  // Barlow Condensed SemiBold
    .bar_font = 0,
    .batt_display = 0,
    .wx_order = 0,
    .show_ampm = true,
    .ampm = GColorLightGray,
  };
}

static void apply_settings(void) {
  apply_backlight_color();
  window_set_background_color(s_window, s.bg);
  load_bar_font();
  load_digits();
  recolor_digits();
  update_time();
  update_date();
  update_weather_text();
  maybe_request_weather();
  layer_mark_dirty(s_canvas);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  bool changed = false;
  bool backlight_changed = false;

#define READ_COLOR(key, field) \
  if ((t = dict_find(iter, MESSAGE_KEY_##key))) { s.field = GColorFromHEX(tuple_int(t)); changed = true; }
#define READ_BOOL(key, field) \
  if ((t = dict_find(iter, MESSAGE_KEY_##key))) { s.field = tuple_int(t) != 0; changed = true; }
#define READ_U8(key, field) \
  if ((t = dict_find(iter, MESSAGE_KEY_##key))) { s.field = (uint8_t)tuple_int(t); changed = true; }

  READ_COLOR(BgColor, bg)
  READ_COLOR(TimeColor, time)
  READ_COLOR(BarColor, bar)
  READ_COLOR(AccentColor, accent)
  READ_COLOR(TextColor, text)
  READ_COLOR(IconColor, icon)
  READ_COLOR(BoxTextColor, box_text)
  READ_BOOL(ShowLines, show_lines)
  READ_BOOL(LeadingZero, leading_zero)
  READ_BOOL(TempUnit, temp_f)
  READ_BOOL(WeatherColors, wx_colors)
  READ_BOOL(BtVibe, bt_vibe)
  READ_U8(BattColors, batt_colors)
  READ_U8(WeatherInterval, weather_interval)
  READ_U8(WeatherOutline, wx_outline)
  READ_U8(TimeFont, time_font)
  READ_U8(BarFont, bar_font)
  READ_U8(BattDisplay, batt_display)
  READ_U8(WeatherOrder, wx_order)
  READ_BOOL(ShowAmPm, show_ampm)
  READ_COLOR(AmPmColor, ampm)

#if defined(PBL_RGB_BACKLIGHT)
  if ((t = dict_find(iter, MESSAGE_KEY_UseCustomBacklight))) {
    s_backlight.custom = tuple_int(t) != 0;
    backlight_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_BacklightColor))) {
    s_backlight.rgb = (uint32_t)tuple_int(t) & 0xFFFFFF;
    backlight_changed = true;
  }
  if (backlight_changed) persist_write_data(KEY_BACKLIGHT, &s_backlight, sizeof(s_backlight));
#endif

  if ((t = dict_find(iter, MESSAGE_KEY_WeatherTemp))) {
    s_wx.temp10 = tuple_int(t);
    if ((t = dict_find(iter, MESSAGE_KEY_WeatherIcon))) s_wx.icon = tuple_int(t);
    s_wx.ts = time(NULL);
    persist_write_data(KEY_WEATHER, &s_wx, sizeof(s_wx));
    update_weather_text();
    layer_mark_dirty(s_canvas);
  }

  if (changed) {
    persist_write_data(KEY_SETTINGS, &s, sizeof(s));
  }
  if (changed || backlight_changed) apply_settings();
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

static void init(void) {
  default_settings();
  load_backlight_settings();
  Settings tmp;
  if (persist_read_data(KEY_SETTINGS, &tmp, sizeof(tmp)) == (int)sizeof(tmp) && tmp.version == SETTINGS_VERSION) s = tmp;
  if (persist_read_data(KEY_WEATHER, &s_wx, sizeof(s_wx)) != (int)sizeof(s_wx)) memset(&s_wx, 0, sizeof(s_wx));

  s_ampm_font = fonts_get_system_font(AMPM_FONT);
  s_batt_img = load_icon(RESOURCE_ID_PDC_BATTERY_BG);
  s_charge_img = load_icon(RESOURCE_ID_PDC_BATTERY_CHARGE);

  time_t now = time(NULL);
  s_tm = *localtime(&now);
  battery_handler(battery_state_service_peek());
  s_bt = connection_service_peek_pebble_app_connection();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, false);

  app_message_register_inbox_received(inbox_received);
  app_message_open(384, 64);

  apply_settings();
  app_focus_service_subscribe(app_focus_changed);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  connection_service_subscribe((ConnectionHandlers){ .pebble_app_connection_handler = bt_handler });
}

static void deinit(void) {
  app_focus_service_unsubscribe();
#if defined(PBL_RGB_BACKLIGHT)
  light_set_system_color();
#endif
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  window_destroy(s_window);
  for (int i = 0; i < 11; i++) if (s_digit[i]) gbitmap_destroy(s_digit[i]);
  if (s_wx_img) gdraw_command_image_destroy(s_wx_img);
  if (s_tomorrow) fonts_unload_custom_font(s_tomorrow);
  gdraw_command_image_destroy(s_batt_img);
  gdraw_command_image_destroy(s_charge_img);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
