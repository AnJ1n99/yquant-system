#!/usr/bin/env bash
# Check or format the repository's C/C++ sources with clang-format 20.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "$ROOT"

usage() {
  cat <<'EOF'
Usage:
  scripts/format.sh                         # check all project sources
  scripts/format.sh --check [paths...]
  scripts/format.sh --check --all
  scripts/format.sh --write paths...
  scripts/format.sh --write --all

Only tracked or unignored C/C++ files under common/, exchange/, trading/, and
main.cpp are eligible. Writing always requires an explicit --write flag.
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

resolve_clang_format() {
  local candidate resolved

  if [[ "${CLANG_FORMAT+x}" == x ]]; then
    [[ -n "$CLANG_FORMAT" ]] || die 'CLANG_FORMAT is set but empty'
    resolved="$(find_executable "$CLANG_FORMAT")" ||
      die "CLANG_FORMAT is not executable: $CLANG_FORMAT"
    printf '%s\n' "$resolved"
    return 0
  fi

  for candidate in \
    /opt/homebrew/opt/llvm@20/bin/clang-format \
    /usr/local/opt/llvm@20/bin/clang-format \
    /usr/lib/llvm-20/bin/clang-format \
    clang-format-20 \
    clang-format; do
    if resolved="$(find_executable "$candidate")"; then
      printf '%s\n' "$resolved"
      return 0
    fi
  done

  die 'clang-format 20 not found; install LLVM 20 or set CLANG_FORMAT'
}

require_llvm_20() {
  local binary="$1" version major
  version="$("$binary" --version 2>&1)" ||
    die "failed to run $binary --version"
  if [[ "$version" =~ version[[:space:]]+([0-9]+) ]]; then
    major="${BASH_REMATCH[1]}"
  else
    die "could not determine clang-format version from: $version"
  fi
  [[ "$major" == 20 ]] ||
    die "clang-format major version 20 is required; found: $version"
  printf '%s\n' "${version%%$'\n'*}"
}

is_cxx_source() {
  case "$1" in
    *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx) return 0 ;;
    *) return 1 ;;
  esac
}

is_project_path() {
  case "$1" in
    main.cpp|common|common/*|exchange|exchange/*|trading|trading/*) return 0 ;;
    *) return 1 ;;
  esac
}

canonical_existing_path() {
  local input="$1" candidate directory base
  if [[ "$input" == /* ]]; then
    candidate="$input"
  else
    candidate="$ROOT/$input"
  fi

  [[ -e "$candidate" || -L "$candidate" ]] || die "path not found: $input"
  [[ ! -L "$candidate" ]] || die "symbolic links are not eligible: $input"

  if [[ -d "$candidate" ]]; then
    (cd "$candidate" && pwd -P)
    return 0
  fi

  directory="$(cd "$(dirname "$candidate")" && pwd -P)"
  base="$(basename "$candidate")"
  printf '%s/%s\n' "$directory" "$base"
}

repo_relative_path() {
  local canonical="$1"
  case "$canonical" in
    "$ROOT"/*) printf '%s\n' "${canonical#"$ROOT"/}" ;;
    *) die "path is outside the repository: $canonical" ;;
  esac
}

FILES=()

append_file() {
  local file="$1" existing
  is_cxx_source "$file" || return 0
  is_project_path "$file" || return 0
  [[ -f "$ROOT/$file" ]] || return 0
  [[ ! -L "$ROOT/$file" ]] || die "symbolic links are not eligible: $file"
  for existing in "${FILES[@]+"${FILES[@]}"}"; do
    [[ "$existing" == "$file" ]] && return 0
  done
  FILES+=("$file")
}

collect_path() {
  local input="$1" canonical relative matched=0 file
  canonical="$(canonical_existing_path "$input")"
  relative="$(repo_relative_path "$canonical")"
  is_project_path "$relative" ||
    die "path is outside the project C/C++ source set: $input"

  if [[ -f "$canonical" ]]; then
    is_cxx_source "$relative" || die "not a C/C++ source file: $input"
  fi

  while IFS= read -r -d '' file; do
    if is_cxx_source "$file" && is_project_path "$file"; then
      append_file "$file"
      matched=1
    fi
  done < <(git ls-files -co --exclude-standard -z -- "$relative")

  ((matched == 1)) ||
    die "path contains no tracked or unignored C/C++ source files: $input"
}

MODE=check
MODE_EXPLICIT=0
ALL=0
PATHS=()

while (($# > 0)); do
  case "$1" in
    --check)
      if ((MODE_EXPLICIT)) && [[ "$MODE" != check ]]; then
        die '--check and --write cannot be combined'
      fi
      MODE=check
      MODE_EXPLICIT=1
      ;;
    --write)
      if ((MODE_EXPLICIT)) && [[ "$MODE" != write ]]; then
        die '--check and --write cannot be combined'
      fi
      MODE=write
      MODE_EXPLICIT=1
      ;;
    --all)
      ALL=1
      ;;
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

if ((ALL)) && ((${#PATHS[@]} > 0)); then
  die '--all cannot be combined with explicit paths'
fi
if [[ "$MODE" == write ]] && ((ALL == 0)) && ((${#PATHS[@]} == 0)); then
  die '--write requires explicit paths or --all'
fi
if ((ALL == 0)) && ((${#PATHS[@]} == 0)); then
  ALL=1
fi

if ((ALL)); then
  while IFS= read -r -d '' file; do
    append_file "$file"
  done < <(git ls-files -co --exclude-standard -z -- \
    common exchange trading main.cpp)
else
  for path in "${PATHS[@]+"${PATHS[@]}"}"; do
    collect_path "$path"
  done
fi

((${#FILES[@]} > 0)) || die 'no eligible C/C++ source files found'

CLANG_FORMAT_BIN="$(resolve_clang_format)"
CLANG_FORMAT_VERSION="$(require_llvm_20 "$CLANG_FORMAT_BIN")"
"$CLANG_FORMAT_BIN" --style=file --assume-filename="$ROOT/main.cpp" \
  --dump-config >/dev/null || die 'failed to parse .clang-format'

printf 'Using: %s (%s)\n' "$CLANG_FORMAT_BIN" "$CLANG_FORMAT_VERSION"

if [[ "$MODE" == check ]]; then
  status=0
  for file in "${FILES[@]+"${FILES[@]}"}"; do
    if diagnostic="$(
      "$CLANG_FORMAT_BIN" --style=file --dry-run --Werror -- "$file" 2>&1
    )"; then
      continue
    else
      printf 'would reformat: %s\n' "$file" >&2
      if [[ -n "$diagnostic" ]]; then
        printf '  %s\n' "${diagnostic%%$'\n'*}" >&2
      fi
      status=1
    fi
  done
  if ((status != 0)); then
    die 'clang-format check failed; use --write with explicit paths or --all'
  fi
  printf 'clang-format check passed (%d files)\n' "${#FILES[@]}"
  exit 0
fi

"$CLANG_FORMAT_BIN" --style=file -i -- "${FILES[@]+"${FILES[@]}"}"
printf 'formatted %d files\n' "${#FILES[@]}"
