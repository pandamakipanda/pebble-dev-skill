// bench_fmt.c —— 在 PC 上测格式化函数的 CPU 成本，并估算手表上的唤醒次数。
//
// 说明：手表是 Cortex-M3/M4 @64~100MHz，PC 是 aarch64。绝对数字不能直接搬，
// 但两个用途是可靠的：
//   1) 比较"我们手写 vs 库函数"的相对开销量级
//   2) 证明单次格式化成本相对"一分钟一次"的调度周期可以忽略
// 真正决定耗电的是**唤醒次数**，那部分由 tick 订阅粒度决定，见文末推算。
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../src/c/fmt.h"

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// 对照组：库函数路径
static void ref_time(char *out, size_t n, int h, int m, bool is24) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_hour = h;
  t.tm_min = m;
  if (is24) {
    strftime(out, n, "%H:%M", &t);
  } else {
    strftime(out, n, "%I:%M", &t);
    if (out[0] == '0') memmove(out, out + 1, strlen(out));
  }
}

int main(void) {
  const long N = 20000000;      // 两千次
  char b[16];
  volatile size_t sink = 0;

  // --- 我方实现 ---
  double t0 = now_sec();
  for (long i = 0; i < N; i++) {
    sink += fmt_time(b, sizeof(b), (int)(i % 24), (int)(i % 60), (i & 1) != 0);
  }
  double t_fmt = now_sec() - t0;

  // --- 对照：strftime ---
  t0 = now_sec();
  for (long i = 0; i < N; i++) {
    ref_time(b, sizeof(b), (int)(i % 24), (int)(i % 60), (i & 1) != 0);
  }
  double t_ref = now_sec() - t0;

  // --- 变化检测（每分钟都要跑一次的那句） ---
  t0 = now_sec();
  for (long i = 0; i < N; i++) {
    sink += fmt_changed("12:34", "12:35") ? 1 : 0;
  }
  double t_changed = now_sec() - t0;

  printf("每轮 %ld 次\n", N);
  printf("  fmt_time      : %7.1f ns/次\n", t_fmt / N * 1e9);
  printf("  strftime 对照 : %7.1f ns/次  (%.1fx)\n", t_ref / N * 1e9, t_ref / t_fmt);
  printf("  fmt_changed   : %7.2f ns/次\n", t_changed / N * 1e9);

  // --- 唤醒次数推算 ---
  printf("\n唤醒次数推算（决定耗电的主因）:\n");
  const long sec_per_day = 86400;
  printf("  SECOND_UNIT 订阅: %ld 次/天\n", sec_per_day);
  printf("  MINUTE_UNIT 订阅: %ld 次/天  (%.1f 倍差距)\n", sec_per_day / 60,
         (double)sec_per_day / (sec_per_day / 60));
  printf("  HOUR_UNIT   订阅: %ld 次/天\n", 24L);

  // 用 PC 的数字做个保守上界估计：手表慢 50 倍
  double slow = 50.0;
  printf("\n按『手表比本机慢 %.0f 倍』保守估算:\n", slow);
  printf("  每次 tick 的格式化开销: %.2f 微秒\n", t_fmt / N * 1e6 * slow);
  printf("  一天(1440 次)累计     : %.1f 毫秒\n", t_fmt / N * 1e3 * slow * 1440);
  printf("  若改成每秒 tick       : %.1f 毫秒/天 (多出 %.1f 倍)\n",
         t_fmt / N * 1e3 * slow * 86400, 86400.0 / 1440.0);
  (void)sink;
  return 0;
}
