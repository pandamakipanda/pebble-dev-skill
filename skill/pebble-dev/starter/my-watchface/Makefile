# ai-watchface 工程脚手架
#
# 常用：
#   make test    本机单元测试 + 差分对拍 + 布局不变量（带 UBSan + 边界金丝雀）
#   make mutate  变异测试：注入已知 bug，验证测试套件真的抓得到
#   make bench   CPU 成本基准
#   make verify  产物健全性检查（构建成功 ≠ 产物可用）
#   make size    真实构建 + 体积分解
#   make ab      朴素版 vs 优化版的完整链接 A/B
#   make all     以上全部
#
# 说明：容器是 aarch64 Android，工程在 /sdcard（fuse）上不支持 chmod +x，
#       所以统一用 `make` 或 `bash measure.sh` 调用，别用 ./measure.sh。

CC_HOST   ?= gcc
CFLAGS_HOST = -std=c11 -Wall -Wextra -Werror -O2
UBSAN       = -fsanitize=undefined -fno-sanitize-recover=all

SRC_FMT = src/c/fmt.c
HDR_FMT = src/c/fmt.h

.PHONY: all test bench size ab build clean help

all: test build

help:
	@echo "make test   本机单元测试 + 差分对拍 + 布局不变量"
	@echo "make mutate 变异测试（证明测试有效）"
	@echo "make bench  CPU 成本基准"
	@echo "make build  pebble build"
	@echo "make verify 产物健全性检查（防\"构建成功但装不上\"）"
	@echo "make size   构建 + 体积分解"
	@echo "make ab     朴素版 vs 优化版 A/B"
	@echo "make clean  清理构建产物"

test: /tmp/test_fmt /tmp/test_layout
	@echo "=== 格式化逻辑 ==="
	@/tmp/test_fmt
	@echo
	@echo "=== 布局不变量 ==="
	@/tmp/test_layout

/tmp/test_layout: tests/test_layout.c $(SRC_FMT) $(HDR_FMT)
	$(CC_HOST) $(CFLAGS_HOST) $(UBSAN) -g -O1 -I src/c tests/test_layout.c $(SRC_FMT) -o $@
	@echo "已编译布局测试（UBSan 开启）"

mutate:
	@python3 tests/mutation_test.py

# 测试构建打开 FMT_TEST_HOOKS：让 fmt.c 里的时间格式化函数导出，
# 这样"被测代码"与"出货代码"是同一份源文件（出货时它们是 static，会被 gc 掉）
/tmp/test_fmt: tests/test_fmt.c $(SRC_FMT) $(HDR_FMT)
	$(CC_HOST) $(CFLAGS_HOST) $(UBSAN) -g -O1 -DFMT_TEST_HOOKS=1 tests/test_fmt.c $(SRC_FMT) -o $@
	@echo "已编译测试（UBSan 开启，FMT_TEST_HOOKS=1）"

bench: /tmp/bench_fmt
	@/tmp/bench_fmt

/tmp/bench_fmt: bench/bench_fmt.c $(SRC_FMT) $(HDR_FMT)
	$(CC_HOST) $(CFLAGS_HOST) -DFMT_TEST_HOOKS=1 bench/bench_fmt.c $(SRC_FMT) -o $@

.PHONY: build
build:
	pebble build

verify:
	python3 tests/verify_pbw.py

size: verify
	bash measure.sh build

ab:
	bash measure.sh ab

clean:
	pebble clean
	rm -f /tmp/test_fmt /tmp/test_layout /tmp/bench_fmt
