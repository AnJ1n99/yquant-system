#!/usr/bin/env bash
# 用替身工具验证日志与退出码，不编译或运行 C++。
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
FIXTURE="$(mktemp -d)"
trap 'rm -rf "$FIXTURE"' EXIT
mkdir -p "$FIXTURE/scripts" "$FIXTURE/bin" "$FIXTURE/benchmark" "$FIXTURE/common" "$FIXTURE/build/bench/benchmark"
cp "$ROOT/scripts/benchmark.sh" "$FIXTURE/scripts/benchmark.sh"
for path in CMakeLists.txt benchmark/CMakeLists.txt benchmark/FastQueue.cc common/ringBuffer.h; do
  printf '测试输入\n' > "$FIXTURE/$path"
done
cat > "$FIXTURE/bin/cmake" <<'TOOL'
#!/usr/bin/env bash
printf '配置或编译输出\n'
exit "${CONFIG_EXIT:-0}"
TOOL
cat > "$FIXTURE/bin/git" <<'TOOL'
#!/usr/bin/env bash
printf '测试版本\n'
TOOL
cat > "$FIXTURE/bin/clang++-19" <<'TOOL'
#!/usr/bin/env bash
printf '测试编译器\n'
TOOL
cat > "$FIXTURE/build/bench/benchmark/fast_queue_test" <<'TOOL'
#!/usr/bin/env bash
printf '基准原始数据\n'
printf '测试错误输出\n' >&2
exit "${TEST_EXIT:-0}"
TOOL
chmod +x "$FIXTURE/bin/"* "$FIXTURE/build/bench/benchmark/fast_queue_test"
export PATH="$FIXTURE/bin:$PATH"

check_run() {
  local expected="$1" status=0
  bash "$FIXTURE/scripts/benchmark.sh" > "$FIXTURE/terminal" 2>&1 || status=$?
  [[ "$status" -eq "$expected" ]] || { cat "$FIXTURE/terminal"; exit 1; }
}
check_run 0
check_run 0
logs=("$FIXTURE/build/bench/logs/"*.log)
[[ "${#logs[@]}" -eq 2 ]]
for log in "${logs[@]}"; do
  grep -q '基准原始数据' "$log"
  grep -q '测试错误输出' "$log"
  grep -q '退出码：0' "$log"
done
export CONFIG_EXIT=42
check_run 42
unset CONFIG_EXIT
export TEST_EXIT=17
check_run 17
unset TEST_EXIT
grep -q '退出码：17' "$FIXTURE/terminal"
logs=("$FIXTURE/build/bench/logs/"*.log)
[[ "${#logs[@]}" -eq 4 ]]
printf '日志回归通过：独立文件、标准输出、错误输出、配置失败、测试失败。\n'
