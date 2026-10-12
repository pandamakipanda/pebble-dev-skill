# 安全策略

## 报告漏洞

请**不要**用公开 issue 报告安全问题。请通过 GitHub 的
[Private vulnerability reporting](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability)
提交，或直接联系维护者。

我们会在 7 天内回复。

## 本项目的安全边界（先读这个，避免误报）

本仓库**不包含**、也**不应该包含**：

- 任何 GitHub token、API key、私钥（`.gitignore` 已排除常见凭据文件；
  历史上曾有人误把 token 提交进仓库 —— 提交前请 `git diff --cached` 自查）
- 任何用户数据

**`emulator-toolkit/` 会启动本地 QEMU 进程并监听本地端口**（默认 12400+ 段）。
这是设计行为，不是漏洞。但请注意：

- 这些端口**只绑定在 localhost**，不要把它们暴露到公网
- `emu_run.py` 接受的 `.pbw` 路径来自命令行，不会下载远程文件
- 脚本会在 `/tmp` 下复制 flash 镜像，不会改动 SDK 的持久目录

## 上游依赖

本项目的安全也取决于上游：[pebble-tool](https://github.com/coredevices/pebble-tool)、
[PebbleOS](https://github.com/coredevices/PebbleOS)、`libpebble2`。
发现上游问题请同时报告给上游。
