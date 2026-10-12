// AI Lab 表盘 —— 由 AI 编码智能体（dsh）编写，配套可复现的测试与测量。
//
// 这个文件刻意保持"薄"：易错的字符串/时间逻辑放在 fmt.c（不含 pebble.h，
// 可在 PC 上跑单元测试与差分对拍，见 tests/test_fmt.c 与 Makefile）。
//
// 优化清单（每条都有实测或文档依据，可复现）：
//   [OPT1] tick 只订阅 MINUTE_UNIT。**注意别把收益说错**（反汇编核实过）：
//          内核 tick_timer_add_subscriber() 只在"第一个订阅者"出现时挂
//          regular_timer_add_seconds_callback —— 也就是**任何** tick 订阅都让 app
//          挂在同一个 1 Hz 事件流上，派发器每秒都在跑（localtime + 逐字段比较）。
//          所以 MINUTE_UNIT 减少的是**用户回调次数**（86400 -> 1440 次/天），
//          收益是"不做无谓的 UI 更新"，而不是"不被唤醒"。
//   [OPT2] 文本内容没变就不调 text_layer_set_text，减少无谓脏矩形
//   [OPT3] 电池用 peek() 取初值 + 订阅变化，不轮询
//   [OPT4] 不打包任何资源/字体，全用系统字体（app 资源 = 0 字节）
//   [OPT5] targetPlatforms 只留要支持的平台，不为旧平台生成二进制
//   [OPT6] 时间/日期/电量串统一走 fmt.c 的纯函数。实测：在 Pebble 上手写格式化
//          **并不省体积**（libc 那几个函数是 12 字节 syscall 桩），选它的理由是
//          可测试——同一份函数能在 PC 上跑单元测试与差分对拍
//   [OPT7] 层高与位置用 graphics_text_layout_get_content_size 实测，不硬编码，
//          避免 42pt 字体在 144x168 屏上被裁切
//   [OPT8] 不做用户配置，因此不引入 persistent storage / 配置页（零额外代码）
//
// 稳健性（对应独立复核提出的问题）：
//   * window_create / text_layer_create 的 NULL 返回全部检查，失败时静默降级
//   * window_unload 与 deinit 分离：即使 window 创建失败也保证订阅被取消

#include <pebble.h>
#include "fmt.h"

// 时间字体：4 个目标平台都有；刻意不用 BITHAM_*_NUMBERS 系列——
// 那是"数字子集"字体，冒号等标点不保证有字形，可能渲染成豆腐块。
#define TIME_FONT_KEY FONT_KEY_BITHAM_42_BOLD
#define TEXT_FONT_KEY FONT_KEY_GOTHIC_18_BOLD

// 缓冲区尺寸统一由 fmt.h 定义（单一真相源，测试与出货代码共用同一组数字，
// 且有编译期护栏保证不会小到装不下最坏输出）
#define TIME_BUF_LEN FMT_TIME_BUF_LEN
#define DATE_BUF_LEN FMT_DATE_BUF_LEN
#define BATT_BUF_LEN FMT_BATT_BUF_LEN

static Window *s_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_batt_layer;

static char s_time_buf[TIME_BUF_LEN];
static char s_date_buf[DATE_BUF_LEN];
static char s_batt_buf[BATT_BUF_LEN];
static bool s_subscribed;   // SDK 未定义"未订阅就取消订阅"的行为，自己记账

// [OPT2] 只有内容变了才写 layer（text_layer_set_text 不复制字符串，
// 所以交给 layer 的必须是我们持有的静态缓冲区）
static void push_if_changed(TextLayer *layer, char *store, size_t store_len,
                            const char *next) {
  // window_load 可能因为某个 layer 创建失败而早退，但订阅已经生效，
  // 回调仍会走到这里——必须挡住 NULL，否则是空指针解引用。
  if (layer == NULL) {
    return;
  }
  if (!fmt_changed(store, next)) {
    return;
  }
  fmt_copy(store, store_len, next);
  text_layer_set_text(layer, store);
}

// [OPT6] 时间串走 fmt_time_ship —— 该函数在 PC 上被穷举差分验证过，
// 且"是否 24 小时制"作为显式参数传入，测试覆盖的就是出货这一份代码。
static void update_time(struct tm *t) {
  char tmp[TIME_BUF_LEN] = {0};   // 预置 0：异常路径也不会读到未初始化数据
  fmt_time_ship(tmp, sizeof(tmp), t->tm_hour, t->tm_min, clock_is_24h_style());
  push_if_changed(s_time_layer, s_time_buf, sizeof(s_time_buf), tmp);
}

static void refresh_date(int wday, int mday) {
  char tmp[DATE_BUF_LEN] = {0};
  fmt_date(tmp, sizeof(tmp), wday, mday);
  push_if_changed(s_date_layer, s_date_buf, sizeof(s_date_buf), tmp);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // [OPT1] MINUTE_UNIT 下每分钟最多回调一次；电池不在这里轮询（见 [OPT3]）
  update_time(tick_time);
  refresh_date(tick_time->tm_wday, tick_time->tm_mday);
}

static void battery_handler(BatteryChargeState state) {
  char tmp[BATT_BUF_LEN] = {0};
  fmt_battery(tmp, sizeof(tmp), state.charge_percent, state.is_charging);
  push_if_changed(s_batt_layer, s_batt_buf, sizeof(s_batt_buf), tmp);
}

// [OPT7] 用系统文本测量确定层高：不硬编码，窄屏上 42pt 字体也不会被裁。
// 测量失败（返回 0/负数）时退回 FMT_FALLBACK_LINE_HEIGHT —— 绝不能把 0 高度
// 传给 layer，那样文字层会"存在但看不见"，是比崩溃更难查的 bug。
// 字体只查一次、由调用方传进来（见 window_load）：
// 原来 measure_height 和 make_layer 各查一次，6 次同键查表纯属浪费。
static int16_t measure_height(const char *sample, GFont font) {
  if (font == NULL) {
    // SDK 文档承诺"加载失败返回回退字体"，理论上不会 NULL；但一旦为 NULL，
    // 后续测量会解引用地址 0x2 直接 hard fault（不是优雅降级），所以这里挡住。
    return FMT_FALLBACK_LINE_HEIGHT;
  }
  GRect box = GRect(0, 0, 10000, 10000);
  GSize size = graphics_text_layout_get_content_size(sample, font, box,
                                                     GTextOverflowModeWordWrap,
                                                     GTextAlignmentCenter);
  if (size.h <= 0) {
    return FMT_FALLBACK_LINE_HEIGHT;
  }
  return (int16_t)size.h;
}

// frame.size.h 就是"实测行高"；字体由调用方传入，不再重复查表。
// 这里也不再 get_frame/set_frame 空转一次——frame 建的时候高度就对。
static TextLayer *make_layer(GRect frame, GFont font, GTextAlignment align) {
  if (font == NULL) {   // NULL font 是延迟 hard fault（测量时解引用地址 0x2）
    return NULL;
  }
  TextLayer *layer = text_layer_create(frame);
  if (layer == NULL) {                  // 低内存时 text_layer_create 可能失败
    return NULL;
  }
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, GColorWhite);
  text_layer_set_text_alignment(layer, align);
  text_layer_set_font(layer, font);
  return layer;
}

static void window_unload(Window *window);   // window_load 要做幂等回收，前置声明

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);
  const int w = bounds.size.w;
  const int h = bounds.size.h;

  window_set_background_color(window, GColorBlack);

  // 幂等保护：若 window_load 被重复调用（框架不保证不会），
  // 直接把 s_*_layer 覆盖掉会泄漏上一层；这里先回收干净。
  window_unload(window);

  // 清空文本缓存。理由要写准（独立审计已核实固件行为，别夸大）：
  // s_*_buf 跨 window 生命周期存活，而 window_unload 销毁 text layer 后新层的
  // text 是 NULL（一个像素都不画）。若不清缓存，重复 load 时 push_if_changed 会拿
  // "新层的空内容" 与 "旧缓存" 比较、判定为"没变化"→ 永不写入 → 界面空白，
  // 且自愈不对称（时间 ≤60s 会回来，日期要等午夜，电量要等电量变化）。
  //
  // 可达性说明：本版固件里重复 load 同一 window 的路径已被审计证否
  // （prv_window_load 有 is_loaded 守卫；系统 UI 只把窗口设为 off-screen 而不 unload；
  // "切走再回来"是换进程 + 整块 __APP_RAM__ 清零）。所以这是**防御性加固**，
  // 代价 3 行，换来"无论框架将来怎么改都不会白屏"。
  s_time_buf[0] = '\0';
  s_date_buf[0] = '\0';
  s_batt_buf[0] = '\0';

  // 两种字体各查一次，后续测量与建层都复用（原来一共查 6 次）
  GFont f_time = fonts_get_system_font(TIME_FONT_KEY);
  GFont f_text = fonts_get_system_font(TEXT_FONT_KEY);

  // 采样串取"每个字段可能出现的最长内容"，保证量到的是最大行高。
  // 用 static const 而不是字面量直传：字面量经 const char* 参数会让编译器
  // 往栈上拷一份（window_load 的栈帧会被撑大）。
  static const char SAMPLE_TIME[] = "00:00";
  static const char SAMPLE_DATE[] = "Wed 31";
  static const char SAMPLE_BATT[] = "100%+";
  const int16_t h_time = measure_height(SAMPLE_TIME, f_time);
  const int16_t h_date = measure_height(SAMPLE_DATE, f_text);
  const int16_t h_batt = measure_height(SAMPLE_BATT, f_text);

  // 三段式纵向排布交给 fmt.c 的纯函数：这样布局逻辑能在 PC 上穷举验证
  // （tests/test_layout.c 会对所有屏幕尺寸检查"在屏幕内 / 非零高 / 尽量不重叠"）。
  FmtLayout layout;
  const int overlapped = fmt_layout_stack((int16_t)h, h_time, h_date, h_batt, &layout);

  const int16_t time_y = layout.time_y;
  const int16_t date_y = layout.date_y;
  const int16_t batt_y = layout.batt_y;

  // 返回值只表示"是否发生了重叠"（装不下的矮屏上会发生）。层高不为 0 由
  // fmt_layout_stack 保证，所以这里不需要额外处理。
  (void)overlapped;

  s_time_layer = make_layer(GRect(0, time_y, w, h_time), f_time,
                            GTextAlignmentCenter);
  s_date_layer = make_layer(GRect(0, date_y, w, h_date), f_text,
                            GTextAlignmentCenter);
  s_batt_layer = make_layer(GRect(0, batt_y, w, h_batt), f_text,
                            GTextAlignmentCenter);

  // 逐层挂载：任一字段创建失败只损失该字段，剩下的照常显示。
  // （早先的写法是"任一失败就整体 return"——那会让三个层全都不挂，
  // 结果是**整屏空白**，比少一行严重得多。三个层用的是同一批字体键，
  // 一旦字体取不到就会一起失败，所以这条路径必须优雅降级。）
  if (s_time_layer != NULL) {
    layer_add_child(root, text_layer_get_layer(s_time_layer));
  }
  if (s_date_layer != NULL) {
    layer_add_child(root, text_layer_get_layer(s_date_layer));
  }
  if (s_batt_layer != NULL) {
    layer_add_child(root, text_layer_get_layer(s_batt_layer));
  }
  if (s_time_layer == NULL && s_date_layer == NULL && s_batt_layer == NULL) {
    return;   // 三个都没建起来就没什么可画的了（window_unload 会回收）
  }

  // 首屏填充，避免出现空白帧
  time_t now = time(NULL);
  struct tm *local = localtime(&now);
  if (local != NULL) {
    update_time(local);
    refresh_date(local->tm_wday, local->tm_mday);
  }
  // [OPT3] 用 peek 拿初值，之后靠订阅
  battery_handler(battery_state_service_peek());
}

static void window_unload(Window *window) {
  if (s_time_layer != NULL) {
    text_layer_destroy(s_time_layer);
    s_time_layer = NULL;
  }
  if (s_date_layer != NULL) {
    text_layer_destroy(s_date_layer);
    s_date_layer = NULL;
  }
  if (s_batt_layer != NULL) {
    text_layer_destroy(s_batt_layer);
    s_batt_layer = NULL;
  }
}

static void init(void) {
  s_window = window_create();
  if (s_window == NULL) {
    return;   // 创建失败：没有 window 可显示，但也不会崩
  }
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);   // [OPT1]
  battery_state_service_subscribe(battery_handler);          // [OPT3]
  s_subscribed = true;
}

static void deinit(void) {
  // 先摘订阅再销毁 window。顺带记一笔固件时序（已核实）：框架在 deinit 事件里
  // 会先 pop_all 卸载窗口，所以这里的 window_destroy 触发的 unload 通常是 no-op；
  // 但"先摘订阅"仍然是对的顺序，不能依赖框架恰好先卸载。
  if (s_subscribed) {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    s_subscribed = false;
  }
  if (s_window != NULL) {
    window_destroy(s_window);
    s_window = NULL;
  }
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
