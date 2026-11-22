# {fmt} and cmake-git-version-tracking as git submodules (2026-11-22)

Commit: `1df88fc` "CMakeLists.txt: use submodules for fmt and
cmake-git-version-tracking", on `dev`, pushed (`9d764ab..1df88fc`).
Platform tested: macOS only.

## 1. Before

- `r8brain-free-src`: git submodule (7.7, `9ca64da`), header-only.
- `{fmt}`: CMake FetchContent, `GIT_TAG` `208219d` (12.2.1).
- `cmake-git-version-tracking` (jj1bdx fork): FetchContent, `GIT_TAG`
  `6c0cb87`.
- FetchContent downloaded both into `build/_deps/` at configure time.

## 2. Findings

- `FetchContent_MakeAvailable` runs `add_subdirectory` on the
  downloaded copy, so `add_subdirectory` on a submodule builds the same
  targets.
- cmake-git-version-tracking reads git state from `CMAKE_SOURCE_DIR`
  (`git_watcher.cmake`), so it keeps reporting the airspy-fmradion
  commit, not the submodule commit.
- fmt with `FMT_MASTER_PROJECT` off builds only the static library (no
  tests, docs, or install rules).
- fmt's `project(FMT)` sets `FMT_SOURCE_DIR`, not `fmt_SOURCE_DIR`.
  `sfmbase` does not link `fmt::fmt` and gets the fmt headers only from
  `include_directories`, so that path has to change.
- No CI: `.github/` holds only `FUNDING.yml`.

## 3. Trade-offs

Gains:

- No network access at configure time.
- All three pins in `git submodule status`; pin changes show as
  gitlink diffs.
- One mechanism for all dependencies.
- The `FETCHCONTENT_SOURCE_DIR_*` override, which ignores `GIT_TAG`,
  is no longer involved.

Costs:

- Clone size: fmt's full history is about 19 MB (r8brain: 33 MB).
- Existing clones need `git submodule update --init --recursive` and a
  new build directory after pulling the change.
- A submodule that is not at its pin shows as a change in the
  superproject, so the binary reports "with uncommitted changes".
- GitHub source archives do not contain submodules, so fmt and
  cmake-git-version-tracking are missing from them, as r8brain already
  was.

## 4. Plan

Decisions by the user: submodules at the repository root, full clones
(not shallow).

1. `git submodule add` for `fmt` and `cmake-git-version-tracking`,
   checked out at the existing pins.
2. `CMakeLists.txt`: replace FetchContent with `add_subdirectory`, stop
   with an error when a submodule is missing, change the fmt include
   path.
3. Add a submodule status check: a warning at configure time and a
   `check_submodules` target that fails.
4. Verify in a scratchpad build directory, including a fresh clone.
5. Update README.md, CHANGES.md, CLAUDE.md, and the build skill.

## 5. Changes

- `.gitmodules`: `fmt` (https://github.com/fmtlib/fmt) and
  `cmake-git-version-tracking`
  (https://github.com/jj1bdx/cmake-git-version-tracking.git).
- Gitlinks: `fmt` at `208219d` (12.2.1), `cmake-git-version-tracking`
  at `6c0cb87`.
- `CMakeLists.txt`:
  - `FATAL_ERROR` when `r8brain-free-src/CDSPResampler.h`,
    `fmt/CMakeLists.txt`, or `cmake-git-version-tracking/CMakeLists.txt`
    is missing, with the message to run `git submodule update --init
    --recursive`.
  - When Git is found and `.git` exists: the configure-time check and
    the `check_submodules` target.
  - `add_subdirectory(cmake-git-version-tracking)` and
    `add_subdirectory(fmt)`; `include(FetchContent)` removed.
  - fmt include path `${CMAKE_SOURCE_DIR}/fmt/include`.
- `cmake/CheckSubmodules.cmake` (new, script mode; inputs
  `GIT_EXECUTABLE`, `SOURCE_DIR`, `SEVERITY`, default `FATAL_ERROR`):

  | `git submodule status` prefix | Report |
  |---|---|
  | `-` | not initialized |
  | `+` | checked-out commit and pinned commit (from `git ls-files --stage`) |
  | `U` | merge conflict |
  | space | local changes, from `git -C <path> status --porcelain --untracked-files=no` |

  Configure runs it with `SEVERITY=WARNING`. `check_submodules` runs it
  with the default and is not part of `all`, so a build with a
  dependency checked out at an untested version still works.
- Docs:
  - README.md: one list of the three submodules,
    `check_submodules` described, unused `brew install fmt` removed.
  - CHANGES.md: "Git submodules required" lists all three; the
    "External version control code required" section is removed; new
    "dev 20261122" entry.
  - CLAUDE.md: `build` skill summary.
  - `.claude/skills/build/SKILL.md`: submodule check section,
    dependency section rewritten, stale `libr8b` line fixed.
- Both CMake files formatted with `cmake-format -i`.

## 6. Results

All decodes: `-m fm -t filesource -c
filename=test-files/piano_iqtest.wav -q -G <out>.wav`, compared with
`cmp` to the output of a FetchContent build of `9d764ab` (fmt 12.2.1).

| Test | Result |
|---|---|
| Scratchpad build from scratch | No warnings, no `_deps/`; `-I.../fmt/include` and `-isystem .../cmake-git-version-tracking` in `compile_commands.json` |
| `-h` | Reports the airspy-fmradion commit and branch |
| FM decode | Byte-identical |
| `check_submodules` at the pins | Passes |
| `git -C fmt checkout 12.2.0` | Fails; names both commits. Configure warns |
| Appended line in `fmt/README.md` | Fails; lists ` M README.md` |
| After restoring | Passes |
| Fresh clone without `--recursive` | Configure stops: "r8brain-free-src/CDSPResampler.h not found. Run: git submodule update --init --recursive" |
| Same clone after `git submodule update --init --recursive` | Configure, build, and `check_submodules` pass; `-h` shows no uncommitted changes; FM decode byte-identical |
| `build/` cleared and rebuilt at `1df88fc` | No warnings, no `_deps/`; `check_submodules` passes; FM decode byte-identical |

The fresh clone was made from a throwaway clone with a test commit,
because the change was not yet committed.

Bugs found in the first test run, fixed before the commit:

- The `+` report printed `160000` (the gitlink mode, the first field of
  `git ls-files --stage`) as the pinned commit.
- The hint suggested `git submodule update` for local changes, which
  does not discard them.

## 7. Updating a dependency

1. `git -C fmt checkout <tag>` (or `cmake-git-version-tracking`).
2. Build and test. Configure warns, and `check_submodules` fails, until
   the new pin is committed.
3. `git add fmt` and commit.

## 8. Open

- Linux builds (Ubuntu, Raspberry Pi OS). Existing clones there need
  `git submodule update --init --recursive` and a new build directory.
