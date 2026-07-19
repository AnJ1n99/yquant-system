# yquant_system — Project Rules

## Language

Use the user's language for conversation. Keep repository artifacts in English, including:
- Code comments and documentation
- Commit messages and PR titles/descriptions
- Issue titles and descriptions
- CLI help text and error messages

## Build

- Configure builds from the repository root using an out-of-source build directory:
  `cmake -S . -B build/dev -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=OFF`
- For changes to `main.cpp`, build:
  `cmake --build build/dev --target yquant_server --parallel`
- For changes under `exchange/`, build:
  `cmake --build build/dev --target libexchange --parallel`
- For changes under `common/` or to CMake files, build both targets:
  `cmake --build build/dev --target yquant_server libexchange --parallel`
- Use the smallest applicable target build above as the current verification step; build both targets when the affected boundary is unclear.
- Use separate build directories when running concurrent builds.
- On non-Linux systems, `exchange/order_manager/order_manager.cc` is excluded because it depends on Linux epoll; do not treat a macOS build as validation of that source file.

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

- Match the existing style of the module being changed.
- Prefer RAII for new or modified resource-owning code.
- Prefer `std::unique_ptr` over raw owning pointers when the ownership model allows it.
- When adding an enum value, find and update every `switch` statement that handles that enum.
- Do not refactor unrelated ownership, macros, or style solely to satisfy these guidelines.
