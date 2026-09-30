# Comment truncation review

Date: 2026-09-29
Base commit: `1fb91c122355340c78eb1dbf523598eef0e565f4` (`1fb91c1`, branch `dev`)
Scope: `include/*.h`, `sfmbase/*.cpp`, `main.cpp`.
Status: applied to `dev` on 2026-09-30 in the commit that adds this report.

## Purpose and scope

Review every comment block longer than three lines and propose a summary of
three lines or fewer, in the style of `bce967b` and `a19ba7b`: where the
reasoning is already recorded in a `doc/*.md` file, keep the facts a
maintainer needs and add a `See doc/XXX.md.` pointer. The change is comment-only.

In scope: git-tracked `include/*.h`, `sfmbase/*.cpp`, and `main.cpp` (45 files
have at least one qualifying block). Out of scope: editor backups (`*~`),
subdirectories such as `sfmbase/txt-*`, `udev-config/`, `volk_config_data/`.

## Counting criteria

A block is one contiguous comment: consecutive `//` (or `///`) lines,
including blank `//` lines; one `/* ... */` block; or a trailing comment
continued onto following `//` lines (as in the `MultipathFilter`
constructor initializer list). A block counts when it spans **4 or more lines**
in the base. No `/* */` blocks other than file-header licenses exist in scope.

## Totals

| Category | Blocks |
| --- | ---: |
| Blocks over three lines found (base) | 121 |
| Excluded: license/copyright notices | 46 |
| Excluded: commented-out code (or code mixed with prose) | 4 |
| Left unchanged: reference citations and upstream-copied text | 6 |
| Summarized (edited) | 65 |
| of which now 3 lines or fewer | 60 |
| of which shortened but still 4 lines | 5 |

121 = 46 + 4 + 6 + 65. The diff changes 31 files, +171/-390 lines.

## Every block over three lines that was summarized

Line numbers are in the base (`1fb91c1`). "Orig" and "New" are line counts
of the comment block.

| Block (base) | Orig | New | What was dropped or where it lives |
| --- | ---: | ---: | --- |
| `include/AfSimpleAgc.h:38` | 5 | 2 | Constructor parameter list folded into prose. |
| `include/AirspyHFSource.h:68` | 9 | 3 | Repaired garbled parameter text; kept attenuation levels (0 = AGC on, 1-8 = 6-48 dB). |
| `include/AirspySource.h:67` | 12 | 3 | Kept gain ranges (LNA 0-14, mixer/VGA 0-15 dB); dropped `::` layout. |
| `include/AmDecode.h:42` | 4 | 2 | Parameter list folded into prose. |
| `include/AudioOutput.h:36` | 4 | 1 | Return-value wording merged into one line. |
| `include/AudioOutput.h:74` | 6 | 3 | Parameter list folded; kept `"-"` = stdout. |
| `include/AudioOutput.h:104` | 7 | 3 | Kept measured latencies and 40 ms floor; dropped attribution; pointer to doc/LATENCY_OPTION_20260715.md. |
| `include/AudioOutput.h:113` | 11 | 3 | Kept negative = no override and floor rule; parameter list folded. |
| `include/AudioResampler.h:32` | 9 | 3 | Kept 15%/20.4 kHz/120 dB and 2.1 vs 34.4 ms; dropped pilot-cut explanation. Doc: doc/LATENCY_PLAN_20260713.md. |
| `include/ConfigParser.h:45` | 4 | 2 | Duplicate of the .cpp comment; folded to two lines. |
| `include/DataBuffer.h:33` | 7 | 3 | Kept 512 MiB worst case; rationale lives in doc/FIXES_CLAUDE_20260610.md (X7). |
| `include/DataBuffer.h:45` | 4 | 2 | Reworded; same facts. |
| `include/DataBuffer.h:98` | 4 | 2 | Reworded; same facts. |
| `include/FileSource.h:82` | 12 | 3 | Kept format list and the raw requirement; dropped obvious parameter names. |
| `include/FileSource.h:101` | 4 | 2 | Reworded; same facts. |
| `include/Filter.h:28` | 6 | 2 | Parameter list folded; blank `//` lines removed. |
| `include/Filter.h:40` | 4 | 3 | Dropped index algebra; now in doc/LPF_VOLK_20260725.md (pointer fixed: it said Filter.cpp). |
| `include/Filter.h:56` | 5 | 2 | Parameter list folded. |
| `include/Filter.h:79` | 5 | 2 | Kept H(z) formula; dropped filler. |
| `include/Filter.h:99` | 5 | 2 | Kept time-constant formula. |
| `include/Filter.h:130` | 6 | 2 | Kept H(z) formula on one line. |
| `include/Filter.h:154` | 6 | 2 | Kept valid range 0.0 .. 0.5. |
| `include/FineTuner.h:28` | 6 | 3 | Kept shift formula. |
| `include/FineTuner.h:37` | 4 | 3 | Kept phase-continuity guarantee and shift formula. |
| `include/FmDecode.h:49` | 14 | 4 | Parameter list condensed; kept units and the meaning of pilot_shift. Still 4 lines (see below). |
| `include/FmDecode.h:66` | 9 | 3 | Kept output layout and const-reference note; pointer to doc/DECODER_ARGS_20260928.md. |
| `include/IfResampler.h:32` | 10 | 3 | Kept 10%/172.8 kHz/140 dB and 0.7 vs 6.2 ms. Doc: doc/LATENCY_PLAN_20260713.md. |
| `include/IfSimpleAgc.h:39` | 5 | 2 | Parameter list folded. |
| `include/MultipathFilter.h:34` | 4 | 2 | Reworded; same facts. |
| `include/MultipathFilter.h:40` | 10 | 4 | Kept mu ~ alpha/order, CMA-not-LMS pitfall, alpha = 1.0 divergence; doc §15. Still 4 lines. |
| `include/MultipathFilter.h:69` | 7 | 3 | Kept limit 10 and headroom rationale; doc §16.2. |
| `include/MultipathFilter.h:91` | 5 | 3 | Return-value semantics kept. |
| `include/NbfmDecode.h:42` | 4 | 2 | Parameter list folded. |
| `include/PhaseDiscriminator.h:28` | 4 | 2 | Parameter list folded. |
| `include/PilotPhaseLock.h:46` | 5 | 2 | Parameter list folded. |
| `include/PilotPhaseLock.h:53` | 6 | 3 | Kept cos/sin pilot_shift meaning. |
| `include/RtlSdrSource.h:66` | 10 | 3 | Kept units (0.1 dB, INT_MIN = auto). |
| `include/Utility.h:154` | 7 | 3 | Kept purpose and the atan2 origin of the NaNs. |
| `main.cpp:316` | 4 | 2 | Reworded; same facts. |
| `main.cpp:774` | 5 | 3 | Kept 64x bound; details in doc/FIXES_CLAUDE_20260610.md (X2). |
| `sfmbase/AirspySource.cpp:43` | 4 | 2 | Reworded; same facts. |
| `sfmbase/AudioOutput.cpp:221` | 4 | 2 | Kept the verbatim-use rule and [1, 40] ms range check. |
| `sfmbase/ConfigParser.cpp:47` | 4 | 2 | Same as ConfigParser.h. |
| `sfmbase/FileSource.cpp:250` | 7 | 3 | Kept 65536 cap and 6.5536 MHz case; arithmetic in doc/FIXES_CLAUDE_20260610.md (X1). |
| `sfmbase/FileSource.cpp:515` | 4 | 3 | Reworded; same facts. |
| `sfmbase/Filter.cpp:40` | 11 | 3 | Index-algebra derivation dropped, kept the resulting formula; derivation in doc/LPF_VOLK_20260725.md. |
| `sfmbase/Filter.cpp:160` | 6 | 3 | Lossless reflow; both formulas kept. |
| `sfmbase/Filter.cpp:252` | 6 | 3 | Formulas kept; corrected garbled note (`p1*2`, `z4`) to `p1*p2`, `p2 = conj(p1)`. |
| `sfmbase/FineTuner.cpp:33` | 5 | 3 | Fixed typo ("shigt"); same facts. |
| `sfmbase/FmDecode.cpp:121` | 4 | 3 | Reworded; same facts. |
| `sfmbase/FmDecode.cpp:178` | 4 | 2 | Kept the MUST-run-before-lock invariant. |
| `sfmbase/MultipathFilter.cpp:49` | 6 | 3 | Kept 4 taps/stage, 10.4 us, 3:1 split; stage-count advice condensed. |
| `sfmbase/MultipathFilter.cpp:65` | 6 | 3 | Kept clamp rationale; doc §15. |
| `sfmbase/MultipathFilter.cpp:76` | 7 | 3 | Kept +-10% figure; garbled bullets folded. |
| `sfmbase/MultipathFilter.cpp:126` | 4 | 2 | Reworded; same facts. |
| `sfmbase/MultipathFilter.cpp:168` | 4 | 3 | Kept resync mechanism. |
| `sfmbase/MultipathFilter.cpp:217` | 5 | 3 | Kept dummy stage count reason; doc/MF_DEBUG_CODE_20260726.md. |
| `sfmbase/MultipathFilter.cpp:244` | 8 | 4 | Kept the -ffast-math/NaN warning verbatim in substance and the NaN-rejection note; doc §16.2. Still 4 lines. |
| `sfmbase/PilotPhaseLock.cpp:49` | 5 | 4 | Kept corner frequencies, DC gain, image rejection, zeta, and the use-once caution; widening history in the docs. Still 4 lines. |
| `sfmbase/PilotPhaseLock.cpp:110` | 6 | 3 | Kept +-0.02 rad; atan2 analysis in the two doc pointers. |
| `sfmbase/PilotPhaseLock.cpp:122` | 4 | 2 | Kept integrator/pole structure. |
| `sfmbase/RtlSdrSource.cpp:243` | 7 | 4 | Kept 65536 cap and ~27963 figure; doc (X1). Still 4 lines. |
| `sfmbase/RtlSdrSource.cpp:337` | 4 | 3 | Kept deadlock rationale; doc/RTL_READ_ASYNC_20260713.md. |
| `sfmbase/RtlSdrSource.cpp:367` | 4 | 3 | Kept push_end rationale; doc/RTL_READ_ASYNC_20260713.md. |
| `sfmbase/RtlSdrSource.cpp:387` | 6 | 3 | Kept consumer-wakeup and benign short-buffer notes. |

## Blocks intentionally left long

Summarized but still four lines (all keep information not recorded in a
form that fits three lines, or need a doc pointer that alone takes most of a
line at the local indent):

- `include/FmDecode.h:49` (14 to 4): eight constructor parameters with units and
  the `pilot_shift` meaning; no doc lists them.
- `include/MultipathFilter.h:40` (10 to 4): step-size law, the
  CMA-not-LMS pitfall (the classic `0 < alpha < 2` bound does not apply), and the
  measured divergence at alpha = 1.0, plus the doc §15 pointer.
- `sfmbase/MultipathFilter.cpp:244` (8 to 4): the `-ffast-math` / NaN warning
  is a critical project constraint (CLAUDE.md), so it is kept prominent; the
  divergence-limit rationale is condensed with a §16.2 pointer.
- `sfmbase/PilotPhaseLock.cpp:49` (5 to 4): loop-filter design values (real
  corners ~40/188 Hz, unity DC gain, ~105 dB image rejection, zeta ~= 0.71) and
  the use-once caution are needed to regenerate the filter.
- `sfmbase/RtlSdrSource.cpp:243` (7 to 4): the 65536 cap and the ~27963-sample
  bound are quoted in the fixes doc only as prose; kept with the (X1) pointer.

Left unchanged (not license text, but not prose to shorten):

- `include/AfSimpleAgc.h:24` and `include/IfSimpleAgc.h:24` (6 lines each):
  the citation of Tisserand and Berviller (Electronics Letters, 2016, HAL
  URL). A reference; needed as is. The `ff...ff` characters are
  ligature-conversion artifacts in the DOI line, left byte-identical.
- `sfmbase/MultipathFilter.cpp:19` and `:25` (5 lines each): the Treichler
  and Agee, and Mochizuki and Hatori, literature references. Needed as is.
- `include/git.h:21` (4 lines) and `:61` (6 lines): Doxygen text copied
  verbatim from upstream cmake-git-version-tracking (MIT). Editing would
  make future re-syncs with upstream noisy; the maintainer may decide otherwise.

## Excluded: license and copyright notices (46)

Left byte-identical.

- File-header GPL/copyright blocks (45), the first 17 to 19 lines of:
  `include/` `AfSimpleAgc.h` `AirspyHFSource.h` `AirspySource.h` `AmDecode.h`
  `AudioOutput.h` `AudioResampler.h` `ConfigParser.h` `DataBuffer.h`
  `FileSource.h` `Filter.h` `FilterParameters.h` `FineTuner.h` `FmDecode.h`
  `FourthConverterIQ.h` `IfResampler.h` `IfSimpleAgc.h` `MovingAverage.h`
  `MultipathFilter.h` `NbfmDecode.h` `PhaseDiscriminator.h` `PilotPhaseLock.h`
  `RtlSdrSource.h` `SoftFM.h` `Source.h` `Utility.h`; `main.cpp`;
  `sfmbase/` `AfSimpleAgc.cpp` `AirspyHFSource.cpp` `AirspySource.cpp`
  `AmDecode.cpp` `AudioOutput.cpp` `AudioResampler.cpp` `ConfigParser.cpp`
  `FileSource.cpp` `Filter.cpp` `FilterParameters.cpp` `FineTuner.cpp`
  `FmDecode.cpp` `IfResampler.cpp` `IfSimpleAgc.cpp` `MultipathFilter.cpp`
  `NbfmDecode.cpp` `PhaseDiscriminator.cpp` `PilotPhaseLock.cpp`
  `RtlSdrSource.cpp`.
- `include/git.h:2-6` (5 lines): upstream URL and "Released under the MIT
  License" notice.

## Commented-out code (not touched; maintainer decision)

- `include/ConfigParser.h:52-70` (19 lines): a complete `main()` test
  example for `ConfigParser`. It could move to `doc/` or a unit test.
- `sfmbase/FmDecode.cpp:238-241` (4 lines): the plain C++ loop equivalent
  to the following libvolk call.
- `sfmbase/MultipathFilter.cpp:152-155` (4 lines): the loop equivalent to the
  VOLK dot product.
- `sfmbase/MultipathFilter.cpp:186-195` (10 lines): mixed block; the
  equivalent-loop code is interleaved with two prose notes (VOLK
  overlap check; API change from VOLK 3.1.0). Left whole so the code
  lines stay untouched. The notes could be split out and shortened.

## Verification

1. **Comment-only proof.** For every changed file (31), the base version
   (`git show 1fb91c1:<file>`) and the edited version were passed through a
   Python state-machine stripper that tracks string, character, and raw-string
   literals and removes `//` and `/* */` comments. The results were split on
   whitespace and compared token by token. Result: **identical token streams
   in all 31 files** (0 failures). Unchanged files are untouched by
   construction (`git diff --stat` lists only these 31).
2. **Formatting.** `clang-format` (project `.clang-format`, column limit 80)
   was applied to the base and edited version of each changed file; the
   difference sets were compared. Both are empty: the base is already
   clang-format clean and the edits introduce **0 new formatting
   differences**. All new comment lines are at most 80 columns.
3. **Build.** `cmake -S . -B build-comment -DCMAKE_BUILD_TYPE=Release` then
   `cmake --build build-comment --target all` in the worktree: success. The
   only warning is the pre-existing
   `sfmbase/IfResampler.cpp:55:28: variable 'output_length_im' set but not used`,
   an unedited file, and the same warning `doc/DECODER_ARGS_20260928.md`
   recorded at the base. As a base-versus-edited check, every `main.cpp` and
   `sfmbase/*.cpp` file (20) was also compiled with `-fsyntax-only -Wall
   -Wextra -Wcomment` in both an extracted copy of `1fb91c1` and the worktree:
   the sets of warnings (line numbers normalized) are identical in all 20.
   Build flags were not touched; `-ffast-math` was not added.
4. **Patch applies.** `patch -p1 --dry-run` of the diff on a clean extract of
   `1fb91c1` succeeds for all 31 files.
5. **Applied to `dev`.** On 2026-09-30 the diff was applied with `git apply`
   on `1fb91c1` (31 files, +171/-390). `clang-format --dry-run` reports
   nothing on the changed files, and an incremental build of `build/` links
   `airspy-fmradion` with no warnings.

No decode runs were made (comment-only change).

## Notes for the maintainer

- Two corrections rode along with the shortening, both in comments only:
  `include/Filter.h` no longer points at "the index algebra in `Filter.cpp`"
  (it now lives in `doc/LPF_VOLK_20260725.md`); `sfmbase/Filter.cpp:252`
  replaces the garbled `p1*2 == abs(p1*p1), z4 = conj(z1)` with
  `p1*p2 == abs(p1*p1)` under `p2 = conj(p1)`; `sfmbase/FineTuner.cpp:33`
  fixes a typo ("shigt").
- `doc/DECODER_ARGS_20260928.md` describes the `FmDecoder::process` change as
  proposed; `1fb91c1` already applies it, so the `FmDecode.h` comment cites
  that doc as the rationale.
- The `-L` latency values in `include/AudioOutput.h` are kept exactly
  (0.014717, 0.034830); the "Kenji's experiments" attribution was dropped.
- Base commit: `1fb91c1`.
