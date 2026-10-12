#!/usr/bin/env python3
"""emu_run.py —— 无显示器环境下启动 Pebble QEMU、装 .pbw、截图。

这是本仓库最省事的模拟器路线：**不需要 pypkjs、websockify、VNC、Xvfb 或 SDL 窗口**。
直接连 QEMU 的 Pebble 协议串口，截图由固件自己渲染回传。

用法：
    emu_run.py <platform> <pbw|-> <out-prefix> [选项]

    platform    emery | flint | gabbro | basalt
    pbw         .pbw 路径；给 "-" 表示只启动并截启动画面
    out-prefix  输出前缀，会生成 <prefix>-before.png / -after.png / -after2.png

选项（一般不用给，会自动发现）：
    --proto/--cons/--gdb/--mon PORT   四个串口/监视口（默认 12344/12345/12346/12347）
    --sdk-root DIR                    Pebble SDK 安装根（含 toolchain/ 与 sdk-core/）
    --qemu PATH                       qemu-pebble 可执行文件
    --flash-dir DIR                   SPI flash 镜像存放目录（默认 $TMPDIR/pebble-emu）

环境变量：
    PEBBLE_SDK_ROOT     等价于 --sdk-root
    PEBBLE_QEMU         等价于 --qemu
    PEBBLE_EMU_FLASH_DIR 等价于 --flash-dir

依赖：`pip install libpebble2 Pillow`（如果用 pebble-tool 的 venv，路径会自动猜）

退出码：0 成功；1 启动/连接失败；2 参数或环境问题。
"""
import argparse
import glob
import os
import signal
import subprocess
import sys
import tempfile
import time


# --------------------------------------------------------------------------
# 路径自动发现：绝不硬编码某台机器的绝对路径
# --------------------------------------------------------------------------
def _find_libpebble2_venv():
    """如果 libpebble2 装在 pebble-tool 的 venv 里，把它的 site-packages 加进 sys.path。

    这只在"用 pipx/uv 装 pebble-tool、却用系统 python 跑本脚本"时才需要。
    找不到就安静跳过 —— 调用方可能自己装好了依赖。
    """
    try:
        import libpebble2  # noqa: F401
        return None
    except ImportError:
        pass
    pats = [
        os.path.expanduser("~/.local/share/uv/tools/pebble-tool/lib/python3*/site-packages"),
        os.path.expanduser("~/.local/pipx/venvs/pebble-tool/lib/python3*/site-packages"),
        "/root/ptvenv/lib/python3*/site-packages",
        os.path.expanduser("~/.pebble-venv/lib/python3*/site-packages"),
    ]
    for pat in pats:
        for cand in sorted(glob.glob(pat), reverse=True):
            if os.path.isdir(cand):
                return cand
    return None


def _detect_sdk_root(explicit):
    """按优先级找 SDK 根目录（里面有 toolchain/ 和 sdk-core/）。"""
    cands = []
    if explicit:
        cands.append(explicit)
    if os.environ.get("PEBBLE_SDK_ROOT"):
        cands.append(os.environ["PEBBLE_SDK_ROOT"])
    home = os.path.expanduser("~")
    # pebble-tool 的持久目录（真实位置与官方文档不一致，这点很坑）
    if os.path.isdir(pebble_sdks := os.path.join(home, ".local/share/pebble-sdk/SDKs")):
        cands += sorted(glob.glob(os.path.join(pebble_sdks, "*")), reverse=True)
    cands += [
        os.path.join(home, ".pebble-sdk/SDKs"),   # 旧版路径
        "/opt/pebble-sdk",
    ]
    for c in cands:
        if c and os.path.isdir(os.path.join(c, "toolchain")) and \
           os.path.isdir(os.path.join(c, "sdk-core")):
            return os.path.realpath(c)
    return None


def _detect_qemu(explicit, sdk_root):
    for c in (explicit, os.environ.get("PEBBLE_QEMU")):
        if c and os.path.exists(c):
            return c
    if sdk_root:
        for name in ("qemu-pebble", "qemu-pebble.exe"):
            p = os.path.join(sdk_root, "toolchain/bin", name)
            if os.path.exists(p):
                return p
    from shutil import which
    return which("qemu-pebble")


# --------------------------------------------------------------------------
# 各平台 QEMU 机器参数
# --------------------------------------------------------------------------
CFG = {
    "emery":  dict(machine="pebble-emery",    cpu="cortex-m33", flash="mtd",  audio=True),
    "flint":  dict(machine="pebble-flint",    cpu="cortex-m4",  flash="mtd",  audio=True),
    "gabbro": dict(machine="pebble-gabbro",   cpu="cortex-m33", flash="mtd",  audio=True),
    "basalt": dict(machine="pebble-snowy-bb", cpu="cortex-m4",  flash="none", audio=False),
}

# SPI flash 尺寸：emery/flint/gabbro 是 32 MiB，basalt 是 16 MiB。
# 尺寸错了固件起不来（而且不报错，只是不响应协议）—— 这个坑踩过。
FLASH_SIZE = {"emery": 32 << 20, "flint": 32 << 20, "gabbro": 32 << 20, "basalt": 16 << 20}


def _spi_candidates(platform, sdk_root):
    """按"最可能已经解压好"的顺序列出 SPI flash 的候选位置。"""
    home = os.path.expanduser("~")
    cands = []
    # 1) 用户显式指定的目录
    # 2) pebble-tool 运行时解压出来的持久副本（最省事，通常已存在）
    pdir = os.path.join(home, ".local/share/pebble-sdk")
    if os.path.isdir(pdir):
        cands += sorted(glob.glob(os.path.join(pdir, "*", platform, "qemu_spi_flash.bin")),
                        reverse=True)
    # 3) SDK 里的原始镜像（未压缩）
    if sdk_root:
        cands.append(os.path.join(sdk_root, "sdk-core/pebble", platform, "qemu",
                                  "qemu_spi_flash.bin"))
    return cands


def _spi_bz2(platform, sdk_root):
    if not sdk_root:
        return None
    p = os.path.join(sdk_root, "sdk-core/pebble", platform, "qemu", "qemu_spi_flash.bin.bz2")
    return p if os.path.exists(p) else None


def ensure_flash(platform, flash_dir, sdk_root):
    """准备 SPI flash 镜像。

    顺序：目标位置已有正确尺寸 → 从 pebble-tool 的持久副本拷贝 →
    从 SDK 的 .bz2 解压 → 都失败就报错并给出可复制的命令。

    **尺寸必须对**：emery/flint/gabbro 是 32 MiB、basalt 是 16 MiB。
    尺寸错了 QEMU 能起、端口在听，但固件不响应协议 —— 非常难查，所以这里硬校验。
    """
    want = FLASH_SIZE[platform]
    os.makedirs(flash_dir, exist_ok=True)
    dest = os.path.join(flash_dir, f"{platform}_spi.bin")

    if os.path.exists(dest) and os.path.getsize(dest) == want:
        return dest

    import shutil
    for cand in _spi_candidates(platform, sdk_root):
        if os.path.exists(cand) and os.path.getsize(cand) == want:
            shutil.copyfile(cand, dest)
            print(f"flash: 复用 {cand}", flush=True)
            return dest

    bz2 = _spi_bz2(platform, sdk_root)
    if bz2:
        import bz2
        print(f"flash: 从 {os.path.basename(bz2)} 解压…", flush=True)
        with bz2.open(bz2, "rb") as src, open(dest, "wb") as dst:
            shutil.copyfileobj(src, dst)
        if os.path.getsize(dest) == want:
            return dest
        print(f"!! 解压后尺寸 {os.path.getsize(dest)} ≠ 期望 {want}", file=sys.stderr)

    # 最后手段：建一个正确尺寸的空镜像（PebbleOS 会在首次启动时初始化）
    if not os.path.exists(dest) or os.path.getsize(dest) != want:
        print(f"flash: 未找到现成镜像，创建 {want} 字节空镜像", flush=True)
        with open(dest, "wb") as f:
            f.truncate(want)
    return dest


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="无显示器环境跑 Pebble QEMU 并截图",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("platform", choices=sorted(CFG))
    ap.add_argument("pbw", help=".pbw 路径，或 '-' 表示只截启动画面")
    ap.add_argument("prefix", help="输出前缀")
    ap.add_argument("--proto", type=int, default=12344)
    ap.add_argument("--cons", type=int, default=12345)
    ap.add_argument("--gdb", type=int, default=12346)
    ap.add_argument("--mon", type=int, default=12347)
    # 向后兼容：旧调用方式是 <plat> <pbw> <prefix> <proto> <cons> <gdb> <mon>
    # 四个端口作为可选位置参数追加在最后。
    ap.add_argument("ports", nargs="*", type=int,
                    help="（兼容旧写法）proto cons gdb mon 四个端口")
    ap.add_argument("--sdk-root")
    ap.add_argument("--qemu")
    ap.add_argument("--flash-dir")
    args = ap.parse_args(argv)
    if args.ports:
        if len(args.ports) != 4:
            ap.error("位置端口参数要么全不给，要么给全四个 (proto cons gdb mon)")
        args.proto, args.cons, args.gdb, args.mon = args.ports

    venv = _find_libpebble2_venv()
    if venv:
        sys.path.insert(0, venv)

    try:
        from libpebble2.communication import PebbleConnection          # noqa: F401
        from libpebble2.communication.transports.qemu import QemuTransport  # noqa: F401
        from libpebble2.services.screenshot import Screenshot          # noqa: F401
        from libpebble2.services.install import AppInstaller           # noqa: F401
        from PIL import Image                                          # noqa: F401
    except ImportError as e:
        print(f"缺少依赖：{e}\n请执行：pip install libpebble2 Pillow", file=sys.stderr)
        return 2

    sdk = _detect_sdk_root(args.sdk_root)
    if not sdk:
        print("找不到 Pebble SDK。请先 `pebble sdk install latest`，"
              "或用 --sdk-root / PEBBLE_SDK_ROOT 指定。", file=sys.stderr)
        return 2
    qemu = _detect_qemu(args.qemu, sdk)
    if not qemu:
        print("找不到 qemu-pebble。请确认 SDK 装好了，或用 --qemu 指定。", file=sys.stderr)
        return 2

    plat = args.platform
    c = CFG[plat]
    micro = os.path.join(sdk, "sdk-core", "pebble", plat, "qemu", "qemu_micro_flash.bin")
    if not os.path.exists(micro):
        print(f"缺少固件镜像：{micro}", file=sys.stderr)
        return 2

    flash_dir = args.flash_dir or os.environ.get("PEBBLE_EMU_FLASH_DIR") or \
        os.path.join(tempfile.gettempdir(), "pebble-emu")
    flash = ensure_flash(plat, flash_dir, sdk)
    if not os.path.exists(flash) or os.path.getsize(flash) != FLASH_SIZE[plat]:
        print(f"!! SPI flash 尺寸不对：{flash}\n"
              f"   {plat} 需要 {FLASH_SIZE[plat]} 字节。可从 SDK 解压：\n"
              f"   bzcat {os.path.dirname(micro)}/qemu_spi_flash.bin.bz2 > {flash}",
              file=sys.stderr)
        return 2

    print(f"SDK  = {sdk}\nQEMU = {qemu}\nFLASH= {flash} "
          f"({os.path.getsize(flash)} 字节)", flush=True)

    log_path = f"{args.prefix}.qemu.log"
    log = open(log_path, "wb")
    cmd = [qemu, "-rtc", "base=localtime", "-serial", "null",
           "-serial", f"tcp::{args.proto},server=on,wait=off",
           "-serial", f"tcp::{args.cons},server=on,wait=off",
           "-kernel", micro,
           "-gdb", f"tcp::{args.gdb},server=on,wait=off",
           "-monitor", f"tcp::{args.mon},server=on,wait=off",
           "-machine", c["machine"], "-cpu", c["cpu"]]
    if c["flash"] == "mtd":
        cmd += ["-drive", f"if=mtd,format=raw,file={flash}"]
    else:
        cmd += ["-drive", f"if=none,id=spi-flash,file={flash},format=raw"]
    if c["audio"]:
        cmd += ["-audio", "driver=sdl,id=audio0"]
    cmd += ["-display", "sdl,show-cursor=on"]

    proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    print(f"qemu pid = {proc.pid}（日志 {log_path}）", flush=True)
    try:
        return run(plat, args, c, proc, log_path)
    finally:
        try:
            os.kill(proc.pid, signal.SIGKILL)
        except Exception:
            pass
        time.sleep(0.5)
        print(f"killed qemu {proc.pid}", flush=True)


def run(plat, args, c, proc, log_path):
    from libpebble2.communication import PebbleConnection
    from libpebble2.communication.transports.qemu import QemuTransport
    from libpebble2.services.screenshot import Screenshot
    from libpebble2.services.install import AppInstaller
    from PIL import Image
    from collections import Counter

    conn = None
    for attempt in range(40):
        if proc.poll() is not None:
            print(f"!! QEMU 提前退出，rc={proc.poll()}", flush=True)
            print(open(log_path, errors="replace").read()[:3000])
            return 1
        time.sleep(1.5)
        try:
            p = PebbleConnection(QemuTransport("127.0.0.1", args.proto))
            p.connect()
            p.run_async()
            conn = p
            print(f"booted after ~{attempt * 1.5:.0f}s; "
                  f"fw={p.firmware_version} platform={p.watch_platform}", flush=True)
            break
        except Exception as e:
            if attempt % 5 == 0:
                print(f"  waiting... attempt {attempt}: {type(e).__name__}: {e}", flush=True)
    if conn is None:
        print("!! 一直没启动 / 协议无响应", flush=True)
        print(open(log_path, errors="replace").read()[:3000])
        return 1

    # 串口缓冲有限：不节流的话装大包会溢出（pebble-tool 也这么做）
    _orig = PebbleConnection.send_packet

    def throttled(self, packet):
        time.sleep(0.002)
        return _orig(self, packet)

    PebbleConnection.send_packet = throttled

    def shot(path, label):
        rows = Screenshot(conn).grab_image()
        h = len(rows)
        w = len(rows[0]) // 3
        img = Image.new("RGB", (w, h))
        px = img.load()
        for y, row in enumerate(rows):
            for x in range(w):
                px[x, y] = (row[x * 3], row[x * 3 + 1], row[x * 3 + 2])
        img.save(path)
        # 不用 img.getdata()（Pillow 14 起弃用）；按像素直接统计
        px2 = img.load()
        cc = Counter(px2[x, y] for y in range(h) for x in range(w))
        print(f"[{label}] {path} {w}x{h} colors={len(cc)} top={cc.most_common(4)}", flush=True)
        return img

    shot(f"{args.prefix}-before.png", "boot-screen")
    if args.pbw != "-":
        print("installing", args.pbw, os.path.getsize(args.pbw), "bytes", flush=True)
        t0 = time.time()
        AppInstaller(conn, args.pbw).install(force_install=True)
        print(f"install OK in {time.time() - t0:.1f}s", flush=True)
        time.sleep(5)
        shot(f"{args.prefix}-after.png", "watchface")
        time.sleep(4)
        shot(f"{args.prefix}-after2.png", "watchface+4s")
    print("DONE", plat, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
