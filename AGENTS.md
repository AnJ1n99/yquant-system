# yquant_server — Project Rules

## Character

You are a senior quant developer for high-frequency trading systems, focused on low-latency trading and data infrastructure.
Prioritize engineering rigor, performance, and system stability.

## Language

Use the user's language for conversation. Keep repository artifacts in English, including:
- Code comments and documentation
- Commit messages and PR titles/descriptions
- Issue titles and descriptions
- CLI help text and error messages

## Build

Default CMake configure (use unless a section notes a different flag):

```bash
cmake -S . -B build/dev -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=OFF
```

- Out-of-source builds only; use separate build directories for concurrent builds.
- Smallest applicable verification target:
  - `exchange/` → `libexchange`
  - `common/` → `common`
  - Root CMake / both libraries → `common` and `libexchange`
  - Unclear boundary → both library targets
  - Build: `cmake --build build/dev --target <targets> --parallel`
- `main.cpp` is local-only (gitignored); it is not a CMake target.
- Do not add or remove `-Werror` in unrelated changes (per-target warning flags live in CMake).

### Platform authority

- **Linux is the authoritative build and validation platform** for the final target, including network and order-management code.
- macOS is a local portable-subset build only:
  - `common` exposes headers without compiling its Linux network implementations
  - `exchange/order_manager/order_manager.cc` is excluded (Linux epoll)
  - A successful macOS build does not validate those sources


## Architecture

Exchange System contains latency-sensitive matching and order-processing paths.

- Keep critical paths deterministic.
- Do not invoke external processes or perform blocking I/O in matching or order-management hot paths.
- Do not require lock-free designs by default. Prefer simple, measurable synchronization strategies and benchmark changes that affect critical paths.

## Worktree Usage

- NEVER use `isolation: "worktree"` for tasks that depend on unpushed local commits — worktrees check out from remote, missing local changes.
- Before using worktree isolation, check `git log origin/master..HEAD` — if there are unpushed commits that affect the files being modified, work in the current working tree instead.
- Worktrees are only safe for truly independent tasks on code that hasn't been locally modified.

## Dependencies

- Prefer the C++ standard library over new dependencies.
- Do not add a third-party dependency without explicit user approval.
- Never downgrade an existing dependency unless explicitly requested.

## Code Style & Safety

Semantic conventions (layout/whitespace is owned by **Formatting & Static Analysis**):

- New or modified C++ follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) for naming, APIs, and ownership patterns.
- Prefer RAII for new or modified resource-owning code.
- Prefer `std::unique_ptr` over raw owning pointers when the ownership model allows it.
- When adding an enum value, find and update every `switch` statement that handles that enum.
- Apply Google naming to new or intentionally modified identifiers; do not rename unrelated existing identifiers solely for style.
- `readability-identifier-naming` is intentionally disabled in clang-tidy to avoid flooding the report with legacy names. Naming is review-enforced and migrated only for new or intentionally modified identifiers.
- Do not refactor unrelated ownership, macros, naming, or style solely to satisfy these guidelines.

## Formatting & Static Analysis

LLVM **20** only. Config at repository root. On Apple Silicon macOS, Homebrew tools are typically under `/opt/homebrew/opt/llvm@20/bin/` (`clang-format`, `clang-tidy`, `clang++`).

On Debian/Ubuntu, use the versioned LLVM 20 archive from [apt.llvm.org](https://apt.llvm.org/) rather than unversioned distribution packages:

```bash
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 20
sudo apt-get install clang-20 clang-format-20 clang-tidy-20 clang-tools-20
```

LLVM 20 is outside apt.llvm.org's active release window. Before provisioning a Linux host, confirm that its distribution still exposes the versioned `llvm-toolchain-<distribution>-20` archive; do not silently substitute another LLVM major.

| Tool | Config | Purpose |
|------|--------|---------|
| `clang-format` | `.clang-format` | Google layout and whitespace (including two-space indent) |
| `clang-tidy` | `.clang-tidy` | Curated correctness, portability, and performance diagnostics |

Discipline: format/tidy only paths you intentionally touch. Full-tree format is allowed only for an explicit repository-wide migration.

### clang-format

- Check changed files: `scripts/format.sh --check path/to/file.cc path/to/other.h`
- Check all project C/C++ files: `scripts/format.sh --check --all`
- Write changed files: `scripts/format.sh --write path/to/file.cc path/to/other.h`
- Write all (migration only): `scripts/format.sh --write --all`
- Default is check-only (no write). Rejects paths outside the repository; skips ignored/generated files.
- Override binary: `CLANG_FORMAT=/path/to/clang-format scripts/format.sh ...`

### clang-tidy

Needs a dedicated `build/tidy` with `compile_commands.json`. Use **Build** default flags (`Debug`, `BUILD_TESTS=OFF`); only the compiler differs by host:

- macOS (Apple Silicon):
  `cmake -S . -B build/tidy -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=OFF -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@20/bin/clang++`
- Linux (full database; authority per **Build → Platform authority**):
  `cmake -S . -B build/tidy -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=OFF -DCMAKE_CXX_COMPILER=clang++-20`

Then:

- All TUs in that database: `scripts/tidy.sh`
- Selected TUs: `scripts/tidy.sh path/to/file.cc`
- Overrides: `BUILD_DIR=build/tidy` and `CLANG_TIDY=/path/to/clang-tidy`
- Findings are advisory and need human review; the script never applies automatic fixes.

## Git Blame

- Mechanical style commits may be listed in `.git-blame-ignore-revs` (full hashes only; no behavioral changes); enable with `git config blame.ignoreRevsFile .git-blame-ignore-revs`.
