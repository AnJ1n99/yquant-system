#!/usr/bin/env bash
# 从任意目录配置、编译、运行队列测试，完整输出保存在独立日志中。
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [[ "$(uname -s)" != Linux ]]; then
  printf '仅支持 Linux；请 SSH 到构建机后执行此脚本。\n' >&2
  exit 1
fi

mkdir -p build/bench/logs
LOG_FILE="$(mktemp "build/bench/logs/run-$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX.log")"
printf '日志文件：%s/%s\n' "$PWD" "$LOG_FILE"

# pipefail 保留构建或测试的失败码，防止 tee 成功掩盖前面的失败。
(
  trap 'status=$?; printf "\n退出码：%d\n" "$status"' EXIT
  printf '开始时间（UTC）：%s\n' "$(date -u +%FT%TZ)"
  printf '工作目录：%s\n' "$PWD"
  printf '系统：'; uname -srmo
  printf '可用 CPU 数量：'; nproc
  printf 'Git 提交：'; git rev-parse HEAD
  printf '工作区状态：\n'; git status --porcelain
  printf '源码校验值：\n'
  sha256sum common/ringBuffer.h benchmark/FastQueue.cc \
    benchmark/CMakeLists.txt CMakeLists.txt scripts/benchmark.sh
  printf '编译器：\n'; clang++-19 --version
  printf '测试参数：'
  if (($#)); then printf ' %q' "$@"; else printf '（默认全部）'; fi
  printf '\n'

  cmake -S . -B build/bench -G Ninja \
    -DCMAKE_CXX_COMPILER=clang++-19 \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
  cmake --build build/bench --target fast_queue_test -j2
  printf '测试程序校验值：\n'
  sha256sum build/bench/benchmark/fast_queue_test
  stdbuf -oL -eL ./build/bench/benchmark/fast_queue_test "$@"
) 2>&1 | tee "$LOG_FILE"
