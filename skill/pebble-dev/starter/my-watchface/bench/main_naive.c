// main_naive.c —— **对照组**，不参与手表构建。
//
// 与 src/c/ai-watchface.c 界面完全一致，但字符串处理全部走
// snprintf / strftime / strcmp（即"AI 直接写出来、不做优化"的典型形态）。
// 把它单独编译成 .o，用 size 对比，才能量出 [OPT6] 到底省了多少。
//
// 注意：这里重复 include 一次 fmt.h 只是为了断言——它没有用到 fmt.c 的函数，
// 编译时链接的是 libc 的 printf 家族。
#include <pebble.h>

#define TEXT_FONT_KEY FONT_KEY_GOTHIC_18_BOLD
#define TIME_BUF_LEN 8
#define DATE_BUF_LEN 8
#define BATT_BUF_LEN 6

static Window *s_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_batt_layer;
static char s_time_buf[TIME_BUF_LEN];
static char s_date_buf[DATE_BUF_LEN];
static char s_batt_buf[BATT_BUF_LEN];

static void refresh_time(int hour24, int minute, int wday, int mday) {
  static const char *const D[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_hour = hour24;
  t.tm_min = minute;

  if (clock_is_24h_style()) {
    strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", &t);
  } else {
    strftime(s_time_buf, sizeof(s_time_buf), "%I:%M", &t);
    if (s_time_buf[0] == '0') {
      memmove(s_time_buf, s_time_buf + 1, strlen(s_time_buf));
    }
  }
  text_layer_set_text(s_time_layer, s_time_buf);

  char next[DATE_BUF_LEN];
  snprintf(next, sizeof(next), "%s %d", D[wday], mday);
  if (strcmp(s_date_buf, next) != 0) {
    strncpy(s_date_buf, next, sizeof(s_date_buf) - 1);
    s_date_buf[sizeof(s_date_buf) - 1] = '\0';
    text_layer_set_text(s_date_layer, s_date_buf);
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  refresh_time(tick_time->tm_hour, tick_time->tm_min, tick_time->tm_wday,
               tick_time->tm_mday);
}

static void battery_handler(BatteryChargeState state) {
  snprintf(s_batt_buf, sizeof(s_batt_buf), "%d%%%s", state.charge_percent,
           state.is_charging ? "+" : "");
  text_layer_set_text(s_batt_layer, s_batt_buf);
}

static TextLayer *make_layer(GRect frame, const char *font_key, GTextAlignment align) {
  TextLayer *layer = text_layer_create(frame);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, GColorWhite);
  text_layer_set_text_alignment(layer, align);
  text_layer_set_font(layer, fonts_get_system_font(font_key));
  return layer;
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);
  const int w = bounds.size.w;
  const int h = bounds.size.h;

  window_set_background_color(window, GColorBlack);
  s_time_layer = make_layer(GRect(0, h / 8, w, 50), FONT_KEY_BITHAM_42_MEDIUM_NUMBERS, GTextAlignmentCenter);
  s_date_layer = make_layer(GRect(0, (h * 5) / 12, w, 22), TEXT_FONT_KEY, GTextAlignmentCenter);
  s_batt_layer = make_layer(GRect(0, h - 26, w, 20), TEXT_FONT_KEY, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_time_layer));
  layer_add_child(root, text_layer_get_layer(s_date_layer));
  layer_add_child(root, text_layer_get_layer(s_batt_layer));

  time_t now = time(NULL);
  struct tm *local = localtime(&now);
  refresh_time(local->tm_hour, local->tm_min, local->tm_wday, local->tm_mday);
  battery_handler(battery_state_service_peek());
}

static void window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_batt_layer);
}

static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
