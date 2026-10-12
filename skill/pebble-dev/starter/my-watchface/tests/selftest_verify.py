#!/usr/bin/env python3
"""selftest_verify.py —— 给 verify_pbw.py 做"体检"（测试的测试）。

为什么要这个：`verify_pbw.py` 是防"构建成功但产物装不上"的护栏。
如果护栏本身失效（比如判据写错、恒返回 0），它会静默放行坏包 —— 那比没有护栏更危险。
所以这里构造**已知的坏包**，确认 verify_pbw.py 会拒绝它们。

用法：python3 tests/selftest_verify.py
退出码 0 = 护栏有效。
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
VERIFY = os.path.join(HERE, "verify_pbw.py")

GOOD_APPINFO = {
    "displayName": "Test",
    "uuid": "6b4c3163-83a1-406d-aebd-388d4ead34c4",
    "sdkVersion": "3",
    "targetPlatforms": ["emery"],
    "watchapp": {"watchface": True},
}


def make_bin(name=b"Test", company=b"Test Co"):
    """造一个结构正确的 pebble-app.bin（PBLAPP 魔数 + 各字段）。"""
    b = bytearray(b"PBLAPP\0\0" + b"\0" * (0x90 - 8))
    b[0x0E:0x10] = (0x90).to_bytes(2, "little")       # load_size
    b[0x18:0x18 + len(name)] = name
    b[0x38:0x38 + len(company)] = company
    b[0x80:0x82] = (0x200).to_bytes(2, "little")      # virtual_size
    return bytes(b)


def build_pbw(path, app_bin, appinfo=None, manifest_size=None):
    appinfo = appinfo or GOOD_APPINFO
    z = zipfile.ZipFile(path, "w")
    z.writestr("appinfo.json", json.dumps(appinfo))
    z.writestr("emery/pebble-app.bin", app_bin)
    z.writestr("emery/app_resources.pbpack", b"\0" * 16)
    z.writestr("emery/manifest.json", json.dumps({
        "manifestVersion": 2,
        "application": {"name": "pebble-app.bin",
                        "size": len(app_bin) if manifest_size is None else manifest_size,
                        "crc": 0},
        "resources": {"name": "app_resources.pbpack", "size": 16, "crc": 0},
        "type": "application",
    }))
    z.close()


def run_verify(path):
    r = subprocess.run([sys.executable, VERIFY, path],
                       capture_output=True, text=False)
    return r.returncode


def main():
    tmp = tempfile.mkdtemp(prefix="verify-selftest-")
    cases = []

    # 好包：必须通过
    good = os.path.join(tmp, "good.pbw")
    build_pbw(good, make_bin())
    cases.append(("结构正确的包", good, 0))

    # 坏包 1：缺少 PBLAPP 魔数（就是 LTO 那次事故的形态）
    bad_magic = os.path.join(tmp, "bad-magic.pbw")
    build_pbw(bad_magic, b"\x04\0\0\0\x14\0\0\0" + b"\0" * 0x90)
    cases.append(("缺 PBLAPP 魔数（LTO 事故形态）", bad_magic, 1))

    # 坏包 2：元数据里的名称不是合法 UTF-8
    bad_utf8 = os.path.join(tmp, "bad-utf8.pbw")
    b = bytearray(make_bin())
    b[0x18:0x1A] = b"\x9a\xff"        # 非法 UTF-8
    build_pbw(bad_utf8, bytes(b))
    cases.append(("元数据名称非 UTF-8", bad_utf8, 1))

    # 坏包 3：manifest 记的大小与实际不符（注入的是旧编译产物）
    bad_size = os.path.join(tmp, "bad-size.pbw")
    build_pbw(bad_size, make_bin(), manifest_size=99999)
    cases.append(("manifest 大小与实际不符", bad_size, 1))

    # 坏包 4：virtual_size 为 0（元数据可疑）
    bad_virt = os.path.join(tmp, "bad-virt.pbw")
    b = bytearray(make_bin())
    b[0x80:0x82] = (0).to_bytes(2, "little")
    build_pbw(bad_virt, bytes(b))
    cases.append(("virtual_size 为 0", bad_virt, 1))

    # 坏包 5：缺平台目录
    bad_plat = os.path.join(tmp, "bad-plat.pbw")
    z = zipfile.ZipFile(bad_plat, "w")
    z.writestr("appinfo.json", json.dumps(GOOD_APPINFO))
    z.writestr("emery/manifest.json", "{}")
    z.close()
    cases.append(("平台三件套不全", bad_plat, 1))

    failed = 0
    print("verify_pbw.py 自检：")
    for label, path, want in cases:
        rc = run_verify(path)
        ok = (rc == want)
        print("  %-32s 期望退出码 %d，实得 %d  %s"
              % (label, want, rc, "✓" if ok else "✗ 护栏失效！"))
        if not ok:
            failed += 1

    shutil.rmtree(tmp, ignore_errors=True)

    if failed:
        print("\n%d 个用例不符合预期 —— verify_pbw.py 的判据有问题 ✗" % failed)
        return 1
    print("\nverify_pbw.py 自检全部通过：好包放行、坏包全部拦下 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
