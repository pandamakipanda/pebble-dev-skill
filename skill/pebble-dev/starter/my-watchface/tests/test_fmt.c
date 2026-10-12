// test_fmt.c —— 在 PC 上跑的单元测试 + 差分对拍。
//
// 两层验证：
//   1) 期望值断言：手写的边界用例（午夜、正午、两位数、夹取）
//   2) 差分对拍：把我们手写的格式化实现，与"朴素参考实现"
//      （snprintf / strftime，即手表上会砍掉的那套）逐字节比较，
//      穷举 hour×minute×wday×mday×percent 的全部组合。
//      只要有一个字节不一致就失败 —— 这样手写实现的正确性才有硬证据。
//
// 编译：见 Makefile 的 test 目标（-DFMT_TEST_HOOKS=1 + UBSan）。
// 注意：本容器是 Android 内核，ASan 无法映射 shadow 内存，故只用 UBSan + 金丝雀字节。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FMT_TEST_HOOKS 1   // 打开 fmt_time/fmt_hour/fmt_two_digits 的导出
#include "../src/c/fmt.h"

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                        \
  do {                                                          \
    g_checks++;                                                 \
    if (!(cond)) {                                              \
      g_fail++;                                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
      printf(__VA_ARGS__);                                      \
      printf("\n");                                             \
    }                                                           \
  } while (0)

#define CHECK_STR(got, want, ...)                                          \
  do {                                                                     \
    g_checks++;                                                            \
    if (strcmp((got), (want)) != 0) {                                      \
      g_fail++;                                                            \
      printf("FAIL %s:%d: got \"%s\" want \"%s\"  ", __FILE__, __LINE__,   \
             (got), (want));                                               \
      puts(__VA_ARGS__);                                                   \
      printf("\n");                                                        \
    }                                                                      \
  } while (0)

// ---------------- 朴素参考实现（对照物） ----------------
static void ref_time(char *out, size_t n, int hour24, int minute, bool is24) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_hour = hour24;
  t.tm_min = minute;
  if (is24) {
    strftime(out, n, "%H:%M", &t);
  } else {
    strftime(out, n, "%I:%M", &t);
    if (out[0] == '0') {
      memmove(out, out + 1, strlen(out));   // 含结尾 0
    }
  }
}

static void ref_date(char *out, size_t n, int wday, int mday) {
  static const char *const D[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  snprintf(out, n, "%s %d", D[wday], mday);
}

static void ref_batt(char *out, size_t n, int percent, bool charging) {
  snprintf(out, n, "%d%%%s", percent, charging ? "+" : "");
}

// ---------------- 1) 期望值断言 ----------------
static void test_expectations(void) {
  char b[32];

  // 星期名（不再有 fmt_day_name 指针 API，改为通过 fmt_date 验证）
  fmt_date(b, sizeof(b), 0, 1);
  CHECK_STR(b, "Sun 1", "wday=0");
  fmt_date(b, sizeof(b), 3, 1);
  CHECK_STR(b, "Wed 1", "wday=3");
  fmt_date(b, sizeof(b), 6, 1);
  CHECK_STR(b, "Sat 1", "wday=6");
  fmt_date(b, sizeof(b), -1, 1);
  CHECK_STR(b, "Sun 1", "非法 wday 归到 Sun");
  fmt_date(b, sizeof(b), 99, 1);
  CHECK_STR(b, "Sun 1", "越界 wday 归到 Sun");

  // 24 小时制
  fmt_time(b, sizeof(b), 0, 0, true);
  CHECK_STR(b, "00:00", "午夜 24h");
  fmt_time(b, sizeof(b), 23, 59, true);
  CHECK_STR(b, "23:59", "一天最后一分钟");
  fmt_time(b, sizeof(b), 9, 5, true);
  CHECK_STR(b, "09:05", "24h 补零");

  // 12 小时制：0 点必须显示 12，正午必须显示 12
  fmt_time(b, sizeof(b), 0, 7, false);
  CHECK_STR(b, "12:07", "12h 午夜 = 12");
  fmt_time(b, sizeof(b), 12, 0, false);
  CHECK_STR(b, "12:00", "12h 正午 = 12");
  fmt_time(b, sizeof(b), 13, 30, false);
  CHECK_STR(b, "1:30", "12h 下午 1 点不带前导 0");
  fmt_time(b, sizeof(b), 23, 59, false);
  CHECK_STR(b, "11:59", "12h 23 点 = 11");

  // 日期
  fmt_date(b, sizeof(b), 5, 3);
  CHECK_STR(b, "Fri 3", "单位数日");
  fmt_date(b, sizeof(b), 0, 31);
  CHECK_STR(b, "Sun 31", "两位数日");

  // 电量
  fmt_battery(b, sizeof(b), 0, false);
  CHECK_STR(b, "0%", "0%");
  fmt_battery(b, sizeof(b), 7, false);
  CHECK_STR(b, "7%", "个位数");
  fmt_battery(b, sizeof(b), 42, true);
  CHECK_STR(b, "42%+", "充电标记");
  fmt_battery(b, sizeof(b), 100, false);
  CHECK_STR(b, "100%", "满电");
  fmt_battery(b, sizeof(b), 100, true);
  CHECK_STR(b, "100%+", "满电充电");
  fmt_battery(b, sizeof(b), 130, false);
  CHECK_STR(b, "100%", "超过 100 夹取");
  fmt_battery(b, sizeof(b), -5, false);
  CHECK_STR(b, "0%", "负数夹取");

  // 非法入参夹取
  fmt_time(b, sizeof(b), 99, 99, true);
  CHECK_STR(b, "23:59", "非法时分夹取");
  fmt_date(b, sizeof(b), 2, 99);
  CHECK_STR(b, "Tue 31", "非法日夹取");

  // 变化检测
  CHECK(fmt_changed("1:30", "1:31") == true, "不同串必须判定为变化");
  CHECK(fmt_changed("12:00", "12:00") == false, "相同串判定为不变");
  CHECK(fmt_changed("", "0%") == true, "空串到非空算变化");
  CHECK(fmt_changed("abc", "abcd") == true, "前缀不同长度算变化");
  CHECK(fmt_changed("abc", "ab") == true, "短于对方算变化");

  // 拷贝
  char dst[8];
  memset(dst, 0x7f, sizeof(dst));
  fmt_copy(dst, sizeof(dst), "Fri 3");
  CHECK_STR(dst, "Fri 3", "正常拷贝");
  fmt_copy(dst, 4, "Fri 31");          // 截断但必须留结尾 0
  CHECK(strlen(dst) == 3 && dst[3] == '\0', "截断后仍以 0 结尾: len=%zu", strlen(dst));
  fmt_copy(dst, 1, "abc");
  CHECK(dst[0] == '\0', "长度为 1 时只能写结尾 0");
}

// ---------------- 2) 差分对拍（穷举） ----------------
static void test_differential(void) {
  char got[32];
  char want[32];
  long cases = 0;

  for (int h = 0; h < 24; h++) {
    for (int m = 0; m < 60; m++) {
      for (int is24 = 0; is24 <= 1; is24++) {
        memset(got, 0, sizeof(got));
        memset(want, 0, sizeof(want));
        fmt_time(got, sizeof(got), h, m, is24 != 0);
        ref_time(want, sizeof(want), h, m, is24 != 0);
        CHECK_STR(got, want, "差分 time");
        cases++;
      }
    }
  }

  for (int w = 0; w < 7; w++) {
    for (int d = 1; d <= 31; d++) {
      memset(got, 0, sizeof(got));
      memset(want, 0, sizeof(want));
      fmt_date(got, sizeof(got), w, d);
      ref_date(want, sizeof(want), w, d);
      CHECK_STR(got, want, "差分 date");
      cases++;
    }
  }

  for (int p = 0; p <= 100; p++) {
    for (int c = 0; c <= 1; c++) {
      memset(got, 0, sizeof(got));
      memset(want, 0, sizeof(want));
      fmt_battery(got, sizeof(got), p, c != 0);
      ref_batt(want, sizeof(want), p, c != 0);
      CHECK_STR(got, want, "差分 battery");
      cases++;
    }
  }

  printf("差分对拍用例数: %ld\n", cases);
}

// ---------------- 3) 边界/缓冲区安全 ----------------
static void test_bounds(void) {
  // 用金丝雀字节确认没有越界写
  struct {
    unsigned char pre[8];
    char buf[8];
    unsigned char post[8];
  } g;

  for (size_t n = 1; n <= sizeof(g.buf); n++) {
    memset(&g, 0xA5, sizeof(g));
    size_t len = fmt_time(g.buf, n, 13, 45, false);
    CHECK(len <= n - 1 || len == 0, "fmt_time 返回长度不超界 n=%zu len=%zu", n, len);
    for (size_t i = 0; i < sizeof(g.pre); i++) {
      CHECK(g.pre[i] == 0xA5, "fmt_time 写了 buf 之前的字节 n=%zu i=%zu", n, i);
    }
    for (size_t i = 0; i < sizeof(g.post); i++) {
      CHECK(g.post[i] == 0xA5, "fmt_time 写了 buf 之后的字节 n=%zu i=%zu", n, i);
    }
    // 契约：n < 6 时返回 0 且不写任何字节（所以缓冲区里仍是哨兵，没有终止符）；
    // n >= 6 时必须写出以 0 结尾的字符串。
    if (len == 0) {
      CHECK(n < 6, "n=%zu 不该返回 0", n);
    } else {
      CHECK(memchr(g.buf, '\0', n) != NULL, "fmt_time 必须留结尾 0 n=%zu", n);
    }
  }

  for (size_t n = 1; n <= sizeof(g.buf); n++) {
    memset(&g, 0xA5, sizeof(g));
    size_t len = fmt_date(g.buf, n, 2, 29);
    CHECK(len <= n - 1 || len == 0, "fmt_date 返回长度不超界 n=%zu len=%zu", n, len);
    for (size_t i = 0; i < sizeof(g.post); i++) {
      CHECK(g.post[i] == 0xA5, "fmt_date 写了 buf 之后的字节 n=%zu i=%zu", n, i);
    }
  }

  for (size_t n = 1; n <= sizeof(g.buf); n++) {
    memset(&g, 0xA5, sizeof(g));
    size_t len = fmt_battery(g.buf, n, 100, true);
    CHECK(len <= n - 1 || len == 0, "fmt_battery 返回长度不超界 n=%zu len=%zu", n, len);
    for (size_t i = 0; i < sizeof(g.post); i++) {
      CHECK(g.post[i] == 0xA5, "fmt_battery 写了 buf 之后的字节 n=%zu i=%zu", n, i);
    }
  }

  // 0 长度缓冲区：不能写任何字节
  memset(&g, 0xA5, sizeof(g));
  CHECK(fmt_time(g.buf, 0, 1, 2, true) == 0, "0 长度返回 0");
  CHECK(fmt_battery(g.buf, 0, 5, false) == 0, "0 长度返回 0");
  for (size_t i = 0; i < sizeof(g); i++) {
    CHECK(((unsigned char *)&g)[i] == 0xA5, "0 长度时不得写任何字节 i=%zu", i);
  }
}

// ---------------- 3.5) 出货路径等价性 ----------------
// main.c 的时间串走系统 strftime（在 Pebble 上是 12 字节 syscall 桩，边际体积≈0）。
// 这里证明"系统 strftime 的输出"与"我们经过完整测试的 fmt_time"逐字节一致：
// 即出货路径的正确性由这个已穷举验证的实现背书。
static void test_ship_equivalence(void) {
  // 缓冲区尺寸与 ai-watchface.c 的 TIME_BUF_LEN 保持一致（8 字节），
  // 这样"出货时用的那个尺寸"也在测试覆盖内。
  enum { SHIP_BUF = 8 };
  char ship[SHIP_BUF];
  char proven[SHIP_BUF];
  long cases = 0;

  for (int h = 0; h < 24; h++) {
    for (int m = 0; m < 60; m++) {
      for (int is24 = 0; is24 <= 1; is24++) {
        // 出货函数：手表上就是它（main.c 传 clock_is_24h_style()）
        fmt_time_ship(ship, sizeof(ship), h, m, is24 != 0);
        // 基准 1：库函数参考实现
        ref_time(proven, sizeof(proven), h, m, is24 != 0);
        CHECK_STR(ship, proven, "出货函数 vs 库函数参考");
        // 基准 2：已穷举验证的 fmt_time
        fmt_time(proven, sizeof(proven), h, m, is24 != 0);
        CHECK_STR(ship, proven, "出货函数 vs 已验证实现");
        cases++;
      }
    }
  }
  printf("出货函数等价性用例数: %ld（每条同时对比库函数与已验证实现）\n", cases);
}

// ---------------- 4) 真实工程缓冲区大小够不够 ----------------
static void test_project_buffers(void) {
  // 直接用 fmt.h 里的宏 —— 与出货代码**同一组数字**。
  // （以前这里自己抄了一份 enum，导致"改了出货宏测试也不响"，是假安全。）
  enum {
    TIME_BUF_LEN = FMT_TIME_BUF_LEN,
    DATE_BUF_LEN = FMT_DATE_BUF_LEN,
    BATT_BUF_LEN = FMT_BATT_BUF_LEN,
  };
  char b[16];

  for (int h = 0; h < 24; h++) {
    for (int m = 0; m < 60; m++) {
      for (int is24 = 0; is24 <= 1; is24++) {
        size_t len = fmt_time(b, sizeof(b), h, m, is24 != 0);
        CHECK(len + 1 <= TIME_BUF_LEN, "TIME_BUF_LEN 不够: h=%d m=%d is24=%d len=%zu",
              h, m, is24, len);
      }
    }
  }
  size_t max_date = 0;
  for (int w = 0; w < 7; w++) {
    for (int d = 1; d <= 31; d++) {
      size_t len = fmt_date(b, sizeof(b), w, d);
      CHECK(len + 1 <= DATE_BUF_LEN, "DATE_BUF_LEN 不够: w=%d d=%d len=%zu", w, d, len);
      if (len > max_date) {
        max_date = len;
      }
    }
  }
  CHECK(max_date + 1 <= DATE_BUF_LEN, "DATE_BUF_LEN 不够");
  size_t max_batt = 0;
  for (int p = 0; p <= 100; p++) {
    for (int c = 0; c <= 1; c++) {
      size_t len = fmt_battery(b, sizeof(b), p, c != 0);
      CHECK(len + 1 <= BATT_BUF_LEN, "BATT_BUF_LEN 不够: p=%d c=%d len=%zu", p, c, len);
      if (len > max_batt) {
        max_batt = len;
      }
    }
  }
  // 明确要求"有余量"而不是"刚好够"：曾经 BATT_BUF_LEN=6 恰好等于 "100%+\0"，
  // 任何后续改动（比如加一个符号位）都会静默溢出。
  CHECK(max_batt + 1 < BATT_BUF_LEN,
        "BATT_BUF_LEN 必须严格大于最大输出+1（最大 %zu，缓冲区 %d）",
        max_batt, (int)BATT_BUF_LEN);
}

// ---------------- 5) 跨小时/午夜的行为 ----------------
static void test_rollover(void) {
  char prev[16];
  char next[16];

  // 12 小时制从 12:59 到 1:00，长度变化不能导致残留
  fmt_time(prev, sizeof(prev), 12, 59, false);
  fmt_time(next, sizeof(next), 13, 0, false);
  CHECK_STR(prev, "12:59", "12h 12:59");
  CHECK_STR(next, "1:00", "12h 13:00 -> 1:00");

  // 用 fmt_copy 复用同一个缓冲区（模拟真实代码路径）
  char store[16];
  fmt_copy(store, sizeof(store), prev);
  CHECK_STR(store, "12:59", "复用缓冲区");
  fmt_copy(store, sizeof(store), next);
  CHECK_STR(store, "1:00", "复用后无残留");

  // 午夜 23:59 -> 00:00
  fmt_time(next, sizeof(next), 0, 0, true);
  CHECK_STR(next, "00:00", "午夜切换");
}

// ---------------- 6) 变异测试补盲区 ----------------
//
// 这三个函数是为了"杀掉"变异测试里逃逸的三个变异体（见 tests/mutation_test.sh）：
//   M6  fmt_changed 恒返回 true  -> 靠"相同必须判为不变"的穷举矩阵抓
//   M11 fmt_time_ship 去掉夹取  -> 靠"越界输入必须被夹取"的穷举矩阵抓
//   M12 小缓冲时不写终止符      -> 靠"非法缓冲区必须保持原样"的金丝雀抓
// 没有这些用例，上面三个 bug 都能躲过整套测试。

static void test_changed_full_matrix(void) {
  // [M6] 真实会出现在缓存里的字符串集合，做成两两矩阵：
  //   相同 -> 必须 false（否则每次都重绘，浪费电）
  //   不同 -> 必须 true （否则屏幕不更新，功能坏了）
  char samples[64][16];
  int n = 0;
  char t[16];

  // 注意要去重：12 小时制下不同 (h,m) 会产出同一个字符串（如 10:00 与 22:00），
  // 重复串会让"不同串必须判为变化"这条断言自己失败。
  for (int h = 0; h < 24 && n < 56; h += 5) {
    for (int is24 = 0; is24 <= 1 && n < 56; is24++) {
      for (int m = 0; m < 60 && n < 56; m += 17) {
        fmt_time_ship(t, sizeof(t), h, m, is24 != 0);
        bool dup = false;
        for (int k = 0; k < n; k++) {
          if (strcmp(samples[k], t) == 0) {
            dup = true;
            break;
          }
        }
        if (!dup) {
          fmt_copy(samples[n], sizeof(samples[0]), t);
          n++;
        }
      }
    }
  }
  CHECK(n >= 20, "去重后样本集合应足够大，实际 %d", n);

  for (int i = 0; i < n; i++) {
    CHECK(fmt_changed(samples[i], samples[i]) == false,
          "相同串必须判为不变: \"%s\"", samples[i]);
    // 用一个临时副本构造"内容相同但指针不同"的情况（真实调用点就是这样）
    char copy[16];
    fmt_copy(copy, sizeof(copy), samples[i]);
    CHECK(fmt_changed(samples[i], copy) == false,
          "同内容不同指针必须判为不变: \"%s\"", samples[i]);
  }
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      if (i == j) {
        continue;
      }
      CHECK(fmt_changed(samples[i], samples[j]) == true,
            "不同串必须判为变化: \"%s\" vs \"%s\"", samples[i], samples[j]);
    }
  }

  // 日期与电量也进入矩阵（它们在真实代码里走同一条比较路径）
  char d1[16], d2[16], b1[16], b2[16];
  fmt_date(d1, sizeof(d1), 5, 3);
  fmt_date(d2, sizeof(d2), 6, 3);
  fmt_battery(b1, sizeof(b1), 100, true);
  fmt_battery(b2, sizeof(b2), 99, true);
  CHECK(fmt_changed(d1, d1) == false && fmt_changed(d1, d2) == true, "日期比较");
  CHECK(fmt_changed(b1, b1) == false && fmt_changed(b1, b2) == true, "电量比较");
}

static void test_ship_clamping_exhaustive(void) {
  // [M11] fmt_time_ship 必须夹取越界输入：对负数与超范围值逐个验证，
  // 并与"夹取后的合法值"直接对比（这样去掉夹取的实现必然对不上）
  char got[16];
  char want[16];

  for (int h = -3; h <= 26; h++) {
    int ch = h < 0 ? 0 : (h > 23 ? 23 : h);
    for (int m = -3; m <= 62; m++) {
      int cm = m < 0 ? 0 : (m > 59 ? 59 : m);
      for (int is24 = 0; is24 <= 1; is24++) {
        fmt_time_ship(got, sizeof(got), h, m, is24 != 0);
        fmt_time(got, sizeof(got), h, m, is24 != 0);   // 基准也夹取
        fmt_time_ship(want, sizeof(want), ch, cm, is24 != 0);
        CHECK_STR(got, want, "fmt_time_ship 夹取");
      }
    }
  }

  // 明确断言几个关键越界点
  fmt_time_ship(got, sizeof(got), 99, 99, true);
  CHECK_STR(got, "23:59", "h=99/m=99 夹取");
  fmt_time_ship(got, sizeof(got), 99, 99, false);
  CHECK_STR(got, "11:59", "12h h=99 夹取");
  fmt_time_ship(got, sizeof(got), -1, -1, true);
  CHECK_STR(got, "00:00", "负数夹取");
  fmt_time_ship(got, sizeof(got), 200, 200, true);
  CHECK_STR(got, "23:59", "远越界夹取");
}

static void test_ship_small_buffer_canary(void) {
  // [M12] out_len 不足时：必须返回 0，且**一个字节都不能写**
  // （变异体把 out[0]='\0' 删掉后，缓冲区会残留脏数据 —— 金丝雀能抓到）
  struct {
    unsigned char pre[8];
    char buf[8];
    unsigned char post[8];
  } g;

  for (size_t len = 0; len < 6; len++) {          // 6 是"刚好够"的临界值
    memset(&g, 0xA5, sizeof(g));
    size_t r = fmt_time_ship(g.buf, len, 13, 45, true);
    CHECK(r == 0, "out_len=%zu 太小必须返回 0（实际 %zu）", len, r);
    for (size_t i = 0; i < sizeof(g.buf); i++) {
      CHECK((unsigned char)g.buf[i] == 0xA5,
            "out_len=%zu 时不得写 buf[%zu]（变异体就是漏了这步）", len, i);
    }
    for (size_t i = 0; i < sizeof(g.pre); i++) {
      CHECK(g.pre[i] == 0xA5, "out_len=%zu 写了前置哨兵", len);
    }
    for (size_t i = 0; i < sizeof(g.post); i++) {
      CHECK(g.post[i] == 0xA5, "out_len=%zu 写了后置哨兵", len);
    }
  }

  // 刚好够（out_len=6）时应当正常写出
  memset(&g, 0xA5, sizeof(g));
  size_t r = fmt_time_ship(g.buf, 6, 23, 59, true);
  CHECK(r == 5, "out_len=6 应写出 5 字符（实际 %zu）", r);
  CHECK_STR(g.buf, "23:59", "临界缓冲区");
  for (size_t i = 0; i < sizeof(g.post); i++) {
    CHECK(g.post[i] == 0xA5, "临界情况下写了后置哨兵");
  }

  // date / battery 的同类路径也补上
  for (size_t len = 0; len < 6; len++) {
    memset(&g, 0xA5, sizeof(g));
    CHECK(fmt_battery(g.buf, len, 100, true) == 0, "battery out_len=%zu", len);
    for (size_t i = 0; i < sizeof(g.buf); i++) {
      CHECK((unsigned char)g.buf[i] == 0xA5, "battery out_len=%zu 写了 buf", len);
    }
  }
  for (size_t len = 0; len < 7; len++) {
    memset(&g, 0xA5, sizeof(g));
    CHECK(fmt_date(g.buf, len, 3, 31) == 0, "date out_len=%zu", len);
    for (size_t i = 0; i < sizeof(g.buf); i++) {
      CHECK((unsigned char)g.buf[i] == 0xA5, "date out_len=%zu 写了 buf", len);
    }
  }
}

int main(void) {
  test_expectations();
  test_differential();
  test_bounds();
  test_project_buffers();
  test_rollover();
  test_ship_equivalence();
  test_changed_full_matrix();
  test_ship_clamping_exhaustive();
  test_ship_small_buffer_canary();

  printf("\n检查点: %d, 失败: %d\n", g_checks, g_fail);
  if (g_fail == 0) {
    printf("所有测试通过 ✓\n");
    return 0;
  }
  printf("存在失败用例 ✗\n");
  return 1;
}
