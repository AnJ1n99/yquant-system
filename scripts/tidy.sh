#!/usr/bin/env bash
# Run read-only clang-tidy 20 checks from a CMake compilation database.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT"

usage() {
  cat <<'EOF'
Usage:
  scripts/tidy.sh                       # analyze every TU in build/tidy
  scripts/tidy.sh path/to/file.cc ...   # analyze selected database TUs

Environment:
  BUILD_DIR       CMake build directory (default: build/tidy)
  CLANG_TIDY      clang-tidy 20 executable
  RUN_CLANG_TIDY  matching run-clang-tidy executable for all-TU runs

This script is advisory and never applies automatic fixes.
EOF
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

find_executable() {
  local candidate="$1"
  if [[ "$candidate" == */* ]]; then
    [[ -x "$candidate" ]] || return 1
    printf '%s\n' "$candidate"
    return 0
  fi
  command -v "$candidate" 2>/dev/null
}

resolve_clang_tidy() {
  local candidate resolved

  if [[ "${CLANG_TIDY+x}" == x ]]; then
    [[ -n "$CLANG_TIDY" ]] || die 'CLANG_TIDY is set but empty'
    resolved="$(find_executable "$CLANG_TIDY")" ||
      die "CLANG_TIDY is not executable: $CLANG_TIDY"
    printf '%s\n' "$resolved"
    return 0
  fi

  for candidate in \
    /opt/homebrew/opt/llvm@20/bin/clang-tidy \
    /usr/local/opt/llvm@20/bin/clang-tidy \
    /usr/lib/llvm-20/bin/clang-tidy \
    clang-tidy-20 \
    clang-tidy; do
    if resolved="$(find_executable "$candidate")"; then
      printf '%s\n' "$resolved"
      return 0
    fi
  done

  die 'clang-tidy 20 not found; install LLVM 20 or set CLANG_TIDY'
}

resolve_run_clang_tidy() {
  local clang_tidy_bin="$1" candidate resolved tool_dir

  if [[ "${RUN_CLANG_TIDY+x}" == x ]]; then
    [[ -n "$RUN_CLANG_TIDY" ]] || die 'RUN_CLANG_TIDY is set but empty'
    resolved="$(find_executable "$RUN_CLANG_TIDY")" ||
      die "RUN_CLANG_TIDY is not executable: $RUN_CLANG_TIDY"
    printf '%s\n' "$resolved"
    return 0
  fi

  tool_dir="$(cd "$(dirname "$clang_tidy_bin")" && pwd -P)"
  for candidate in \
    "$tool_dir/run-clang-tidy" \
    /usr/lib/llvm-20/bin/run-clang-tidy \
    run-clang-tidy-20 \
    run-clang-tidy; do
    if resolved="$(find_executable "$candidate")"; then
      printf '%s\n' "$resolved"
      return 0
    fi
  done

  die 'run-clang-tidy not found; install the LLVM 20 clang tools package or set RUN_CLANG_TIDY'
}

validate_run_clang_tidy() {
  local binary="$1" help
  help="$("$binary" -h 2>&1)" || die "failed to run $binary -h"
  [[ "$help" == *clang-tidy-binary* ]] ||
    die "RUN_CLANG_TIDY does not appear to be run-clang-tidy: $binary"
}

require_llvm_20() {
  local binary="$1" version major
  version="$("$binary" --version 2>&1)" ||
    die "failed to run $binary --version"
  if [[ "$version" =~ version[[:space:]]+([0-9]+) ]]; then
    major="${BASH_REMATCH[1]}"
  else
    die "could not determine clang-tidy version from: $version"
  fi
  [[ "$major" == 20 ]] ||
    die "clang-tidy major version 20 is required; found: $version"
  printf '%s\n' "${version%%$'\n'*}"
}

is_translation_unit() {
  case "$1" in
    *.c|*.cc|*.cpp|*.cxx) return 0 ;;
    *) return 1 ;;
  esac
}

canonical_source_path() {
  local input="$1" candidate directory base canonical
  if [[ "$input" == /* ]]; then
    candidate="$input"
  else
    candidate="$ROOT/$input"
  fi

  [[ -f "$candidate" ]] || die "translation unit not found: $input"
  [[ ! -L "$candidate" ]] || die "symbolic links are not eligible: $input"
  directory="$(cd "$(dirname "$candidate")" && pwd -P)"
  base="$(basename "$candidate")"
  canonical="$directory/$base"
  case "$canonical" in
    "$ROOT"/*) ;;
    *) die "path is outside the repository: $input" ;;
  esac
  is_translation_unit "$canonical" || die "not a C/C++ translation unit: $input"
  printf '%s\n' "$canonical"
}

compile_database_contains() {
  local database="$1" source="$2" python_bin
  python_bin="$(command -v python3 2>/dev/null)" ||
    die 'python3 is required to read compile_commands.json'
  "$python_bin" - "$database" "$source" <<'PY'
import json
from pathlib import Path
import sys

database = Path(sys.argv[1])
target = Path(sys.argv[2]).resolve()

try:
    entries = json.loads(database.read_text(encoding="utf-8"))
except (OSError, UnicodeError, json.JSONDecodeError) as error:
    print(f"error: failed to parse {database}: {error}", file=sys.stderr)
    raise SystemExit(2)

for entry in entries:
    source = entry.get("file")
    if not isinstance(source, str):
        continue
    candidate = Path(source)
    if not candidate.is_absolute():
        directory = entry.get("directory")
        if not isinstance(directory, str):
            continue
        candidate = Path(directory) / candidate
    if candidate.resolve() == target:
        raise SystemExit(0)

raise SystemExit(1)
PY
}

PATHS=()
while (($# > 0)); do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      while (($# > 0)); do
        PATHS+=("$1")
        shift
      done
      break
      ;;
    -*) die "unknown option: $1" ;;
    *) PATHS+=("$1") ;;
  esac
  shift
done

if [[ "${BUILD_DIR+x}" == x ]]; then
  [[ -n "$BUILD_DIR" ]] || die 'BUILD_DIR is set but empty'
else
  BUILD_DIR=build/tidy
fi

if [[ "$BUILD_DIR" == /* ]]; then
  BUILD_DIR_CANDIDATE="$BUILD_DIR"
else
  BUILD_DIR_CANDIDATE="$ROOT/$BUILD_DIR"
fi
[[ -d "$BUILD_DIR_CANDIDATE" ]] || die "build directory not found: $BUILD_DIR"
BUILD_DIR_ABS="$(cd "$BUILD_DIR_CANDIDATE" && pwd -P)"
COMPILE_DB="$BUILD_DIR_ABS/compile_commands.json"
[[ -f "$COMPILE_DB" ]] ||
  die "$COMPILE_DB not found; configure the dedicated CMake build first"

CLANG_TIDY_BIN="$(resolve_clang_tidy)"
CLANG_TIDY_VERSION="$(require_llvm_20 "$CLANG_TIDY_BIN")"
"$CLANG_TIDY_BIN" --config-file="$ROOT/.clang-tidy" --verify-config \
  >/dev/null || die 'failed to parse .clang-tidy'

TIDY_EXTRA_ARGS=()
RUNNER_EXTRA_ARGS=()
if [[ "$(uname -s)" == Darwin ]]; then
  command -v xcrun >/dev/null 2>&1 ||
    die 'xcrun is required to locate the macOS SDK for clang-tidy'
  SDKROOT="$(xcrun --show-sdk-path 2>/dev/null)" ||
    die 'failed to locate the macOS SDK with xcrun'
  [[ -d "$SDKROOT" ]] || die "macOS SDK directory not found: $SDKROOT"
  TIDY_EXTRA_ARGS+=(
    --extra-arg="-isysroot$SDKROOT"
    --extra-arg=-stdlib=libc++
  )
  RUNNER_EXTRA_ARGS+=(
    -extra-arg="-isysroot$SDKROOT"
    -extra-arg=-stdlib=libc++
  )
fi

printf 'Using: %s (%s)\n' "$CLANG_TIDY_BIN" "$CLANG_TIDY_VERSION"
printf 'Compile DB: %s\n' "$COMPILE_DB"

if ((${#PATHS[@]} == 0)); then
  RUN_CLANG_TIDY_BIN="$(resolve_run_clang_tidy "$CLANG_TIDY_BIN")"
  validate_run_clang_tidy "$RUN_CLANG_TIDY_BIN"
  printf 'Runner: %s\n' "$RUN_CLANG_TIDY_BIN"
  if ! "$RUN_CLANG_TIDY_BIN" \
    -p "$BUILD_DIR_ABS" \
    -clang-tidy-binary "$CLANG_TIDY_BIN" \
    -config-file "$ROOT/.clang-tidy" \
    "${RUNNER_EXTRA_ARGS[@]+"${RUNNER_EXTRA_ARGS[@]}"}" \
    -quiet; then
    die 'clang-tidy failed; compiler errors or tool failures are not advisory'
  fi
  printf 'clang-tidy completed; review advisory diagnostics above\n'
  exit 0
fi

FILES=()
for path in "${PATHS[@]+"${PATHS[@]}"}"; do
  file="$(canonical_source_path "$path")"
  if compile_database_contains "$COMPILE_DB" "$file"; then
    :
  elif (($? == 1)); then
    die "translation unit is not present in $COMPILE_DB: $path"
  else
    exit 1
  fi
  duplicate=0
  for existing in "${FILES[@]+"${FILES[@]}"}"; do
    if [[ "$existing" == "$file" ]]; then
      duplicate=1
      break
    fi
  done
  ((duplicate == 1)) || FILES+=("$file")
done

status=0
for file in "${FILES[@]+"${FILES[@]}"}"; do
  if ! "$CLANG_TIDY_BIN" \
    --config-file="$ROOT/.clang-tidy" \
    -p="$BUILD_DIR_ABS" \
    --quiet \
    "${TIDY_EXTRA_ARGS[@]+"${TIDY_EXTRA_ARGS[@]}"}" \
    "$file"; then
    status=1
  fi
done

if ((status != 0)); then
  die 'clang-tidy failed; compiler errors or tool failures are not advisory'
fi
printf 'clang-tidy completed; review advisory diagnostics above\n'
