# `FmDecoder::process` parameter type: why it differs from the other two decoders, and a proposed fix

Date: 2026-09-28
Base commit: `a19ba7b77d8f76e4974030c25794832760a63e8f` (branch `dev`)
Scope: `include/FmDecode.h`, `sfmbase/FmDecode.cpp`. **Proposed only — not
applied to `dev`.** See `work/decoder-args-20260928.diff`.

## Background

The three decoder entry points have different parameter conventions for
their main `process()` method:

| Decoder | Signature (at `a19ba7b`) |
| --- | --- |
| `AmDecoder::process` (`include/AmDecode.h:51`) | `void process(const IQSampleVector &samples_in, SampleVector &audio);` |
| `NbfmDecoder::process` (`include/NbfmDecode.h:49`) | `void process(const IQSampleVector &samples_in, SampleVector &audio);` |
| `FmDecoder::process` (`include/FmDecode.h:75`) | `void process(IQSampleVector samples_in, SampleVector &audio);` (by value, non-const) |

`FmDecoder::process` is the odd one out. This report traces why, checks
whether the original reason for the by-value parameter still holds, and
proposes changing it to `const IQSampleVector &` like the other two.

## Why `FmDecoder::process` takes its input by value

### The V41 change (commit `29fa28b`, 2026-05-04)

`doc/FIXES_CLAUDE_20260504.md`, item V41, changed **both**
`FmDecoder::process` and `AmDecoder::process` from `const IQSampleVector &`
to by-value `IQSampleVector`. The stated reason:

> Before the fix, `void FmDecoder::process(const IQSampleVector &samples_in, ...)`
> had `m_samples_in_iffiltered = std::move(samples_in);` inside the body.
> `std::move` on a `const &` produces a `const &&`, which the
> `vector::operator=(vector&&)` overload cannot bind to, so
> `vector::operator=(const vector&)` was selected and the operation
> silently degraded to a copy.

This is correct C++: `std::move` on a `const` object yields a `const&&`,
which cannot bind to `vector&&` (the move-assignment parameter), so overload
resolution falls back to `vector&` "copy" (in `AmDecode.cpp`) — actually to
`const vector&` (copy-assignment). Changing the parameter to by-value
(`IQSampleVector samples_in`, no `const`) makes `std::move(samples_in)`
inside the body resolve to a genuine `IQSampleVector&&`, so the intended
move actually happens. Both decoders' no-filter branches had exactly this
pattern:

- `FmDecode.cpp` (no-IF-filter branch): `m_samples_in_iffiltered = std::move(samples_in);`
- `AmDecode.cpp` (two `default:` branches, both dead code — see below):
  `m_buf_filtered2 = std::move(samples_in);`

Verified directly with `git log -p -S"IQSampleVector &samples_in"` on both
files: `29fa28b` is exactly the commit that flipped both signatures from
`const &` to by-value, with the diff matching this description.

### The AmDecoder precedent (commit `7a73be7`, 2026-09-23)

`AmDecoder::process` was later changed back to `const IQSampleVector &` in
commit `7a73be7` ("Apply C++ const, reference, and std::move cleanup"),
following the review in `doc/CPP_OBJECTS_20260923.md`. The commit message
states the reason directly:

> `AmDecoder::process` takes `const IQSampleVector&`, removing a copy of
> every AM-mode IF block; the by-value parameter only served moves in
> unreachable `default:` branches.

Confirmed by reading `sfmbase/AmDecode.cpp` at `a19ba7b`: the only two
`std::move(samples_in)` sites in `AmDecoder::process` are both in
`default:` cases of `switch` statements over `m_mode` — one inside a `switch
(m_mode)` nested under `case ModType::USB/LSB/CW/WSPR`, guarded by
`m_mode` being fixed at construction to one of exactly those four values
(so its own `default:` is unreachable), and one in the outer `switch
(m_mode)`'s `default:`, unreachable because the only call site
(`main.cpp`) only ever constructs an `AmDecoder` for
AM/DSB/USB/LSB/CW/WSPR and never reaches `default`. Both are now plain
`m_buf_filtered2 = samples_in;` (copy) rather than move, since dead code
performing a move added no value, and the same commit's diff shows exactly
that change. So for `AmDecoder`, the by-value parameter was pure cost
(one guaranteed copy per block, for every mode) buying a "move" that could
never execute. Reverting to `const &` was a straightforward win, and
`7a73be7`'s own bit-exactness testing (FM default, `-E 36`, `-X`, `-f
narrow`, AM, NBFM against the same test file used here) confirmed no
behavior change.

`7a73be7` did **not** apply the same change to `FmDecoder::process`,
because `FmDecoder`'s no-IF-filter branch move
(`m_samples_in_iffiltered = std::move(samples_in);`) is **not** dead code —
it executes on every block whenever `m_fmfilter_enable` is false (the
`FilterType::Default` and `FilterType::Wide` cases, both selected via `-f`;
see "Which CLI options exercise which branch" below). So at that point the
by-value parameter was still serving a real, reachable move, unlike
`AmDecoder`'s. `7a73be7` instead reworked a different pattern inside
`FmDecoder::process` — the final-line moves into `m_samples_in_multipathfiltered`
became `std::swap`s, for the buffer-capacity reasons documented in
`doc/CPP_OBJECTS_20260923.md` §3.4 — leaving the `samples_in` parameter
type itself untouched.

### The call site copies `if_samples` into the by-value parameter every block

`main.cpp:1007` calls `fm.process(if_samples, audiosamples);` with
`if_samples` as an lvalue. Since `48b2acd` ("Hoist main loop buffers and
swap decoder output", 2026-09-24, documented in
`doc/MAIN_CPP_SWAP_20260924.md`), `if_samples` is declared once above the
main loop (`main.cpp:925`) and persists across iterations specifically so
it keeps a stable heap allocation instead of being reallocated every block.

Passing an lvalue to a by-value parameter invokes the copy constructor, not
the move constructor — `fm.process(if_samples, audiosamples)` copies the
entire IF block into `FmDecoder::process`'s `samples_in` parameter on
**every single call**, regardless of `m_fmfilter_enable`. This means the
`std::move(samples_in)` inside the no-IF-filter branch, which V41 fixed to
be a genuine move, only ever moves a copy that was just made at the call
boundary — the net allocation cost for that branch is unchanged from before
V41 (one copy either way), and the `m_fmfilter_enable == true` branch pays
the same by-value copy for no benefit at all (its branch never moves
`samples_in`). The `doc/MAIN_CPP_SWAP_20260924.md` coverage table doesn't
count this cost separately because it was outside that report's scope
(that report only asked whether `main.cpp`'s own buffers should be
hoisted, not whether the *decoder* parameter types were efficient); it
does note in passing that `if_samples = std::move(if_shifted_samples)`
(`main.cpp:972`) is itself a pure ownership transfer, which is precisely
why the value is immediately available in `if_samples` for the *next* copy
into `fm.process`.

**Verified by measurement** (see "Allocation count" below): switching to
`const &` on this signature removes very close to one heap allocation per
processed block, in **both** `m_fmfilter_enable` states, confirming this
call-site copy is a real, per-block, unconditional cost — not something
confined to the no-filter path.

### Would `std::move(if_samples)` at the call site help instead?

No. `if_samples` is written at the top of every loop iteration (either via
`if_resampler.process(if_shifted_samples, if_samples)` or
`if_samples = std::move(if_shifted_samples)`, `main.cpp:970,972`) and then
read once, at the `fm.process(...)` call (`main.cpp:1007`) later in the
*same* iteration; it is not read again until the next iteration overwrites
it. So moving it into `fm.process` would not read stale data. But
`std::move(if_samples)` here would still bind to the by-value parameter via
the *move* constructor, which **transfers** `if_samples`'s heap allocation
into the parameter and leaves `if_samples` in a valid-but-unspecified state
(in practice, empty, for `libc++`/`libstdc++`'s `vector` move
constructor). The next iteration's `if_samples.resize(...)`-based write (via
`IfResampler::process` or the plain `std::move(if_shifted_samples)`
assignment) would then have to reallocate, because the capacity that
`48b2acd`'s hoist was specifically trying to keep would have just been
given away. This would defeat the entire point of the `main.cpp` hoist
documented in `doc/MAIN_CPP_SWAP_20260924.md` — trading the call-site copy
for a call-site move only relocates the same reallocation forward by one
line, at the cost of adding a `main.cpp` change that "gives away" a
loop-local buffer's allocation. The correct fix instead changes the
*decoder's* parameter to `const &`, so the call performs neither a copy nor
a move: `if_samples` is only read, never taken from, at this call.

### Which CLI options exercise which branch of `m_fmfilter_enable`

From `main.cpp:827-855`, `m_fmfilter_enable` is set once, at
`FmDecoder` construction, from the `-f`/`--filtertype` option
(`filtertype_str`, default `"default"`):

| `-f` value | `fmfilter_enable` | Coefficients |
| --- | --- | --- |
| `default` (no `-f`, or `-f default`) | `false` | n/a (delay-only passthrough) |
| `wide` | `false` | n/a (delay-only passthrough) |
| `medium` | `true` | `jj1bdx_fm_384kHz_medium` |
| `narrow` | `true` | `jj1bdx_fm_384kHz_narrow` |

So the FM default test case (no `-f`) exercises `m_fmfilter_enable == false`
(the historical no-filter/move branch), and `-f narrow` exercises
`m_fmfilter_enable == true` (the `m_fmfilter.process(...)` branch). Both
are in the test matrix below; `-E 36` and `-X` don't touch
`m_fmfilter_enable` at all (they're independent options — multipath filter
stage count and pilot-phase shift, respectively) and were kept in the
matrix as a bit-exactness cross-check on other parts of `FmDecoder::process`
that aren't touched by this fix.

## Proposed fix

Change `FmDecoder::process` to take `const IQSampleVector &samples_in`,
and inside the body select which buffer feeds the RMS measurement and
`m_ifagc.process(...)` via a pointer, rather than copying or moving into
`m_samples_in_iffiltered` on the no-filter path:

```cpp
// include/FmDecode.h
  // samples_in is taken by const reference: the no-IF-filter path selects
  // it directly instead of copying into m_samples_in_iffiltered.
  void process(const IQSampleVector &samples_in, SampleVector &audio);
```

```cpp
// sfmbase/FmDecode.cpp
void FmDecoder::process(const IQSampleVector &samples_in, SampleVector &audio) {

  // If no sampled baseband signal comes out,
  // terminate and wait for next block,
  if (samples_in.size() == 0) {
    audio.resize(0);
    return;
  }

  // Apply IF filter if enabled; otherwise use samples_in directly, with
  // no copy into m_samples_in_iffiltered.
  const IQSampleVector *if_filtered = &samples_in;
  if (m_fmfilter_enable) {
    m_fmfilter.process(samples_in, m_samples_in_iffiltered);
    if_filtered = &m_samples_in_iffiltered;
  }

  // Measure IF RMS level.
  m_if_rms = Utility::rms_level_sample(*if_filtered);

  // Perform IF AGC.
  m_ifagc.process(*if_filtered, m_samples_in_after_agc);

  if (m_wait_multipath_blocks > 0) {
    ... // unchanged
```

Nothing past this point in `process()` reads `samples_in` or
`m_samples_in_iffiltered` again (the next consumer is
`m_ifagc.process(*if_filtered, m_samples_in_after_agc)`, whose output,
`m_samples_in_after_agc`, is what the rest of the function actually uses).

### Every consumer already takes its input by `const &`

Confirmed by reading the signatures directly:

- `Utility::rms_level_sample(const IQSampleVector &samples)` (`include/Utility.h:95`)
- `IfSimpleAgc::process(const IQSampleVector &samples_in, IQSampleVector &samples_out)` (`include/IfSimpleAgc.h:50`)
- `LowPassFilterFirIQ::process(const IQSampleVector &samples_in, IQSampleVector &samples_out)` (`include/Filter.h:37`)

So a `const IQSampleVector *` (or equivalently a `const IQSampleVector &`
rebound via a small helper — a raw pointer was chosen here because C++
references cannot be reseated, and the branch needs to point at one of two
different objects depending on `m_fmfilter_enable`) satisfies every
call in this stretch of the function without copying anything.

### `m_samples_in_iffiltered` is still needed, but only for the filtered path

Grepped every use of `m_samples_in_iffiltered` in the tree
(`include/FmDecode.h`, `sfmbase/FmDecode.cpp`, `main.cpp`): the member is
declared once (`FmDecode.h:140`) and used at exactly the four sites this
fix touches. With the fix, `m_samples_in_iffiltered` is written only when
`m_fmfilter_enable` is true (by `m_fmfilter.process(...)`, which fully
`resize()`s and overwrites it — see `sfmbase/Filter.cpp`'s
`LowPassFilterFirIQ::process`) and is never touched when
`m_fmfilter_enable` is false. Since `m_fmfilter_enable` is `const` and
fixed for the lifetime of a given `FmDecoder` instance (set once at
construction from the `-f` option), an instance built with
`m_fmfilter_enable == false` simply never uses this member again — it sits
at whatever size it last held (empty, for a freshly-constructed instance,
since it's default-initialized) but nothing reads it. This mirrors exactly
how `AmDecoder`'s `m_buf_filtered1a`/`m_buf_filtered1b` members are unused
outside their relevant `m_mode` branches. Removing the member entirely was
considered and rejected — see "Alternatives" below.

## Alternatives considered / rejected

1. **Keep by-value, add `std::move(if_samples)` at the call site.**
   Rejected: as analyzed above, this "moves" a persistent loop buffer's
   allocation into the decoder every block, defeating the `main.cpp` hoist
   from `doc/MAIN_CPP_SWAP_20260924.md` and reintroducing a reallocation on
   the *next* iteration instead of removing one. It would also apply to
   `am.process`/`nbfm.process` call sites inconsistently (those decoders
   already take `const &`, so `std::move` there wouldn't compile against a
   const-ref parameter, or would be a silent no-op/warning against a
   pass-by-value one) — asymmetric, fragile, and against the direction
   `7a73be7` already took for `AmDecoder`.

2. **Keep by-value, but branch on whether to move or copy based on
   `m_fmfilter_enable`.** E.g., keep the by-value parameter only because
   the underlying decoder call is by-value, but that doesn't remove the
   caller-side copy at all — the copy happens at the call boundary,
   *before* the function body's `if` even runs. This alternative doesn't
   exist as a real option: by-value parameters always copy-or-move at the
   call site regardless of what the callee's body does with them
   afterward. This is exactly the point the "call site" section above
   demonstrates empirically (see the equal allocation-count reduction in
   both filter states).

3. **`const &` parameter, but keep assigning into
   `m_samples_in_iffiltered` on the no-filter path via `= samples_in;`
   (a copy).** Rejected: this recreates exactly the cost this fix removes
   (one `IQSampleVector` copy per block on the no-filter path) while
   additionally still not fixing the filter-enabled path's now-removed
   call-site copy. Strictly worse than the proposed fix, with no offsetting
   benefit — the pointer-selection approach costs nothing extra at runtime
   (a single pointer, assigned once per call) and requires no vector copy
   at all.

4. **Remove `m_samples_in_iffiltered` entirely and have
   `LowPassFilterFirIQ::process` write into a local variable, or return a
   `const IQSampleVector &` that aliases either `samples_in` or a member.**
   Rejected as unnecessary complexity: `m_samples_in_iffiltered` still
   needs to be a persistent member (not a fresh local) so its heap
   allocation survives across calls when `m_fmfilter_enable` is true —
   exactly the same "hoisted buffer" reasoning as
   `doc/MAIN_CPP_SWAP_20260924.md` applies to it. Making
   `m_fmfilter.process`'s destination conditionally a local variable would
   reintroduce a fresh-allocate-every-call regression on the
   filter-enabled path, which is used by real on-air configurations
   (`-f medium`, `-f narrow`).

5. **Use a `const IQSampleVector &if_filtered` reference initialized by a
   conditional (ternary) expression instead of a pointer.** Functionally
   equivalent to the proposed fix (`const IQSampleVector &if_filtered =
   m_fmfilter_enable ? (m_fmfilter.process(samples_in, m_samples_in_iffiltered), m_samples_in_iffiltered) : samples_in;`)
   but was rejected as a style choice: burying a function call with a side
   effect (`m_fmfilter.process(...)`) inside a comma-expression inside a
   ternary inside a reference initializer is harder to read than the
   plain `if`/pointer-reassignment form, for no behavioral difference.
   The straightforward `if (m_fmfilter_enable) { ...; if_filtered = &...; }`
   was preferred for clarity, consistent with the project's general
   preference for explicit control flow over compact-but-dense
   expressions (see the existing `if`/`else` structure this replaces).

The recommended fix (selecting a `const IQSampleVector *` to either
`samples_in` or `m_samples_in_iffiltered`) was chosen over all of the
above because it is the only option that removes the call-site copy in
*both* `m_fmfilter_enable` states, requires no change to `main.cpp` (so it
doesn't touch or risk the buffer-hoisting work from
`doc/MAIN_CPP_SWAP_20260924.md`), and needs no change to
`m_samples_in_iffiltered`'s lifetime or type.

## Safety argument

1. **`samples_in` is never written to.** The fix's whole point is that
   `FmDecoder::process` no longer needs mutable access to its input; every
   remaining use of `samples_in` (the initial `.size() == 0` check, and
   passing it to `m_fmfilter.process`/via `if_filtered`) is read-only, and
   every downstream consumer (`Utility::rms_level_sample`,
   `IfSimpleAgc::process`, `LowPassFilterFirIQ::process`) already declares
   its input parameter `const &`.
2. **`if_filtered` never dangles.** It points to either `samples_in` (a
   parameter reference, valid for the whole function call) or
   `m_samples_in_iffiltered` (a class member, valid for the object's
   lifetime, definitely outliving the function call). Both are set before
   `if_filtered` is first dereferenced (`Utility::rms_level_sample(*if_filtered)`,
   the first line after the `if`), and `if_filtered` is not stored anywhere
   beyond local scope, so no lifetime issue can arise from this change.
3. **`m_samples_in_iffiltered`'s only reader (`if_filtered`, when the
   filter branch was taken) always sees a value `m_fmfilter.process` just
   fully wrote.** `LowPassFilterFirIQ::process` (`sfmbase/Filter.cpp`)
   `resize()`s its output to the input length and writes every element via
   a VOLK dot product before returning — the same "always fully
   overwritten before being read" property that
   `doc/MAIN_CPP_SWAP_20260924.md` already relied on for its swap sites.
   No path in the fixed code reads `m_samples_in_iffiltered` without it
   having just been written in the same call, and when
   `m_fmfilter_enable` is false, no path reads it at all — `if_filtered`
   points at `samples_in` in that case, so the member's stale (or empty)
   contents from a previous call, or a previous decoder configuration, are
   never observed.
4. **No aliasing hazard.** `if_filtered` and `m_samples_in_after_agc` are
   always different `IQSampleVector` objects (one is `samples_in` or
   `m_samples_in_iffiltered`, the other is a separate member never
   assigned to `m_samples_in_iffiltered` or bound to `samples_in`), so
   `m_ifagc.process(*if_filtered, m_samples_in_after_agc)` cannot pass the
   same underlying buffer as both its input and output parameter under
   this change any differently than it already could not before it.
5. **The change is confined to the top of `process()`, before the
   multipath/phase-discriminator/pilot-PLL/stereo logic.** None of that
   downstream logic reads `samples_in` or `m_samples_in_iffiltered`
   directly (it works from `m_samples_in_after_agc` onward), so this fix
   cannot interact with the `7a73be7`/`48b2acd` swap-based buffer-reuse
   changes already in that later code, beyond removing one now-unnecessary
   `std::move`.

## Test results

### Build

Baseline (`a19ba7b`, in a throwaway clone outside any tracked worktree) and
patched (this worktree's two changed files) were each built with
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` followed by
`cmake --build build --target all`, per the project's `build` skill.

Both builds completed with **zero errors**, and the same single
pre-existing, unrelated warning appears identically in both:

```
sfmbase/IfResampler.cpp:55:28: warning: variable 'output_length_im' set but not used [-Wunused-but-set-variable]
```

No new compiler warnings were introduced by this change.
`clang-format -i include/FmDecode.h sfmbase/FmDecode.cpp` was run before
building the patched binary; `git diff` shows no reformatting beyond the
touched hunks (the changed `process()` declaration line is exactly 80
columns, within the project's `ColumnLimit: 80`).

### Bit-exactness

Six file-source decodes were run against
`test-files/joakfm-20260715045930z-iq.wav` (60 s, 384 kHz, 32-bit float
stereo IQ WAV), config `freq=82500000,srate=384000,filename=<file>`,
output via `-G`, baseline and patched run concurrently (`&` + `wait`) per
case:

| Case | Command flags | `m_fmfilter_enable` exercised | `cmp` result | "overflow" in either log? |
| --- | --- | --- | --- | --- |
| FM default | `-m fm` | `false` (no-filter/selection path) | identical | no |
| FM `-E 36` | `-m fm -E 36` | `false` | identical | no |
| FM `-X` | `-m fm -X` | `false` | identical | no |
| FM `-f narrow` | `-m fm -f narrow` | `true` (filtered path) | identical | no |
| AM | `-m am` | n/a (unaffected decoder) | identical | no |
| NBFM | `-m nbfm` | n/a (unaffected decoder) | identical | no |

All six baseline/patched WAV pairs are byte-for-byte identical (`cmp` exit
code 0). None of the twelve logs (6 baseline + 6 patched) contains the
string "overflow". This confirms the fix is numerically a no-op in every
tested `FmDecoder` configuration, as expected: the change only affects
which buffer's allocation is used to satisfy the same reads, not any
computed value.

### Allocation count

Following the "scratch-only `operator new`/`operator delete` counting
shim" method from `doc/MAIN_CPP_SWAP_20260924.md`: a small
`AllocCounter.cpp` (not part of the tracked source tree) overriding the
four global `operator new[]`/`new`/`delete[]`/`delete` forms to count
calls and print the totals to `stderr` at exit, linked into two separate,
throwaway instrumented builds (`add_executable(airspy-fmradion main.cpp
AllocCounter.cpp)` added only to each scratch clone's `CMakeLists.txt` —
one clone at `a19ba7b`, one clone at `a19ba7b` with this fix's two files
overlaid). Both instrumented binaries were confirmed to produce
byte-identical WAV output to their corresponding non-instrumented builds
before trusting their counts.

| Case | Baseline `new` calls | Patched `new` calls | Reduction | Blocks processed |
| --- | ---: | ---: | ---: | ---: |
| FM default (`m_fmfilter_enable = false`) | 56,368 | 45,118 | 11,250 | ~11,250 (23,040,000 samples / 2048-sample filesource block) |
| FM `-f narrow` (`m_fmfilter_enable = true`) | 56,369 | 45,119 | 11,250 | ~11,250 |

The reduction is essentially exactly one allocation per processed block —
and, notably, **the same reduction appears in both `m_fmfilter_enable`
states.** This confirms the analysis above: the eliminated allocation is
the by-value parameter's copy of `if_samples` at the call site
(`main.cpp:1007`), which happened unconditionally on every call regardless
of which branch `FmDecoder::process`'s body took, not something confined
to the no-IF-filter path. (The 56,368 vs. 56,369 / 45,118 vs. 45,119
one-count difference between the two baseline, and two patched, rows is
FM-filter-state-dependent block-count noise at the very end of the file —
not a semantic difference in this fix.)

The FM-default baseline (56,368) matches the "after" figure in
`doc/MAIN_CPP_SWAP_20260924.md` exactly (67,616 at `7a73be7`, minus that
report's 11,248-call reduction). This report's baseline (`a19ba7b`)
therefore already includes those improvements, and the 11,250-call
reduction measured here comes on top of them; it is not a re-measurement
of the same effect.

### CPU / wall-clock time

`/usr/bin/time -l` was run twice for FM default, baseline and patched run
concurrently to control for host load:

| Rep | Metric | Baseline | Patched |
| --- | --- | --- | --- |
| 1 | user CPU (s) | 4.49 | 4.50 |
| 1 | sys CPU (s) | 0.88 | 1.07 |
| 1 | max RSS (bytes) | 9,207,808 | 9,142,272 |
| 2 | user CPU (s) | 4.39 | 4.38 |
| 2 | sys CPU (s) | 1.18 | 1.32 |
| 2 | max RSS (bytes) | 9,519,104 | 9,338,880 |

Both `fm_time1.wav` and `fm_time2.wav` pairs (baseline vs. patched) were
also confirmed byte-identical via `cmp`. Consistent with
`doc/MAIN_CPP_SWAP_20260924.md`'s own finding for a comparably small
per-block allocation-count change, user/sys CPU time and max RSS are
within the same run-to-run spread as each other — **no measurable CPU or
peak-memory benefit at this granularity.** This is stated honestly rather
than oversold: removing one ~aligned-block-sized allocation per 2048-sample
block is a real, reproducible, and mechanistically-explained reduction in
allocator traffic (see "Allocation count" above), but it is far too small
relative to the per-block DSP compute cost (filtering, resampling, AGC,
phase discrimination) to show up as a wall-clock or RSS difference with
this measurement method.

## Recommendation

**Apply the proposed fix** (`include/FmDecode.h`,
`sfmbase/FmDecode.cpp` — see `work/decoder-args-20260928.diff`). Reasoning:

- It makes `FmDecoder::process`'s signature consistent with
  `AmDecoder::process` and `NbfmDecoder::process`, both already
  `const IQSampleVector &`, removing the one remaining exception the task
  set out to investigate.
- It is behavior-preserving: six bit-exact WAV comparisons across FM
  (default, `-E 36`, `-X`, `-f narrow`), AM, and NBFM, with no queue
  overflows, using an unmodified `main.cpp` call site.
- It introduces no new compiler warnings.
- It has a clear, reproducible, mechanistically-explained benefit: roughly
  one fewer heap allocation per processed FM block (~11,250 over this
  60-second test file), in *both* `m_fmfilter_enable` states — larger in
  scope than the V41 fix's original no-filter-path-only framing suggested,
  since the eliminated cost was always at the call boundary, not inside
  either branch of the function body.
- The CPU/memory benefit is not measurable at the process level with the
  methods available here, and this report says so plainly, consistent with
  the equivalent honest conclusion in `doc/MAIN_CPP_SWAP_20260924.md` for a
  similarly-sized allocation-count change.
- The risk is low: the fix touches only the first ~20 lines of
  `FmDecoder::process`, before any of the multipath/PLL/stereo logic;
  every downstream consumer of the selected buffer already takes
  `const &`; and `main.cpp`'s call site (`fm.process(if_samples,
  audiosamples)`) needs no change at all — the existing lvalue argument
  now simply binds to a reference instead of invoking a copy constructor.

This report's fix is **proposed only**. It has not been applied to
`include/FmDecode.h` or `sfmbase/FmDecode.cpp` in the tracked worktree, has
not been committed, and has not been pushed anywhere. The verification
above was performed entirely in this agent's own worktree and in
throwaway clones/build directories outside any tracked worktree.
