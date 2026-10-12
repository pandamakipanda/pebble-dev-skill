// fmt.h —— 纯 C 格式化层（不含 pebble.h）。
//
// 为什么要有这一层（按实测理由写，不写想当然的理由）：
//   * 在本 SDK 上实测过：把 snprintf/strftime 换成手写实现**并不会省体积**。
//     libpebble.a 里 strftime/snprintf 是 12 字节的系统调用桩，用完即被 --gc-sections
//     丢弃；而手写实现自身是净增（emery 上 +213 字节 .text）。
//   * 保留这一层的真实理由是**可验证**：它不依赖 SDK，能在 PC 上跑单元测试与差分
//     对拍，把"日期/电量字符串、变化检测、缓冲区边界"这些易错部分变成纯函数。
//   * 时间串最终交给系统的 strftime 输出（边际体积 ≈ 0），但它的正确性由本层的
//     fmt_time 做基准差分证明——两者被证明逐字节等价（见 tests/test_fmt.c）。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>   // FmtLayout 用 int16_t（与 SDK 的 GRect 成员同宽）

// ---------------------------------------------------------------------------
// 缓冲区尺寸：**单一真相源**
//
// 放在这里（而不是放在 ai-watchface.c 里）是刻意的：测试与出货代码必须用
// 同一组数字。之前测试文件自己写了一份 `enum { TIME_BUF_LEN = 8, ... }`，
// 与出货宏毫无关联——有人把出货宏改小，测试依然全绿，这种"假安全"必须消掉。
//
// 尺寸取自各函数的最坏输出（含结尾 0），并留出余量：
//   fmt_time_ship 最坏 "23:59\0" = 6  -> 8 留 2 字节余量
//   fmt_date      最坏 "Wed 31\0" = 7 -> 8 留 1 字节余量
//   fmt_battery   最坏 "100%+\0" = 6  -> 8 留 2 字节余量（曾经是 6，零余量）
// ---------------------------------------------------------------------------
// 文本测量失败时的回退行高（出货代码与测试共用同一个值）
#define FMT_FALLBACK_LINE_HEIGHT 20

#define FMT_TIME_BUF_LEN 8
#define FMT_DATE_BUF_LEN 8
#define FMT_BATT_BUF_LEN 8

// 编译期护栏：任何"把缓冲区改小到不够用"的改动都会在这里直接编译失败
typedef char fmt_assert_time_buf_fits[(FMT_TIME_BUF_LEN >= 6) ? 1 : -1];
typedef char fmt_assert_date_buf_fits[(FMT_DATE_BUF_LEN >= 7) ? 1 : -1];
typedef char fmt_assert_batt_buf_fits[(FMT_BATT_BUF_LEN >= 6) ? 1 : -1];

// 注：星期缩写不再暴露成 `const char *fmt_day_name(int)`。
// 那种"返回指向扁平字符串中间的指针"的 API 有坑：扁平常量里没有逐个 NUL 结尾，
// 拿到指针的调用方会一路读到后面的星期名（测试立刻抓到了 "WedThuFriSat"）。
// 现在星期名在 fmt_date 内部按 3*wday 取子串，不需要对外暴露。

// 组装 "Www D"（如 "Fri 3"），返回写入长度（不含结尾 0）。
// 契约：out_len < 7 时返回 0 且**不写任何字节**（三个函数统一）。
size_t fmt_date(char *out, size_t out_len, int wday, int mday);

// 组装电量串：percent 会夹到 0..100；charging 时追加 '+'。
// 契约：out_len < 6 时返回 0 且**不写任何字节**（三个函数统一）。
size_t fmt_battery(char *out, size_t out_len, int percent, bool charging);

// 纯 C 的内容比较：相同返回 false。替代 strcmp 调用点（少一次库调用）。
bool fmt_changed(const char *current, const char *next);

// 把 src 拷进 dst（含结尾 0），最多 dst_len 字节。
// 返回值表示"是否写出了内容"：dst_len==0 时 false，否则恒为 true。
// 它**不表示内容发生了变化**——要判断变化请先用 fmt_changed。
bool fmt_copy(char *dst, size_t dst_len, const char *src);

// ---------------------------------------------------------------------------
// 出货时钟路径：组装 "HH:MM" / "h:MM"。
//
// "是否 24 小时制"作为显式参数传入（而不是在函数里调 clock_is_24h_style()），
// 这样同一个函数在手表上由 C 代码调用、在 PC 上可被单元测试直接调用，
// 测试覆盖的就是真正出货的那份代码。
// ---------------------------------------------------------------------------
// 契约：out_len < 6 时返回 0 且**不写任何字节**。
// 入参越界会被夹取（hour 到 0..23，minute 到 0..59）。
size_t fmt_time_ship(char *out, size_t out_len, int hour24, int minute, bool is_24h);

// ---------------------------------------------------------------------------
// 三段式纵向布局：把"时间/日期/电量"三行摆进 h 像素高的屏幕。
//
// 这是纯计算，不碰任何 SDK 类型——因此可以在 PC 上穷举验证（tests/test_layout.c）。
// 约束的优先级（写死在这里，测试按同一优先级断言）：
//   1) 每层必须完整落在 [0, h] 内（层不会自动裁剪，越界 = 看不见）
//   2) 每层高度必须 > 0（零高度 = 存在但看不见）
//   3) 互不重叠 —— 真实表都满足；屏幕矮到装不下三行时允许重叠
// 返回值：0 表示三层互不重叠；1 表示屏幕太矮、已接受重叠。
// ---------------------------------------------------------------------------
typedef struct {
  int16_t time_y, time_h;
  int16_t date_y, date_h;
  int16_t batt_y, batt_h;
} FmtLayout;

int fmt_layout_stack(int16_t h,
                     int16_t h_time, int16_t h_date, int16_t h_batt,
                     FmtLayout *out);

#ifdef FMT_TEST_HOOKS
// 仅测试构建可见：出货构建里下面这些是 static，不占最终固件。
size_t fmt_two_digits(char *out, unsigned value);   // out 至少 2 字节，不写结尾 0
size_t fmt_hour(char *out, size_t out_len, int hour24, bool is_24h);
size_t fmt_time(char *out, size_t out_len, int hour24, int minute, bool is_24h);
#endif
