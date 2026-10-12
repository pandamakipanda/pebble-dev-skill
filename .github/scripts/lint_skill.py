#!/usr/bin/env python3
"""lint_skill.py —— 校验 skill 文件的结构，避免"装了但加载不了"。

检查项：
  1. YAML frontmatter 存在，且有非空的 `name` 与 `description`
  2. `name` 与所在目录名一致（DSH 等工具按目录名定位 skill）
  3. 没有未闭合的代码围栏（会让后续内容被吞进代码块）
  4. 正文里出现的相对链接目标存在（防止指到不存在的文件）
  5. description 不过长（skill catalog 会把描述注入每轮上下文，太长是纯开销）

用法：python3 lint_skill.py <SKILL.md> [...]
退出码 0 = 通过。
"""
import os
import re
import sys

MAX_DESC = 400          # 经验值：catalog 摘要会被注入每轮上下文
REQUIRED_SECTIONS_HINT = ("## ",)   # 至少要有一个二级标题


def fail(msgs, path, text):
    msgs.append("%s: %s" % (path, text))


def parse_frontmatter(text):
    """返回 (frontmatter dict, 正文, 错误)"""
    if not text.startswith("---\n"):
        return None, text, "文件没有以 YAML frontmatter 开头"
    end = text.find("\n---", 4)
    if end == -1:
        return None, text, "frontmatter 没有闭合的 ---"
    raw = text[4:end]
    body = text[end + 4:]
    fm = {}
    for line in raw.splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if ":" not in line:
            return None, body, "frontmatter 里有无法解析的行: %r" % line
        k, v = line.split(":", 1)
        fm[k.strip()] = v.strip()
    return fm, body, None


def main(argv):
    paths = argv[1:]
    if not paths:
        print("用法: python3 lint_skill.py <SKILL.md> [...]")
        return 1

    msgs = []
    for path in paths:
        if not os.path.exists(path):
            msgs.append("%s: 文件不存在" % path)
            continue
        text = open(path, encoding="utf-8").read()
        fm, body, err = parse_frontmatter(text)
        if err:
            fail(msgs, path, err)
            continue

        # 1) 必填字段
        for key in ("name", "description"):
            if not fm.get(key):
                fail(msgs, path, "frontmatter 缺少非空的 `%s`" % key)
        name = fm.get("name", "")

        # 2) name 与目录名一致
        parent = os.path.basename(os.path.dirname(os.path.abspath(path)))
        if name and parent and name != parent:
            fail(msgs, path,
                 "`name: %s` 与所在目录名 %r 不一致（skill 按目录名定位）"
                 % (name, parent))

        # 3) 代码围栏配对
        fences = len(re.findall(r"^```", body, re.M))
        if fences % 2 != 0:
            fail(msgs, path, "代码围栏数量为奇数（%d），有未闭合的代码块" % fences)

        # 4) description 长度
        desc = fm.get("description", "")
        if len(desc) > MAX_DESC:
            fail(msgs, path,
                 "description 有 %d 字符，超过 %d（会被注入每一轮上下文）"
                 % (len(desc), MAX_DESC))

        # 5) 至少有一个二级标题（必须按行检查：正文第一行是 "# 标题"，
        #    用 body.lstrip().startswith("## ") 会永远匹配不上 —— 这个 bug 真犯过）
        if not any(re.match(r"^##\s", line) for line in body.splitlines()):
            fail(msgs, path, "正文里没有二级标题（## ...）")

        # 6) 相对链接的目标存在（只查本地相对路径，跳过 http/anchor）
        base = os.path.dirname(os.path.abspath(path))
        real_links = 0
        for m in re.finditer(r"\[[^\]]*\]\(([^)]+)\)", body):
            target = m.group(1).strip()
            if target.startswith(("http://", "https://", "#", "mailto:")):
                continue
            target = target.split("#", 1)[0]
            if not target:
                continue
            real_links += 1
            if not os.path.exists(os.path.join(base, target)):
                fail(msgs, path, "相对链接指向不存在的路径: %s" % target)

        print("  %s: name=%r desc=%d 字符 围栏=%d 相对链接=%d"
              % (path, name, len(desc), fences, real_links))

    if msgs:
        print()
        for m in msgs:
            print("::error::" + m if os.environ.get("GITHUB_ACTIONS") else "✗ " + m)
        return 1
    print("\nskill 结构检查通过 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
