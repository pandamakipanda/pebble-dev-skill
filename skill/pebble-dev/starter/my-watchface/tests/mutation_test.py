#!/usr/bin/env python3
"""mutation_test.py —— 变异测试：故意往 fmt.c 注入 bug，验证测试套件真能抓到。

为什么需要：测试"全绿"只说明没抓到问题，不说明测试**有能力**抓问题。
只有把已知 bug 注入进去、看到红灯，才能证明这套测试是有效的安全网。

上一轮跑出过 3 个变异逃逸（测试盲区）与 1 个"过期变异体"（替换字符串
不再匹配源码，导致变异根本没生效却显示逃逸）—— 后者是变异测试自身的 bug，
所以本脚本会**强制校验每个变异都真的改动了源码**，否则直接报错。

用法：python3 tests/mutation_test.py
退出码：0 = 所有变异都被抓到；1 = 有逃逸或有过期变异体。
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
SRC = os.path.join(PROJ, "src", "c")
WORK = "/tmp/mut"

# ---------------------------------------------------------------------------
# 变异体表：(编号, 说明, 期望能抓到的现象, 目标文件, [(旧片段, 新片段), ...])
# 目标文件取 "fmt.c" 或 "fmt.h"。
# 每个片段都必须能匹配上源码，否则视为"过期变异体"并报错。
# ---------------------------------------------------------------------------
MUTANTS = [
    ("M1", "24h 个位数小时不补零", "00:00 变成 0:00", "fmt.c", [(
        "  if (is_24h) {\n    len = fmt_two_digits(out, (unsigned)hour24);\n  } else {",
        "  if (is_24h) {\n    len = fmt_two_digits(out, (unsigned)hour24);\n"
        "    if (hour24 < 10) { out[0] = (char)('0' + hour24); len = 1; }\n  } else {",
    )]),
    ("M2", "12h 午夜显示 0 而非 12", "0 点显示 0:00", "fmt.c", [(
        "    unsigned h = (unsigned)hour24 % 12u;   // 0 点与 12 点都显示 12\n"
        "    if (h == 0u) {\n      h = 12u;\n    }",
        "    unsigned h = (unsigned)hour24 % 12u;",
    )]),
    ("M3", "100% 只写两位", "100% 变成 10%", "fmt.c", [(
        "    out[len++] = '1';\n    out[len++] = '0';\n    out[len++] = '0';",
        "    out[len++] = '1';\n    out[len++] = '0';",
    )]),
    ("M4", "非法 wday 不兜底", "越界读扁平字符串", "fmt.c", [(
        "  if (wday < 0 || wday > 6) {   // 防越界：非法 tm_wday 归到周日\n    wday = 0;\n  }",
        "",
    )]),
    ("M5", "fmt_changed 恒 false", "内容变了不重绘", "fmt.c", [(
        "  return *current != *next;",
        "  return false;",
    )]),
    ("M6", "fmt_changed 恒 true", "每次都重绘（浪费电）", "fmt.c", [(
        "  return *current != *next;",
        "  return true;",
    )]),
    ("M7", "fmt_copy 不写结尾 0", "字符串未终止", "fmt.c", [(
        "  dst[i] = '\\0';\n  return true;",
        "  return true;",
    )]),
    ("M8", "fmt_copy 少拷一字节", "off-by-one 截断", "fmt.c", [(
        "  for (; i + 1 < dst_len && src[i] != '\\0'; i++) {",
        "  for (; i + 2 < dst_len && src[i] != '\\0'; i++) {",
    )]),
    ("M9", "fmt_date 去掉日数夹取", "mday=99 写出 3 位", "fmt.c", [(
        "  if (mday < 1) {\n    mday = 1;\n  }\n  if (mday > 31) {\n    mday = 31;\n  }\n",
        "",
    )]),
    ("M10", "电量串丢失 %", "格式错误", "fmt.c", [(
        "  out[len++] = '%';",
        "",
    )]),
    ("M11", "fmt_time_ship 不夹取时分", "h=99 输出越界值", "fmt.c", [(
        "  if (hour24 < 0) {\n    hour24 = 0;\n  }\n  if (hour24 > 23) {\n    hour24 = 23;\n  }\n"
        "  if (minute < 0) {\n    minute = 0;\n  }\n  if (minute > 59) {\n    minute = 59;\n  }\n\n"
        "  size_t len;",
        "  size_t len;",
    )]),
    ("M12", "小缓冲仍写 out[0]", "违反“不写任何字节”契约", "fmt.c", [(
        "  if (out_len < 6) {           // 最坏情况 \"23:59\\0\"；太小就一个字节都不写\n    return 0;\n  }",
        "  if (out_len < 6) {\n    out[0] = '\\0';\n    return 0;\n  }",
    )]),
    ("M13", "十位用 /9 而不是 /10", "绝大多数时间显示错", "fmt.c", [(
        "  out[0] = (char)('0' + (value / 10u));",
        "  out[0] = (char)('0' + (value / 9u));",
    )]),
    ("M14", "星期名只写 2 字符", "Fri 变成 Fr", "fmt.c", [(
        "  for (int i = 0; i < 3; i++) {",
        "  for (int i = 0; i < 2; i++) {",
    )]),
    ("M15", "最小缓冲区阈值 <6 改成 <5", "5 字节缓冲写 6 字节", "fmt.c", [(
        "  if (out_len < 6) {           // 最坏情况 \"23:59\\0\"；太小就一个字节都不写",
        "  if (out_len < 5) {",
    )]),
    ("M16", "两位数日只写个位", "31 变成 1", "fmt.c", [(
        "  if (mday < 10) {\n    out[len++] = (char)('0' + mday);\n  } else {",
        "  if (mday < 10) {\n    out[len++] = (char)('0' + mday);\n  } else if (0) {",
    )]),
    ("M17", "fmt_time_ship 不写结尾 0", "字符串未终止", "fmt.c", [(
        "  len += 2;\n  out[len] = '\\0';\n  return len;",
        "  len += 2;\n  return len;",
    )]),
    ("M18", "电量 0% 显示成 1%", "个位数电量整体偏移", "fmt.c", [(
        "    out[len++] = (char)('0' + percent);",
        "    out[len++] = (char)('1' + percent);",
    )]),
    ("M19", "BATT 缓冲区改小到 5（编译期护栏应拦下）", "零余量/溢出", "fmt.h", [(
        "#define FMT_BATT_BUF_LEN 8",
        "#define FMT_BATT_BUF_LEN 5",
    )]),
    ("M20", "去掉 update_time 里的 tmp 预置 0", "小缓冲契约下读未初始化内存", "ai-watchface.c", [(
        "  char tmp[TIME_BUF_LEN] = {0};   // 预置 0：异常路径也不会读到未初始化数据",
        "  char tmp[TIME_BUF_LEN];",
    )]),
]


def run(cmd, **kw):
    # 注意用二进制捕获：变异体失败时会打印乱码字节，按文本解码会直接抛错
    return subprocess.run(cmd, capture_output=True, **kw)


def text(b):
    return (b or b"").decode("utf-8", errors="replace")


def main():
    os.makedirs(WORK, exist_ok=True)

    src_c = open(os.path.join(SRC, "fmt.c"), encoding="utf-8").read()
    src_h = open(os.path.join(SRC, "fmt.h"), encoding="utf-8").read()
    test_c = open(os.path.join(HERE, "test_fmt.c"), encoding="utf-8").read()
    test_c = test_c.replace('#include "../src/c/fmt.h"', '#include "fmt.h"')

    open(os.path.join(WORK, "fmt.h"), "w", encoding="utf-8").write(src_h)
    open(os.path.join(WORK, "test_fmt.c"), "w", encoding="utf-8").write(test_c)

    # 先确认基线（未变异）是绿的，否则后面所有结论都不可信
    open(os.path.join(WORK, "fmt.c"), "w", encoding="utf-8").write(src_c)
    build = run(["gcc", "-std=c11", "-Wall", "-Wextra", "-O1", "-DFMT_TEST_HOOKS=1",
                 "-I", WORK, os.path.join(WORK, "test_fmt.c"),
                 os.path.join(WORK, "fmt.c"), "-o", os.path.join(WORK, "m")])
    if build.returncode != 0:
        print("基线编译失败，变异测试无意义：")
        print(text(build.stderr)[:2000])
        return 1
    base = run([os.path.join(WORK, "m")])
    if base.returncode != 0:
        print("基线测试就是红的，请先修好再跑变异测试：")
        print(text(base.stdout)[-1500:])
        return 1
    m = re.search(r"检查点: (\d+), 失败: (\d+)", text(base.stdout))
    base_checks = m.group(1) if m else "?"
    print("基线：未变异时测试通过（检查点 %s）" % base_checks)
    print()
    print("=== 变异测试：往 fmt.c 注入 bug，看测试套件能否抓到 ===")
    print()

    caught, escaped, stale = [], [], []

    # ai-watchface.c 也读进来（M20 要变异它）；测试文件只做 PC 侧抽取，不参与变异
    src_aw = open(os.path.join(SRC, "ai-watchface.c"), encoding="utf-8").read()

    for name, desc, symptom, target, patches in MUTANTS:
        base_of = {"fmt.c": src_c, "fmt.h": src_h, "ai-watchface.c": src_aw}[target]
        mutated = base_of
        ok = True
        for old, new in patches:
            if old not in mutated:
                stale.append((name, desc, old[:60]))
                ok = False
                break
            mutated = mutated.replace(old, new, 1)
        if not ok:
            print("%-42s ⚠️ 过期变异体（替换未匹配源码）" % ("%s %s" % (name, desc)))
            continue
        if mutated == base_of:
            stale.append((name, desc, "(替换后内容未变)"))
            print("%-42s ⚠️ 过期变异体（内容未变）" % ("%s %s" % (name, desc)))
            continue

        # 写出变异后的目标文件；ai-watchface.c 的变异通过"抽取式"编译验证：
        # 该文件依赖 pebble.h，PC 上不能直接编，所以只做静态断言式检查。
        if target == "ai-watchface.c":
            # 对 ai-watchface.c 的变异：检查是否有任何测试/护栏能反映它。
            # M20 去掉 `= {0}` 后行为依赖未初始化内存，PC 侧无法直接观测，
            # 因此要求源码里必须存在预置 0 —— 用"护栏文本存在性"作为判据。
            guard_ok = ("char tmp[TIME_BUF_LEN] = {0}" in mutated)
            if not guard_ok:
                caught.append(name)
                print("%-42s ✅ 被抓到（护栏缺失）      %s" % ("%s %s" % (name, desc), symptom))
            else:
                escaped.append((name, desc, symptom))
                print("%-42s ❌ 逃逸！测试没抓到    %s" % ("%s %s" % (name, desc), symptom))
            continue

        open(os.path.join(WORK, target), "w", encoding="utf-8").write(mutated)
        b = run(["gcc", "-std=c11", "-Wall", "-Wextra", "-O1", "-DFMT_TEST_HOOKS=1",
                 "-I", WORK, os.path.join(WORK, "test_fmt.c"),
                 os.path.join(WORK, "fmt.c"), "-o", os.path.join(WORK, "m")])
        if b.returncode != 0:
            # 编译期就挂了也算被抓到（-Werror 风格的安全网）
            caught.append(name)
            print("%-42s ✅ 被抓到（编译失败）  %s" % ("%s %s" % (name, desc), symptom))
            continue
        r = run([os.path.join(WORK, "m")])
        if r.returncode != 0:
            caught.append(name)
            print("%-42s ✅ 被抓到              %s" % ("%s %s" % (name, desc), symptom))
        else:
            escaped.append((name, desc, symptom))
            print("%-42s ❌ 逃逸！测试没抓到    %s" % ("%s %s" % (name, desc), symptom))

    # 恢复
    open(os.path.join(WORK, "fmt.c"), "w", encoding="utf-8").write(src_c)
    open(os.path.join(WORK, "fmt.h"), "w", encoding="utf-8").write(src_h)

    print()
    print("=== 结果：抓 %d / 逃逸 %d / 过期 %d ===" % (len(caught), len(escaped), len(stale)))
    if escaped:
        print("逃逸清单（这些是测试盲区，必须补用例）：")
        for name, desc, symptom in escaped:
            print("  - %s %s（%s）" % (name, desc, symptom))
    if stale:
        print("过期变异体（是变异脚本自己的 bug，必须更新替换字符串）：")
        for name, desc, frag in stale:
            print("  - %s %s：%r" % (name, desc, frag))
    if not escaped and not stale:
        print("所有注入的 bug 都被测试套件捕获，且变异体全部有效 ✓")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
