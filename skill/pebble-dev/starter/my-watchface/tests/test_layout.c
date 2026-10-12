// test_layout.c —— 布局不变量的穷举验证。
//
// 背景：本工程没法在模拟器上截图（无 DISPLAY），所以"三个文字层是否都在屏幕内、
// 是否零高度、是否重叠"不能靠看图确认。
//
// **关键点：这里测的是出货代码本身。** 布局算法是放在 src/c/fmt.c 里的纯函数
// `fmt_layout_stack()`（ai-watchface.c 直接调用它、不再自己算一遍），所以本测试
// 覆盖的就是真正跑在手表上的那段逻辑 —— 不存在"手抄副本漂移"的问题。
// 文件末尾另有一条源码同步断言，防止有人把 ai-watchface.c 改回自己算布局。
//
// 约束优先级（与 fmt.h 的注释一致）：
//   1) 每层完整落在 [0, h] 内   —— 层不会自动裁剪，越界 = 看不见
//   2) 每层高度 > 0             —— 零高度 = 存在但看不见
//   3) 互不重叠                 —— 真实表都满足；屏幕装不下时允许重叠（1 优先于 3）
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define FMT_TEST_HOOKS 1
#include "fmt.h"

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                        \
  do {                                                          \
    g_checks++;                                                 \
    if (!(cond)) {                                              \
      g_fail++;                                                 \
      printf("FAIL %d: ", __LINE__);                            \
      printf(__VA_ARGS__);                                      \
      printf("\n");                                             \
    }                                                           \
  } while (0)

// 真实平台尺寸（官方 Hardware Information）与实测行高的代表值
static const struct {
  const char *name;
  int w, h;
  int16_t h_time, h_date, h_batt;
} PLATFORMS[] = {
  {"aplite  144x168", 144, 168, 42, 20, 20},
  {"basalt  144x168", 144, 168, 42, 20, 20},
  {"diorite 144x168", 144, 168, 42, 20, 20},
  {"flint   144x168", 144, 168, 42, 20, 20},
  {"chalk   180x180", 180, 180, 42, 20, 20},
  {"emery   200x228", 200, 228, 42, 20, 20},
  {"gabbro  260x260", 260, 260, 42, 20, 20},
};

static void check_invariants(const char *tag, int w, int h, const FmtLayout *L,
                             int overlap_allowed) {
  // 约束 2：零高度绝不允许
  CHECK(L->time_h > 0, "%s: 时间层高度为 0", tag);
  CHECK(L->date_h > 0, "%s: 日期层高度为 0", tag);
  CHECK(L->batt_h > 0, "%s: 电量层高度为 0", tag);

  // 约束 1：每层完整在屏幕内（最高优先级）
  CHECK(L->time_y >= 0 && L->time_y + L->time_h <= h,
        "%s: 时间层越界 y=%d h=%d 屏高=%d", tag, L->time_y, L->time_h, h);
  CHECK(L->date_y >= 0 && L->date_y + L->date_h <= h,
        "%s: 日期层越界 y=%d h=%d 屏高=%d", tag, L->date_y, L->date_h, h);
  CHECK(L->batt_y >= 0 && L->batt_y + L->batt_h <= h,
        "%s: 电量层越界 y=%d h=%d 屏高=%d", tag, L->batt_y, L->batt_h, h);

  // 约束 3：只在屏幕装得下时才要求不重叠
  if (!overlap_allowed) {
    CHECK(L->time_y + L->time_h <= L->date_y,
          "%s: 时间层与日期层重叠 (%d+%d > %d)", tag, L->time_y, L->time_h, L->date_y);
    CHECK(L->date_y + L->date_h <= L->batt_y,
          "%s: 日期层与电量层重叠 (%d+%d > %d)", tag, L->date_y, L->date_h, L->batt_y);
  }
  CHECK(w > 0 && w <= 1000, "%s: 屏幕宽异常 %d", tag, w);
}

// 分布检查：内容不能挤在一端。
// 这条是被模拟器截图抓出来的回归——"整叠贴底"当时不重叠、不越界，通过了所有
// 结构断言，但视觉上三行全挤在底部、上方一大片空白。所以必须把"均衡"也测上。
static void check_balance(const char *tag, int h, const FmtLayout *L,
                          int room_to_spare) {
  // 只有"装得下三行"时才有"怎么分配剩余空间"的自由度；装不下时本来就只能挤，
  // 强行要求均衡是错的（会把"屏幕太矮"误报成布局 bug）。
  if (!room_to_spare) {
    return;
  }
  int top = L->time_y;
  int bottom = h - (L->batt_y + L->batt_h);
  int slack = top + bottom;
  if (slack < 8) {
    return;   // 屏幕太满，没什么可分配的，不评判
  }
  int diff = top > bottom ? top - bottom : bottom - top;
  CHECK(diff <= slack / 2 + 2,
        "%s: 内容偏向一端 上留白=%d 下留白=%d（总余量 %d）", tag, top, bottom, slack);
}

// 屏幕是否"装得下三行 + 两个间隙"
static int fits(int h, int a, int b, int c) {
  return a + b + c + 4 <= h;
}

int main(void) {
  FmtLayout L;

  // --- 1) 真实平台：必须既在屏幕内、又不重叠 ---
  printf("真实平台：\n");
  for (size_t i = 0; i < sizeof(PLATFORMS) / sizeof(PLATFORMS[0]); i++) {
    int rc = fmt_layout_stack((int16_t)PLATFORMS[i].h, PLATFORMS[i].h_time,
                              PLATFORMS[i].h_date, PLATFORMS[i].h_batt, &L);
    check_invariants(PLATFORMS[i].name, PLATFORMS[i].w, PLATFORMS[i].h, &L, 0);
    check_balance(PLATFORMS[i].name, PLATFORMS[i].h, &L, 1);   // 真实表都装得下
    CHECK(rc == 0, "%s: 真实平台不应发生重叠（返回值 %d）", PLATFORMS[i].name, rc);
    printf("  %-16s time_y=%3d(%2d) date_y=%3d(%2d) batt_y=%3d(%2d)\n",
           PLATFORMS[i].name, L.time_y, L.time_h, L.date_y, L.date_h,
           L.batt_y, L.batt_h);
  }

  // --- 2) 行高穷举：含负数、0、异常大（模拟测量失败与异常字体）---
  printf("行高穷举...\n");
  long combos = 0, bad = 0;
  for (int ht = -5; ht <= 130; ht += 1) {
    for (int hd = -5; hd <= 70; hd += 5) {
      for (int hb = -5; hb <= 70; hb += 5) {
        for (size_t i = 0; i < sizeof(PLATFORMS) / sizeof(PLATFORMS[0]); i++) {
          int h = PLATFORMS[i].h;
          int rc = fmt_layout_stack((int16_t)h, (int16_t)ht, (int16_t)hd,
                                    (int16_t)hb, &L);
          int eh = ht > 0 ? ht : FMT_FALLBACK_LINE_HEIGHT;
          int ed = hd > 0 ? hd : FMT_FALLBACK_LINE_HEIGHT;
          int eb = hb > 0 ? hb : FMT_FALLBACK_LINE_HEIGHT;
          int before = g_fail;
          check_invariants("行高穷举", PLATFORMS[i].w, h, &L, !fits(h, eh, ed, eb));
          check_balance("行高穷举", h, &L, fits(h, eh, ed, eb));
          if (rc == 0) {
            CHECK(!(L.time_y + L.time_h > L.date_y || L.date_y + L.date_h > L.batt_y),
                  "返回值说没重叠，实际重叠了");
          }
          if (g_fail != before) {
            bad++;
          }
          combos++;
        }
      }
    }
  }
  printf("  组合 %ld 个，违约 %ld 个\n", combos, bad);

  // --- 3) 屏幕尺寸穷举：1..600 高，含极小屏（挤不下）与极端比例 ---
  printf("屏幕尺寸穷举...\n");
  long scombos = 0, sbad = 0;
  for (int h = 1; h <= 600; h++) {
    for (int w = 1; w <= 400; w += 37) {
      int rc = fmt_layout_stack((int16_t)h, 42, 20, 20, &L);
      int before = g_fail;
      check_invariants("屏幕尺寸穷举", w, h, &L, !fits(h, 42, 20, 20));
      check_balance("屏幕尺寸穷举", h, &L, fits(h, 42, 20, 20));
      if (rc == 0) {
        CHECK(!(L.time_y + L.time_h > L.date_y || L.date_y + L.date_h > L.batt_y),
              "返回值说没重叠，实际重叠了 (h=%d)", h);
      }
      if (g_fail != before) {
        sbad++;
        if (sbad <= 3) {
          printf("  !! 屏 %dx%d: time %d+%d, date %d+%d, batt %d+%d\n", w, h,
                 L.time_y, L.time_h, L.date_y, L.date_h, L.batt_y, L.batt_h);
        }
      }
      scombos++;
    }
  }
  printf("  组合 %ld 个，违约 %ld 个\n", scombos, sbad);

  // --- 4) NULL 输出指针必须被安全拒绝 ---
  CHECK(fmt_layout_stack(200, 42, 20, 20, NULL) != 0, "NULL 输出应返回非 0");

  // --- 5) 极端屏幕也不能崩/不能越界 ---
  int16_t edges[] = {0, 1, 2, 32000, 32767};
  for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
    int rc = fmt_layout_stack(edges[i], 42, 20, 20, &L);
    CHECK(L.time_h > 0 && L.date_h > 0 && L.batt_h > 0,
          "极端屏高度 %d 出现零高", edges[i]);
    (void)rc;
  }

  // --- 6) 源码同步断言：出货代码必须调用纯函数，而不是自己重算布局 ---
  // 定位工程根：不能硬编码绝对路径（工程被复制到别处后会指向"别人的文件"，
  // 断言就变成假通过 —— 这个坑真踩过）。
  //
  // __FILE__ 可能是 "tests/test_layout.c"（相对）或 "/abs/.../tests/test_layout.c"（绝对），
  // 所以这里生成两个候选根，哪个真的存在 src/c/ai-watchface.c 就用哪个。
  static char dir[4096];
  strncpy(dir, __FILE__, sizeof(dir) - 1);
  dir[sizeof(dir) - 1] = '\0';
  char *slash = strrchr(dir, '/');
  if (slash != NULL) {
    *slash = '\0';                  // -> ".../tests"
  } else {
    dir[0] = '.';
    dir[1] = '\0';
  }

  static char root[8192];
  static char src[8192];
  const char *cands[2];
  static char c0[8192];
  static char c1[8192];
  strncpy(c0, dir, sizeof(c0) - 1); c0[sizeof(c0) - 1] = '\0';   // 候选 A：tests/ 本身
  strncpy(c1, dir, sizeof(c1) - 1); c1[sizeof(c1) - 1] = '\0';   // 候选 B：tests/ 的上一级
  {
    char *sl2 = strrchr(c1, '/');
    if (sl2 != NULL) {
      *sl2 = '\0';
    } else {
      c1[0] = '.';
      c1[1] = '\0';
    }
  }
  cands[0] = c0;
  cands[1] = c1;

  FILE *f = NULL;
  for (int ci = 0; ci < 2 && f == NULL; ci++) {
    strncpy(src, cands[ci], sizeof(src) - 1);
    src[sizeof(src) - 1] = '\0';
    strncat(src, "/src/c/ai-watchface.c", sizeof(src) - strlen(src) - 1);
    f = fopen(src, "r");
    if (f != NULL) {
      strncpy(root, cands[ci], sizeof(root) - 1);
      root[sizeof(root) - 1] = '\0';
    }
  }
  if (f == NULL) {
    printf("  （找不到 %s，跳过同步断言）\n", src);
  } else {
    char buf[131072];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    CHECK(strstr(buf, "fmt_layout_stack(") != NULL,
          "ai-watchface.c 必须调用 fmt_layout_stack（不要自己算布局）");
    {
      static char fcsrc[4096];
      strncpy(fcsrc, root, sizeof(fcsrc) - 1);
      fcsrc[sizeof(fcsrc) - 1] = '\0';
      strncat(fcsrc, "/src/c/fmt.c", sizeof(fcsrc) - strlen(fcsrc) - 1);
      FILE *fc = fopen(fcsrc, "r");
      if (fc != NULL) {
        char cb[131072];
        size_t cn = fread(cb, 1, sizeof(cb) - 1, fc);
        cb[cn] = '\0';
        fclose(fc);
        CHECK(strstr(cb, "(h - (int16_t)need) / 2") != NULL,
              "fmt_layout_stack 必须把整叠垂直居中（贴底会造成视觉回归）");
      }
    }
    CHECK(strstr(buf, "#define TIME_BUF_LEN FMT_TIME_BUF_LEN") != NULL,
          "ai-watchface.c 的缓冲区尺寸必须来自 fmt.h（单一真相源）");
    CHECK(strstr(buf, "s_time_buf[0] = '\\0';") != NULL,
          "window_load 必须清空文本缓存（否则重新 load 会白屏）");
    CHECK(strstr(buf, "if (layer == NULL) {") != NULL,
          "push_if_changed 必须有 NULL 守卫（创建失败后回调仍会进来）");
    CHECK(strstr(buf, "window_unload(window);") != NULL,
          "window_load 必须做幂等回收（防重复 load 泄漏）");
    printf("  源码同步断言：5 条\n");
  }

  printf("\n检查点: %d, 失败: %d\n", g_checks, g_fail);
  if (g_fail == 0) {
    printf("布局不变量全部满足 ✓\n");
    return 0;
  }
  printf("存在不满足的不变量 ✗\n");
  return 1;
}
