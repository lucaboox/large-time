#include <pebble.h>

// ---------------------------------------------------------------------------
// LargeTime: huge time in the middle, date bar on top, weather + battery bar
// on the bottom. Built for the 144x168 Pebble Time.
//
// Performance: one canvas layer, minute ticks only, strings formatted when
// their data changes, digits are pre-rendered 2-bit images (recolored once
// per settings change), icons are TimeStyle's line-art draw commands.
// ---------------------------------------------------------------------------

#define KEY_SETTINGS 1
#define KEY_WEATHER  2
#define SETTINGS_VERSION 1
#define DEG "\xc2\xb0"

// Layout (144x168)
#define TOP_H 28        // top bar height
#define BOT_Y 140       // bottom bar starts here
#define LINE_W 2        // divider line thickness
#define DIGIT_GAP 3     // preferred space between digits (tightens for wide times)
#define ICON 25         // native size of the line-art icons
#define EDGE 4          // side margin in the bars

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
} Settings;

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

static GBitmap *s_digit[11];  // 0-9, 10 = colon
static bool s_narrow;          // which digit set is loaded
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
static const uint32_t DIGIT_RES[2][11] = {
  { RESOURCE_ID_DIGIT_0, RESOURCE_ID_DIGIT_1, RESOURCE_ID_DIGIT_2, RESOURCE_ID_DIGIT_3,
    RESOURCE_ID_DIGIT_4, RESOURCE_ID_DIGIT_5, RESOURCE_ID_DIGIT_6, RESOURCE_ID_DIGIT_7,
    RESOURCE_ID_DIGIT_8, RESOURCE_ID_DIGIT_9, RESOURCE_ID_DIGIT_COLON },
  { RESOURCE_ID_DIGITN_0, RESOURCE_ID_DIGITN_1, RESOURCE_ID_DIGITN_2, RESOURCE_ID_DIGITN_3,
    RESOURCE_ID_DIGITN_4, RESOURCE_ID_DIGITN_5, RESOURCE_ID_DIGITN_6, RESOURCE_ID_DIGITN_7,
    RESOURCE_ID_DIGITN_8, RESOURCE_ID_DIGITN_9, RESOURCE_ID_DIGITN_COLON },
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
  if (s_digits_loaded && narrow == s_narrow) return;
  for (int i = 0; i < 11; i++) {
    if (s_digit[i]) gbitmap_destroy(s_digit[i]);
    s_digit[i] = gbitmap_create_with_resource(DIGIT_RES[narrow][i]);
  }
  s_narrow = narrow;
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

static void load_wx_icon(void) {
  uint8_t icon = s_wx.ts ? s_wx.icon : I_UNKNOWN;
  if (s_wx_img && icon == s_wx_img_icon) return;
  if (s_wx_img) gdraw_command_image_destroy(s_wx_img);
  s_wx_img = gdraw_command_image_create_with_resource(wx_resource(icon));
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
  if (s_batt.is_charging || s_batt.is_plugged) {
    draw_pdc(ctx, s_charge_img, x, y, outline, fill);  // bolt uses inverted colors
    return;
  }
  int pct = s_batt.charge_percent > 0 ? s_batt.charge_percent : 5;
  graphics_context_set_fill_color(ctx, level);
  graphics_fill_rect(ctx, GRect(x + 3, y + 8, 18 * pct / 100, 8), 0, GCornerNone);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static int text_width(const char *str) {
  return graphics_text_layout_get_content_size(str, s_font, GRect(0, 0, 144, 40),
                                               GTextOverflowModeFill, GTextAlignmentLeft).w;
}

// Gothic 24 Bold: caps start ~9px below the text box top and are ~16px tall
#define FONT_OFF 9
#define FONT_CAP 16

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
  int y = TOP_H + LINE_W + (BOT_Y - TOP_H - LINE_W - digit_h) / 2;
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
  text_left(ctx, s_temp, EDGE, bcap);
  draw_weather_icon(ctx, EDGE + text_width(s_temp) + 2, iy);

  int pw = text_width(s_pct);
  text_left(ctx, s_pct, w - EDGE - pw, bcap);
  draw_battery(ctx, w - EDGE - pw - 2 - ICON, iy);
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
  };
}

static void apply_settings(void) {
  window_set_background_color(s_window, s.bg);
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
    apply_settings();
  }
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
  Settings tmp;
  if (persist_read_data(KEY_SETTINGS, &tmp, sizeof(tmp)) == (int)sizeof(tmp) && tmp.version == SETTINGS_VERSION) s = tmp;
  if (persist_read_data(KEY_WEATHER, &s_wx, sizeof(s_wx)) != (int)sizeof(s_wx)) memset(&s_wx, 0, sizeof(s_wx));

  s_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_batt_img = gdraw_command_image_create_with_resource(RESOURCE_ID_PDC_BATTERY_BG);
  s_charge_img = gdraw_command_image_create_with_resource(RESOURCE_ID_PDC_BATTERY_CHARGE);

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

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  connection_service_subscribe((ConnectionHandlers){ .pebble_app_connection_handler = bt_handler });
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  window_destroy(s_window);
  for (int i = 0; i < 11; i++) if (s_digit[i]) gbitmap_destroy(s_digit[i]);
  if (s_wx_img) gdraw_command_image_destroy(s_wx_img);
  gdraw_command_image_destroy(s_batt_img);
  gdraw_command_image_destroy(s_charge_img);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
