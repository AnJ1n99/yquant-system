#!/usr/bin/env bash
# 在远端 Linux 构建机上构建 Release 与 Debug 两套产物。
# 用法：
#   scripts/build.sh          增量构建
#   scripts/build.sh fresh    删掉构建目录，从零配置并全量重建
# 覆盖编译器：CXX_COMPILER=g++ scripts/build.sh
set -euo pipefail

# 与调用时所在目录无关，始终在仓库根执行
cd "$(dirname "${BASH_SOURCE[0]}")/.."

CXX_COMPILER="${CXX_COMPILER:-clang++-19}"
JOBS="$(nproc)"
FRESH="${1:-}"

for tool in cmake ninja "$CXX_COMPILER"; do
    command -v "$tool" >/dev/null || { echo "找不到 $tool" >&2; exit 1; }
done

build_one() {
    local build_type="$1" build_dir="$2" cached

    if [[ "$FRESH" == "fresh" ]]; then
        rm -rf "$build_dir"
    else
        # 换编译器后旧缓存会让 CMake 直接报错，先检测再清理
        cached="$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$build_dir/CMakeCache.txt" 2>/dev/null || true)"
        if [[ -n "$cached" && "$cached" != "$(command -v "$CXX_COMPILER")" ]]; then
            echo "编译器已变更（$cached -> $CXX_COMPILER），重新配置 $build_dir"
            rm -rf "$build_dir"
        fi
    fi

    cmake -S . -B "$build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DCMAKE_CXX_COMPILER="$CXX_COMPILER"

    cmake --build "$build_dir" -j "$JOBS"
}

build_one Release cmake-build-release
build_one Debug   cmake-build-debug

echo "构建完成：$CXX_COMPILER，-j $JOBS"
