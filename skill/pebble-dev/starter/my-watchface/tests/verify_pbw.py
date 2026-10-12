#!/usr/bin/env python3
"""verify_pbw.py —— 产物健全性检查（构建成功 ≠ 产物可用）。

**为什么需要这个脚本**：实测踩过一次——在 wscript 里加 `-flto` 后构建照样
"finished successfully"，但 `pebble-app.bin` 开头**没有 `PBLAPP` 元数据头**，
整个包装到手表会直接失败。原因是 `inject_metadata.py` 是**原地改写** objcopy
出来的 bin，而 LTO 下目标文件是 GIMPLE 字节码、objcopy 取不到内容，
注入器"写了个寂寞"且不报错。

所以每次构建后都该跑一遍这个检查。用法：

    python3 tests/verify_pbw.py [build/xxx.pbw]

退出码 0 = 全部通过；1 = 有硬伤。
"""
import json
import os
import struct
import sys
import zipfile

PBLAPP_MAGIC = b"PBLAPP\0\0"
# PebbleProcessInfo 的字段偏移（见 SDK 的 fw/app_management/pebble_process_info.h）
OFF_LOAD_SIZE = 0x0E      # uint16
OFF_NAME = 0x18           # 32 字节，UTF-8
OFF_COMPANY = 0x38        # 32 字节，UTF-8
OFF_VIRTUAL_SIZE = 0x80   # uint16
STRUCT_SIZE = 0x82


def fail(msg):
    print("  ✗ " + msg)
    return 1


def main():
    pbw = sys.argv[1] if len(sys.argv) > 1 else None
    if pbw is None:
        # 默认取唯一的 .pbw
        import glob
        cands = glob.glob("build/*.pbw")
        if len(cands) != 1:
            print("用法: python3 tests/verify_pbw.py <file.pbw>")
            return 1
        pbw = cands[0]

    if not os.path.exists(pbw):
        print("找不到 %s" % pbw)
        return 1

    print("检查 %s（%d 字节）" % (pbw, os.path.getsize(pbw)))
    bad = 0
    z = zipfile.ZipFile(pbw)

    if z.testzip() is not None:
        bad += fail("zip CRC 校验失败")

    app = json.loads(z.read("appinfo.json").decode("utf-8"))
    names = z.namelist()
    plats = sorted({n.split("/")[0] for n in names if "/" in n})
    if not plats:
        return fail("包里没有任何平台目录")

    print("  平台: %s" % ", ".join(plats))
    print("  uuid: %s" % app["uuid"])

    for plat in plats:
        for f in ("pebble-app.bin", "app_resources.pbpack", "manifest.json"):
            if "%s/%s" % (plat, f) not in names:
                bad += fail("%s 缺少 %s" % (plat, f))
        if "%s/pebble-app.bin" % plat not in names:
            continue

        b = z.read("%s/pebble-app.bin" % plat)

        # 1) 元数据魔数：这是 LTO 那次事故的直接判据
        if b[:8] != PBLAPP_MAGIC:
            bad += fail("%s: pebble-app.bin 开头不是 PBLAPP 魔数（得到 %s）"
                        "—— 元数据注入失败，包装到手表会失败"
                        % (plat, b[:8].hex()))
            continue

        # 2) 元数据字段能否正常解析（UTF-8 名称等）
        if len(b) < STRUCT_SIZE:
            bad += fail("%s: bin 比元数据结构还短" % plat)
            continue
        try:
            name = b[OFF_NAME:OFF_NAME + 32].split(b"\0")[0].decode("utf-8")
            company = b[OFF_COMPANY:OFF_COMPANY + 32].split(b"\0")[0].decode("utf-8")
        except UnicodeDecodeError as e:
            bad += fail("%s: 元数据里的名称不是合法 UTF-8（%s）" % (plat, e))
            continue
        (load_size,) = struct.unpack_from("<H", b, OFF_LOAD_SIZE)
        (virt_size,) = struct.unpack_from("<H", b, OFF_VIRTUAL_SIZE)

        # 3) 与 manifest 交叉核对，确保注入的是这次编译的结果
        mf = json.loads(z.read("%s/manifest.json" % plat))
        msize = mf["application"]["size"]
        if msize != len(b):
            bad += fail("%s: manifest 记的大小 %d ≠ 实际 %d" % (plat, msize, len(b)))

        # 4) 合理性：虚拟大小（.text+.data+.bss）不能为 0、不应超过加载大小太多
        if virt_size == 0:
            bad += fail("%s: virtual_size 为 0，元数据可疑" % plat)
        if len(b) < load_size:
            bad += fail("%s: bin(%d) 比 load_size(%d) 还小" % (plat, len(b), load_size))

        print("  %-7s app=%-6d load=%-6d virt=%-6d name=%r company=%r"
              % (plat, len(b), load_size, virt_size, name, company))

    if bad:
        print("\n发现 %d 处问题 ✗" % bad)
        return 1
    print("\n产物健全性检查全部通过 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
