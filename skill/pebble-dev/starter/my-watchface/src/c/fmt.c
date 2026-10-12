// fmt.c —— 纯 C 实现，不依赖 Pebble SDK。
//
// 先纠正一个常见误解（本项目实测结论）：
//   "手写格式化能省体积" 在 Pebble SDK 上**不成立**。libpebble.a 里的
//   strftime/snprintf 是 12 字节的系统调用桩，未被引用就被 --gc-sections 丢掉；
//   而这里的实现自身是净增（实测 emery 上整包 +442 字节 .text，见 measure.sh ab）。
//   保留它的理由是**可验证性**：这些函数能在 PC 上跑单元测试与差分对拍。
//
// 出货路径：
//   * 时间/日期/电量串 -> 本文件（fmt_time_ship / fmt_date / fmt_battery）
//   * fmt_time / fmt_hour / fmt_two_digits 在出货构建里是 static，未引用即被丢弃
#include "fmt.h"

#ifdef FMT_TEST_HOOKS
// 测试构建：导出符号供 tests/test_fmt.c 做差分对拍
#define FMT_LINKAGE
#else
// 出货构建：static + unused。只有 fmt_time_ship/fmt_date/fmt_battery 被 main.c 调用，
// 其余（fmt_time/fmt_hour）没有调用点，交给链接器 --gc-sections 丢弃即可。
#define FMT_LINKAGE static __attribute__((unused))
#endif


// 星期名用**扁平字符串**而不是 char*[7]：
// char* 数组是指针数组，指针本身必须待在 .data（RAM）；扁平字符串整块进 flash。
// 实测收益（emery）：.data 从 28 字节降到 0，.text 也小 18 字节。
//
// ⚠️ 这里刻意**不**暴露 "const char *fmt_day_name(int)" 这种返回子串指针的 API：
// 扁平常量内部没有逐个 NUL 结尾，调用方拿着指针会一路读到后面的星期名
// （我们的测试立刻抓到了 "WedThuFriSat"）。所以只在 fmt_date 内部按 3*wday 取用。
static const char DAY_NAMES_FLAT[] = "SunMonTueWedThuFriSat";

FMT_LINKAGE size_t fmt_two_digits(char *out, unsigned value) {
  if (value > 99u) {
    value = 99u;
  }
  out[0] = (char)('0' + (value / 10u));
  out[1] = (char)('0' + (value % 10u));
  return 2;
}

// 出货时钟路径。手表上调用方传 clock_is_24h_style()；PC 测试直接传布尔值，
// 因此测试覆盖的就是真正出货的这份代码（而不是"手抄副本"）。
//
// 12 小时制按 Pebble 惯例去掉前导 0（"9:30"），24 小时制保留两位（"09:30"）。
size_t fmt_time_ship(char *out, size_t out_len, int hour24, int minute, bool is_24h) {
  if (out_len < 6) {           // 最坏情况 "23:59\0"；太小就一个字节都不写
    return 0;
  }
  if (hour24 < 0) {
    hour24 = 0;
  }
  if (hour24 > 23) {
    hour24 = 23;
  }
  if (minute < 0) {
    minute = 0;
  }
  if (minute > 59) {
    minute = 59;
  }

  size_t len;
  if (is_24h) {
    len = fmt_two_digits(out, (unsigned)hour24);
  } else {
    unsigned h = (unsigned)hour24 % 12u;   // 0 点与 12 点都显示 12
    if (h == 0u) {
      h = 12u;
    }
    if (h < 10u) {
      out[0] = (char)('0' + h);
      len = 1;
    } else {
      len = fmt_two_digits(out, h);
    }
  }
  out[len++] = ':';
  fmt_two_digits(out + len, (unsigned)minute);
  len += 2;
  out[len] = '\0';
  return len;
}

FMT_LINKAGE size_t fmt_hour(char *out, size_t out_len, int hour24, bool is_24h) {
  if (out_len < 2) {
    return 0;
  }
  // 夹到合法范围，避免脏数据（如未初始化的 tm）写出乱码
  if (hour24 < 0) {
    hour24 = 0;
  }
  if (hour24 > 23) {
    hour24 = 23;
  }

  unsigned h = (unsigned)hour24;
  if (!is_24h) {
    h %= 12u;                 // 0 -> 12am 显示为 12，12pm 显示为 12
    if (h == 0u) {
      h = 12u;
    }
  }

  // 24 小时制恒定两位（"09:05"）；12 小时制去掉前导 0（"9:05"）。
  // 数字拆分只有一处实现，两条路径不会算出不同结果。
  if (h < 10u && !is_24h) {
    out[0] = (char)('0' + h);
    return 1;
  }
  return fmt_two_digits(out, h);
}

FMT_LINKAGE size_t fmt_time(char *out, size_t out_len, int hour24, int minute, bool is_24h) {
  if (out_len < 6) {           // 最坏情况 "12:59\0"；太小就一个字节都不写
    return 0;
  }
  if (minute < 0) {
    minute = 0;
  }
  if (minute > 59) {
    minute = 59;
  }

  size_t len = fmt_hour(out, out_len - 3, hour24, is_24h);
  out[len++] = ':';
  len += fmt_two_digits(out + len, (unsigned)minute);
  out[len] = '\0';
  return len;
}

size_t fmt_date(char *out, size_t out_len, int wday, int mday) {
  if (out_len < 7) {           // 最坏情况 "Wed 31\0"；太小就一个字节都不写
    return 0;
  }
  if (mday < 1) {
    mday = 1;
  }
  if (mday > 31) {
    mday = 31;
  }

  if (wday < 0 || wday > 6) {   // 防越界：非法 tm_wday 归到周日
    wday = 0;
  }
  const char *name = &DAY_NAMES_FLAT[3 * wday];
  size_t len = 0;
  for (int i = 0; i < 3; i++) {
    out[len++] = name[i];
  }
  out[len++] = ' ';
  // 复用同一处两位数实现：出货代码里只有一份数字拆分逻辑
  if (mday < 10) {
    out[len++] = (char)('0' + mday);
  } else {
    fmt_two_digits(out + len, (unsigned)mday);
    len += 2;
  }
  out[len] = '\0';
  return len;
}

size_t fmt_battery(char *out, size_t out_len, int percent, bool charging) {
  if (out_len < 6) {           // 最坏情况 "100%+\0"；太小就一个字节都不写
    return 0;
  }
  if (percent < 0) {
    percent = 0;
  }
  if (percent > 100) {
    percent = 100;
  }

  size_t len = 0;
  if (percent >= 100) {
    out[len++] = '1';
    out[len++] = '0';
    out[len++] = '0';
  } else if (percent >= 10) {
    fmt_two_digits(out + len, (unsigned)percent);
    len += 2;
  } else {
    out[len++] = (char)('0' + percent);
  }
  out[len++] = '%';
  if (charging) {
    out[len++] = '+';
  }
  out[len] = '\0';
  return len;
}

// 三段式纵向布局。算法与"逐层夹取 + 从下往上推开"的直观做法一致，
// 但由于它是纯函数，tests/test_layout.c 可以对所有屏幕尺寸穷举验证它。
int fmt_layout_stack(int16_t h,
                     int16_t h_time, int16_t h_date, int16_t h_batt,
                     FmtLayout *out) {
  if (out == NULL) {
    return 1;
  }
  // 约束 2：高度必须为正，否则退回到一个可见的最小值
  if (h_time <= 0) {
    h_time = FMT_FALLBACK_LINE_HEIGHT;
  }
  if (h_date <= 0) {
    h_date = FMT_FALLBACK_LINE_HEIGHT;
  }
  if (h_batt <= 0) {
    h_batt = FMT_FALLBACK_LINE_HEIGHT;
  }
  if (h <= 0) {
    h = 1;
  }

  // 约束 1 的前提：层高不能超过屏幕高，否则无论怎么摆都会越界。
  // 极端情况下（屏幕比一行字还矮）只能让层高度=屏幕高度，让系统去裁字。
  if (h_time > h) {
    h_time = h;
  }
  if (h_date > h) {
    h_date = h;
  }
  if (h_batt > h) {
    h_batt = h;
  }

  const int16_t gap = 2;
  const int32_t need = (int32_t)h_time + h_date + h_batt + 2 * gap;
  int16_t time_y = 0;
  int16_t date_y = 0;
  int16_t batt_y = 0;

  if (need <= h) {
    // 装得下：紧凑排布 + **整叠垂直居中**。
    //
    // 两个坑都踩过，写在这里免得再犯：
    //   1) "先算比例位置、再从下往上推"在矮屏上会顾此失彼：把日期往上推之后它又撞到
    //      时间层，最后仍然重叠（被 tests/test_layout.c 的 h=74 用例抓出来）。
    //   2) 反过来"整叠贴到屏幕底部"虽然不重叠，但视觉上三行全挤在底部、上方一大片
    //      空白（被模拟器截图抓出来）。所以必须按剩余空间居中分配。
    time_y = (int16_t)((h - (int16_t)need) / 2);
    date_y = (int16_t)(time_y + h_time + gap);
    batt_y = (int16_t)(date_y + h_date + gap);
    if (time_y < 0) {
      time_y = 0;
      date_y = (int16_t)(h_time + gap);
      batt_y = (int16_t)(date_y + h_date + gap);
    }
  } else {
    // 装不下：按比例位置摆放，并逐层夹进屏幕。
    // 此时重叠不可避免，约束优先级是"在屏幕内 > 不重叠"。
    time_y = (int16_t)(h / 8);
    date_y = (int16_t)((h * 5) / 12);
    batt_y = (int16_t)(h - h_batt - 4);

    if (time_y > h - h_time) {
      time_y = (int16_t)(h - h_time);
    }
    if (time_y < 0) {
      time_y = 0;
    }
    if (date_y > h - h_date) {
      date_y = (int16_t)(h - h_date);
    }
    if (date_y < 0) {
      date_y = 0;
    }
    if (batt_y > h - h_batt) {
      batt_y = (int16_t)(h - h_batt);
    }
    if (batt_y < 0) {
      batt_y = 0;
    }
  }

  out->time_y = time_y;
  out->time_h = h_time;
  out->date_y = date_y;
  out->date_h = h_date;
  out->batt_y = batt_y;
  out->batt_h = h_batt;

  const bool no_overlap = (time_y + h_time <= date_y) && (date_y + h_date <= batt_y);
  return no_overlap ? 0 : 1;
}

// 纯 C 的"内容是否变化"比较，替代 strcmp 调用点，
// 让上层不必为一个字符串比较再拉进 string.h 的重型实现。
bool fmt_changed(const char *current, const char *next) {
  if (current == next) {
    return false;
  }
  while (*current != '\0' && *current == *next) {
    current++;
    next++;
  }
  return *current != *next;
}

// 把 src 拷进 dst（含结尾 0），最多 dst_len 字节。
// 返回值表示"是否写出了内容"：dst_len==0 时 false，否则恒为 true。
// 它不表示内容发生了变化——要判断变化请先用 fmt_changed。
bool fmt_copy(char *dst, size_t dst_len, const char *src) {
  if (dst_len == 0) {
    return false;
  }
  size_t i = 0;
  for (; i + 1 < dst_len && src[i] != '\0'; i++) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
  return true;
}
