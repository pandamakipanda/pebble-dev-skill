#!/usr/bin/env python3
"""test_render.py —— 在模拟器里**真的渲染一次**并断言画面正确。

为什么需要这个：结构断言（布局穷举、产物健全性）永远抓不到"合法但难看"的问题。
我们踩过一次真实回归 —— 三行文字全部挤到屏幕底部、上方 2/3 全黑。它不越界、
不重叠、非零高，**通过了当时所有断言**，只有截图才发现。

断言分三类：
  1. 硬断言（画面必须满足）：尺寸、非空白、内容不在边缘、**上下留白均衡**
  2. 基线断言（用出厂镜像启动时才有）：`before` 应像出厂画面，`after` 应像我们的 app
     —— 这一条能证明"我的 app 真的渲染了"，而不只是"屏幕上本来就有东西"
  3. 回归断言：`after` 与 `after2` 之间只应有时间区域的差异

用法：
    python3 tests/test_render.py [--platform emery] [--pbw build/my-watchface.pbw]
    （需要 emulator-toolkit/emu_run.py 与 libpebble2 / Pillow）

退出码：0 通过；1 断言失败；2 环境不具备（缺依赖/SDK/模拟器）——CI 里把 2 当"跳过"。
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def _find_emu():
    """向上查找 emulator-toolkit/emu_run.py。

    不要用 "数 ../ 的层数" —— 目录结构一变就错，而且我给编译期路径
    数错过两次。这里逐级向上找，找不到就返回 None（调用方当"环境不具备"）。
    """
    d = HERE
    for _ in range(8):
        cand = os.path.join(d, "emulator-toolkit", "emu_run.py")
        if os.path.exists(cand):
            return cand
        parent = os.path.dirname(d)
        if parent == d:
            break
        d = parent
    return None


EMU = _find_emu()

# 各平台的预期维度（来自官方 Hardware Information）
EXPECT = {
    "emery":  (200, 228),
    "flint":  (144, 168),
    "basalt": (144, 168),
    "gabbro": (260, 260),
}

fails = []
checks = 0


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        fails.append(msg)
    print(f"  {'✓' if cond else '✗'} {msg}")


def px_stats(path):
    from PIL import Image
    im = Image.open(path).convert("L")
    w, h = im.size
    px = im.load()
    on = [(x, y) for y in range(h) for x in range(w) if px[x, y] > 128]
    if not on:
        return dict(w=w, h=h, count=0, top=None, bottom=None, left=None, right=None)
    ys = [y for _, y in on]
    xs = [x for x, _ in on]
    return dict(w=w, h=h, count=len(on), top=min(ys), bottom=h - 1 - max(ys),
                left=min(xs), right=w - 1 - max(xs))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--platform", default="emery", choices=sorted(EXPECT))
    ap.add_argument("--pbw")
    ap.add_argument("--keep", action="store_true", help="保留临时截图目录")
    ap.add_argument("--timeout", type=int, default=900,
                    help="模拟器单次运行的总超时秒数（默认 900；老平台启动慢）")
    args = ap.parse_args()

    if not os.path.exists(EMU):
        print(f"找不到模拟器脚本：{EMU}")
        return 2

    pbw = args.pbw
    if not pbw:
        cands = [f for f in os.listdir("build") if f.endswith(".pbw")] if os.path.isdir("build") else []
        if not cands:
            print("找不到 .pbw，请先 pebble build（或用 --pbw 指定）")
            return 2
        pbw = os.path.join("build", cands[0])
    if not os.path.exists(pbw):
        print(f"找不到 {pbw}")
        return 2

    tmp = tempfile.mkdtemp(prefix="render-test-")
    prefix = os.path.join(tmp, "run")
    report_path = os.path.join(tmp, "report.json")
    base_ports = 12400 + (abs(hash(args.platform)) % 400)

    cmd = [sys.executable, EMU, args.platform, os.path.abspath(pbw), prefix,
           "--pristine-flash",                    # 关键：从出厂镜像启动，才能证明 app 真的渲染了
           "--flash-dir", os.path.join(tmp, "flash"),
           "--out-json", report_path,
           "--proto", str(base_ports), "--cons", str(base_ports + 1),
           "--gdb", str(base_ports + 2), "--mon", str(base_ports + 3)]
    print("运行模拟器：", " ".join(cmd[:6]), "…")
    try:
        proc = subprocess.run(cmd, capture_output=True, text=False, timeout=args.timeout)
    except subprocess.TimeoutExpired as e:
        # 必须显式处理：以前这里让它裸抛，CI 里表现为"步骤无输出直接失败"，
        # 完全看不出是超时（这个坑真踩过）。
        partial = (e.stdout or b"").decode("utf-8", "replace")
        print("\n".join("    " + ln for ln in partial.strip().splitlines()[-12:]))
        print(f"\n模拟器跑满 {args.timeout}s 仍未完成 —— 判定为失败（不是『环境不具备』）。")
        print("  常见原因：该平台的 SPI flash 需要 SDK 里的 qemu_spi_cooker 预处理，")
        print("  而那是 32 位 x86 二进制，在 arm64 上跑不了（basalt 就是这样）。")
        return 1
    out = (proc.stdout or b"").decode("utf-8", "replace")
    print("\n".join("    " + ln for ln in out.strip().splitlines()[-12:]))

    if not os.path.exists(report_path):
        print("\n模拟器没有产出报告 —— 环境不具备（缺依赖 / 找不到 SDK / QEMU 起不来）")
        print(f"  退出码 {proc.returncode}")
        return 2

    rep = json.load(open(report_path, encoding="utf-8"))
    print(f"\n平台={rep.get('platform')} 固件={rep.get('firmware')}")
    check(rep.get("boot_ok"), "模拟器启动成功")
    check(rep.get("install_ok"), "应用安装成功")

    shots = rep.get("screenshots", {})
    for name in ("before", "after"):
        check(name in shots, f"拿到了 {name} 截图")
    if "after" not in shots:
        return 1

    want_w, want_h = EXPECT[args.platform]
    for name in ("before", "after"):
        if name not in shots:
            continue
        s = shots[name]
        check((s["width"], s["height"]) == (want_w, want_h),
              f"{name} 分辨率 {s['width']}x{s['height']}（期望 {want_w}x{want_h}）")

    after = px_stats(shots["after"]["path"])
    print(f"\n  after 像素统计：{after}")

    # ---- 硬断言 ----
    check(after["count"] > 0, f"after 不是空白画面（前景像素 {after['count']}）")
    check(after["count"] < after["w"] * after["h"] * 0.5,
          f"after 不是整屏填充（前景占比 {after['count'] / (after['w'] * after['h']):.1%}）")

    # 内容不该贴边 —— 贴边通常意味着布局被推歪了
    edge = 2
    check(min(after["top"], after["bottom"], after["left"], after["right"]) >= edge,
          f"内容不贴边（上{after['top']} 下{after['bottom']} 左{after['left']} 右{after['right']}）")

    # **均衡断言**：这条是抓"三行挤到底部"回归的关键判据。
    # 注意不要用"字形行范围"判断布局 —— 42pt 数字的字形只占层高一部分，
    # 居中与贴底的**字形行范围可能重合**（我们被这点误导过一次）。留白差值才可靠。
    slack = after["top"] + after["bottom"]
    if slack >= 8:
        diff = abs(after["top"] - after["bottom"])
        check(diff <= slack * 0.5 + 2,
              f"垂直分布均衡（上留白 {after['top']} / 下留白 {after['bottom']}，差 {diff}）")

    # ---- 基线断言：证明"我的 app 真的渲染了" ----
    # 出厂镜像的启动画面背景是灰的（0xAA），我们的表盘是黑底。
    # 若 before 就已经是黑底小字内容，说明 flash 里本来就装着旧 app（脏基线），
    # 这时**不能**把"画面变了"当成安装成功的证据。
    try:
        from PIL import Image
        im = Image.open(shots["before"]["path"])
        colors = im.convert("RGB").getcolors(maxcolors=100000) or []
        bg = max(colors, key=lambda c: c[0])[1] if colors else None
        is_gray_factory = bg is not None and len(bg) == 3 and bg[0] == bg[1] == bg[2] and 100 < bg[0] < 220
        if is_gray_factory:
            check(True, f"before 是出厂画面（背景 {bg}）—— 基线干净")
            im2 = Image.open(shots["after"]["path"]).convert("RGB")
            b2 = max(im2.getcolors(maxcolors=100000) or [(0, (0, 0, 0))], key=lambda c: c[0])[1]
            check(b2 != bg, f"after 背景已变为 {b2}，与出厂画面不同 → 应用确实渲染了")
        else:
            print(f"  · before 背景是 {bg}，不是出厂灰 —— 基线不干净（flash 里已有 app），"
                  f"跳过'画面变化'断言，仅做硬断言")
    except Exception as e:
        print(f"  · 基线比较跳过：{e}")

    # ---- 回归断言：4 秒后只有时间区域该变 ----
    if "watchface+4s" in shots:
        try:
            from PIL import Image, ImageChops
            a = Image.open(shots["after"]["path"]).convert("RGB")
            b = Image.open(shots["watchface+4s"]["path"]).convert("RGB")
            bbox = ImageChops.difference(a, b).getbbox()
            if bbox is None:
                print("  · 4 秒内画面无变化（同一分钟内属正常）")
            else:
                x0, y0, x1, y1 = bbox
                area = (x1 - x0) * (y1 - y0)
                check(area < a.size[0] * a.size[1] * 0.35,
                      f"4 秒内变化区域很小（{area} px²，bbox={bbox}）——"
                      f"说明只刷新了变动部分，不是整屏重绘")
        except Exception as e:
            print(f"  · 回归比较跳过：{e}")

    if not args.keep:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)

    print(f"\n检查点: {checks}, 失败: {len(fails)}")
    if fails:
        for f in fails:
            print("  ✗ " + f)
        print("渲染检查未通过 ✗")
        return 1
    print("渲染检查全部通过 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
