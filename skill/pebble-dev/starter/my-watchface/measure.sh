#!/usr/bin/env bash
# measure.sh —— 可复现的体积/性能测量脚本。
#
# 用法：
#   ./measure.sh build     只构建并打印 .pbw 与各平台 ELF 段大小
#   ./measure.sh ab        额外做 A/B：用 snprintf/strftime 的对照组 vs 手写 fmt
#   ./measure.sh all       两者都做（默认）
#
# 设计原则：所有数字都从真实构建产物里读，不写死。
set -euo pipefail

cd "$(dirname "$0")"
export PATH=/root/ptvenv/bin:$PATH

SDK=${PEBBLE_SDK_DIR:-/root/.local/share/pebble-sdk/SDKs/4.33.1}
TOOLCHAIN="$SDK/toolchain/arm-none-eabi/bin"
SIZE="$TOOLCHAIN/arm-none-eabi-size"
NM="$TOOLCHAIN/arm-none-eabi-nm"

hr() { printf '%s\n' "------------------------------------------------------------"; }

do_build() {
  hr
  echo "[1/3] pebble build"
  hr
  pebble clean >/dev/null 2>&1 || true
  local t0 t1
  t0=$(date +%s.%N)
  pebble build >/tmp/build.log 2>&1 || { tail -30 /tmp/build.log; exit 1; }
  t1=$(date +%s.%N)

  echo "构建耗时: $(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.1f s", b-a}')"
  echo "构建日志: /tmp/build.log"
  hr
  echo "[2/3] .pbw 产物"
  hr
  ls -l build/*.pbw | awk '{printf "%-40s %8d 字节\n", $9, $5}'
  echo "内容清单（各平台二进制/资源）:"
  unzip -l build/*.pbw | awk 'NR>3 && $1 ~ /^[0-9]+$/ {printf "  %-36s %7d\n", $4, $1}'

  hr
  echo "[3/3] 各平台 ELF 段大小（真实代码体积）"
  hr
  printf "%-10s %8s %8s %8s %10s\n" "平台" ".text" ".data" ".bss" "RAM占用"
  for p in basalt emery flint gabbro; do
    local elf="build/$p/pebble-app.elf"
    [ -f "$elf" ] || continue
    local text data bss
    text=$($SIZE -A "$elf" | awk '$1==".text"{print $2; exit}')
    data=$($SIZE -A "$elf" | awk '$1==".data"{print $2; exit}')
    bss=$($SIZE -A "$elf" | awk '$1==".bss"{print $2; exit}')
    printf "%-10s %8s %8s %8s %10s\n" "$p" "$text" "$data" "$bss" "$((data+bss))"
  done

  hr
  echo "printf/strftime 家族是否被链进最终固件"
  hr
  for p in basalt emery flint gabbro; do
    local elf="build/$p/pebble-app.elf"
    [ -f "$elf" ] || continue
    local n
    n=$($NM "$elf" | grep -icE 'snprintf|vsnprintf|strftime|_vfprintf|__printf' || true)
    printf "  %-8s %s 处\n" "$p" "$n"
  done
}

AB_BACKUP=/tmp/ab_src_backup
AB_PKG_BACKUP=/tmp/ab_package.json

# 无论脚本怎么退出，都把源码恢复回去；备份放在 /tmp，绝不在工程里留旧快照
restore_src() {
  if [ -d "$AB_BACKUP" ]; then
    rm -rf src
    cp -a "$AB_BACKUP" src
  fi
  if [ -f "$AB_PKG_BACKUP" ]; then
    cp "$AB_PKG_BACKUP" package.json
  fi
}

do_ab() {
  hr
  echo "A/B 对照：库函数格式化(snprintf/strftime) vs 手写 fmt —— 完整链接对比"
  hr
  # 提前体检：如果 src/ 里没有 fmt_time_ship，说明快照是旧的/源码被覆盖过
  if ! grep -q fmt_time_ship src/c/fmt.c; then
    echo "!! 中止：src/c/fmt.c 里没有 fmt_time_ship，源码可能被旧快照覆盖过。" >&2
    echo "   先修好源码再跑 A/B，否则量出来的是错的。" >&2
    return 1
  fi
  trap restore_src EXIT INT TERM
  echo "做法：把 bench/ 换成 src/ 真构建一次朴素版，再换回来构建优化版，"
  echo "      两边都量最终 ELF 的 .text 与 .pbw 大小。"
  echo

  # 每次都重建快照：否则会拿旧源码去量，数字是错的（踩过一次）
  rm -rf "$AB_BACKUP"
  cp -a src "$AB_BACKUP"
  cp package.json "$AB_PKG_BACKUP"

  measure_variant() {
    local name="$1"
    pebble clean >/dev/null 2>&1 || true
    if ! pebble build >/tmp/ab_build.log 2>&1; then
      echo "!! [$name] 构建失败"; tail -20 /tmp/ab_build.log; return 1
    fi
    local pbw text
    pbw=$(stat -c %s build/*.pbw)
    text=$($SIZE -A build/emery/pebble-app.elf | awk '$1==".text"{print $2; exit}')
    local printfn
    printfn=$($NM build/emery/pebble-app.elf | grep -icE 'snprintf|vsnprintf|strftime|_vfprintf|__printf' || true)
    printf "%-26s .text(emery)=%-7s .pbw=%-8s printf家族符号=%s\n" "$name" "$text" "$pbw" "$printfn"
    echo "$name $text $pbw $printfn" >> /tmp/ab_result.txt
  }

  rm -f /tmp/ab_result.txt

  # 变体 1：朴素版（snprintf/strftime）
  rm -rf src && mkdir -p src/c
  cp bench/main_naive.c src/c/ai-watchface.c
  cp bench/package.json package.json
  measure_variant "朴素版(snprintf-strftime)"

  # 变体 2：优化版（手写 fmt）
  rm -rf src && cp -a src_opt_backup src
  cp package.json.opt package.json
  measure_variant "优化版(手写fmt)"

  restore_src
  trap - EXIT INT TERM

  hr
  if [ -s /tmp/ab_result.txt ]; then
    awk '{
      if (NR==1) { n1=$1; t1=$2+0; p1=$3+0 }
      else       { n2=$1; t2=$2+0; p2=$3+0 }
    }
    END {
      printf "%-28s %10s %10s\n", "指标", "朴素版", "优化版"
      printf "%-28s %10d %10d  (%+d 字节, %+.1f%%)\n", ".text (emery)", t1, t2, t2-t1, (t1? (t2-t1)*100/t1 : 0)
      printf "%-28s %10d %10d  (%+d 字节, %+.1f%%)\n", ".pbw 总大小", p1, p2, p2-p1, (p1? (p2-p1)*100/p1 : 0)
    }' /tmp/ab_result.txt
  fi
  hr
  echo "说明：.pbw 里每个平台固定带一份 4092 字节的空资源包(系统资源)，"
  echo "      它不随代码变化，是体积下限的主要成分。"
}

case "${1:-all}" in
  build) do_build ;;
  ab)    do_ab ;;
  all)   do_build; do_ab ;;
  *)     echo "用法: $0 [build|ab|all]"; exit 1 ;;
esac
