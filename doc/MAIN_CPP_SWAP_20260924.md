# Hoisting `main.cpp`'s loop buffers and swapping decoder output

Date: 2026-09-24
Base commit: `7a73be75456b471ce05dfb30970dfa251ea7addb` (branch `dev`)
Scope: `main.cpp`'s main processing loop, and the final output-parameter
assignment in `FmDecoder::process`, `AmDecoder::process`, and
`NbfmDecoder::process`.

## Background

A prior review, doc/CPP_OBJECTS_20260923.md (§3.4, commit `7a73be7`),
found that `FmDecode.cpp`, `AmDecode.cpp`, and `NbfmDecode.cpp` each end
their `process()` method by moving a persistent member buffer into the
caller's output parameter, e.g. `audio = std::move(m_buf_mono);`. A
`std::vector` move assignment transfers the source's heap allocation and
discards the destination's, so this permanently empties the member buffer;
the decoder must then reallocate it from scratch on the next call. The
review recommended `std::swap` instead, which trades the two buffers
without discarding either allocation — but noted a caveat: `main.cpp`
declares its own loop-body buffers (`if_shifted_samples`, `if_samples`,
`audiosamples`) fresh, with zero capacity, on every iteration of the main
loop (`main.cpp:937-942` at `7a73be7`), so at the time of the review a
swap would have had nothing useful to trade — the destination was always
empty anyway. The review therefore left these three sites unchanged and
suggested hoisting `main.cpp`'s loop buffers out of the loop as the
prerequisite. This report implements that hoist and the three swaps, and
measures the result.

## What hoisting a buffer means

"Hoisting" here means moving a buffer's declaration from inside the loop
body to just above the loop, so one `std::vector` object lives for the
whole run instead of being constructed and destroyed on every iteration.

The relevant part of `main.cpp` looked like this at `7a73be7` (simplified):

```cpp
for (;;) {
  IQSampleVector iqsamples = source_buffer.pull();
  IQSampleVector if_shifted_samples;
  IQSampleVector if_samples;
  SampleVector audiosamples(0);
  ...
  fm.process(if_samples, audiosamples);
  audio_output->write(audiosamples);
}
// closing brace: if_shifted_samples, if_samples, and audiosamples
// are all destroyed here, freeing whatever heap memory they hold.
```

Every pass through the loop starts `if_samples` and `audiosamples` out
empty. As soon as something resizes them to hold real data, the vector
has to ask the heap allocator for memory (`new`/`malloc`). At the closing
brace, both vectors go out of scope and that memory is freed. So each
loop iteration pays for at least one allocate-and-free pair per buffer,
even though the next iteration is just going to ask for a same-sized (or
similarly sized) block again.

Hoisting moves the declarations above the loop instead:

```cpp
IQSampleVector if_shifted_samples;
IQSampleVector if_samples;
SampleVector audiosamples;
for (;;) {
  IQSampleVector iqsamples = source_buffer.pull();
  ...
  fm.process(if_samples, audiosamples);
  audio_output->write(audiosamples);
}
```

Now the same three objects persist across iterations. A `std::vector`
that is resized to a value it has already reached before does not need to
reallocate — it just reuses the memory it is already holding (`resize()`
only grows the underlying allocation, never shrinks it, unless a caller
explicitly asks it to with e.g. `shrink_to_fit()`, which nothing here
does). Since the block sizes in this pipeline are fixed by the device and
decoder configuration, after the first one or two blocks every hoisted
buffer settles into a stable capacity and stops allocating altogether.

This connects directly to the decoders. `FmDecoder::process()` (and the
AM/NBFM equivalents) end with a line like:

```cpp
audio = std::move(m_buf_mono);   // before this change
```

Move-assignment here frees whatever `audio` (the caller's buffer) held
before, and hands `audio` ownership of `m_buf_mono`'s allocation —
leaving `m_buf_mono` empty. So `m_buf_mono` must reallocate on the
decoder's next call, regardless of what `main.cpp` does. Changing this to

```cpp
std::swap(audio, m_buf_mono);   // after this change
```

trades the two buffers instead: `main.cpp` gets the finished audio block
(what it wanted), and the decoder gets back whatever `audio` used to hold
(an old, no-longer-needed audio block, but with a perfectly good
allocation) to reuse as `m_buf_mono` next time. Nothing is freed and
nothing is allocated by the swap itself.

This is exactly why swapping here would have gained nothing before this
change: if `audio` (i.e., `main.cpp`'s `audiosamples`) is a fresh, empty
vector on every single call — because `main.cpp` declares it fresh every
iteration — then swapping hands the decoder back an empty vector, which
is no better than the `std::move` it replaced. `main.cpp` has to hold
onto its buffer across iterations first, so that there is a real,
already-sized allocation for the decoder to receive back. That is the
dependency doc/CPP_OBJECTS_20260923.md §3.4 flagged and this report
resolves.

The risk this creates: a buffer obtained via `swap` still holds whatever
data was in it from two calls ago, not zeros and not "empty" in any
enforced sense — only `.size()` says how much of it is meaningful.
`std::swap` is therefore only safe at a given call site if **every** code
path that produces that buffer fully `resize()`s and overwrites it before
anything reads it, and if no path reads it while relying on it having
been cleared or on stale contents. Section "Per-buffer analysis" and
"Safety argument per swap site" below check exactly that, for every
buffer this change touches, and the bit-exact comparison in "Test
results" is the empirical backstop for that argument.

## Per-buffer analysis (against `7a73be7`)

### `main.cpp`'s loop-body buffers

| Buffer | Declared at (7a73be7) | Filled by | Hoisted? |
| --- | --- | --- | --- |
| `iqsamples` | `main.cpp:935`, `IQSampleVector iqsamples = source_buffer.pull();` | `DataBuffer<IQSample>::pull()` (`include/DataBuffer.h:102-114`), which builds a local, always-empty `ret`, `std::swap`s it with `m_queue.front()` under the lock, and returns it by value (NRVO/move elision applies to the direct-initialization at the call site). | **No.** `pull()` always hands back an already-sized allocation that the producer thread built (see `sfmbase/FileSource.cpp:407,443`, which itself keeps one persistent `iqsamples` local across its own loop and moves it into `DataBuffer::push()` each iteration — the allocation genuinely has to cross the thread boundary). `iqsamples` in `main.cpp` never grows or shrinks on its own; it just receives a fully formed block every call. Hoisting it and changing `IQSampleVector iqsamples = source_buffer.pull();` (direct-init) to `iqsamples = source_buffer.pull();` (move-assignment) would not save an allocation — both forms end up with `iqsamples` holding exactly the vector `pull()` produced, and the *replaced* old buffer is discarded either way (at the end of the previous iteration if not hoisted, or by the move-assignment itself if hoisted). Confirmed by the allocation-count experiment below: `iqsamples` was deliberately left untouched, and the measured savings match the decoder-side changes exactly, with nothing left over that would suggest a missed opportunity here. |
| `if_shifted_samples` | `main.cpp:937`, fresh `IQSampleVector` every iteration | Either `fourth_downconverter.process(iqsamples, if_shifted_samples)` (`main.cpp:962`, taken when `enable_fs_fourth_downconverter` is true — zero-IF sources: Airspy HF+, RTL-SDR, or a `filesource` with `zero_offset`) or `if_shifted_samples = std::move(iqsamples)` (`main.cpp:964`, taken otherwise). | **Yes.** On the `fourth_downconverter.process()` path, `FourthConverterIQ::process()` (`include/FourthConverterIQ.h:38-82`) unconditionally `resize()`s `samples_out` to `samples_in.size()` and then overwrites every element in the loop before returning — a full overwrite, so any leftover contents from a previous, longer-lived buffer are never read. Hoisting is safe and beneficial on this path. On the `std::move(iqsamples)` path it is a pure ownership transfer either way (see the `iqsamples` row); hoisting neither helps nor hurts there. |
| `if_downsampled_samples` | `main.cpp:938` | **Never used anywhere in the file** (confirmed by `grep`; it is declared and never read or written again). Pre-existing dead code, unrelated to this change. | Left exactly as-is, still declared fresh inside the loop, still unused. Not part of this change's scope; removing genuinely dead code is a separate cleanup. |
| `if_samples` | `main.cpp:939` | Either `if_resampler.process(if_shifted_samples, if_samples)` (`main.cpp:969`, taken when `enable_downsampling` is true) or `if_samples = std::move(if_shifted_samples)` (`main.cpp:971`, otherwise). | **Yes.** `IfResampler::process()` (`sfmbase/IfResampler.cpp:39-80`) always `resize()`s `samples_out` to the r8brain resampler's reported `output_length_re` and then overwrites every element (`sfmbase/IfResampler.cpp:68-75`) before returning — full overwrite, safe to hoist. This is the path that matters most for AM and NBFM (both decimate 384 kHz down to 48 kHz internally, so `enable_downsampling` is true for them against the 384 kHz test file used here); FM's IF target rate (384 kHz, `FmDecoder::sample_rate_if`) equals the test file's rate, so `enable_downsampling` is false for FM against this particular file and the resampler path is not exercised by the FM test cases below (see the coverage caveat). |
| `audiosamples` | `main.cpp:942`, `SampleVector audiosamples(0);` | The decoder's `process()` call (`fm.process`/`nbfm.process`/`am.process`, `main.cpp:1006,1011,1021`), which on every code path either calls `audio.resize(0)` directly (early-return paths, e.g. `FmDecode.cpp:92-94,146-149,194-197`) or fully computes and then swaps/moves a member buffer into `audio` (the happy path, see below). | **Yes.** Every reachable path through all three decoders' `process()` leaves `audio` in a fully determined state (either explicitly sized to 0, or holding a freshly computed block) before returning, so a stale value left over from an earlier iteration is never read through `audio` itself. The `continue` statements in the loop (`main.cpp:946-949,987-990`) that skip a `.process()` call entirely happen strictly before `audiosamples` would be read that iteration (the `audio_exists` check and the write are both after the decode `switch`), so a skipped iteration never observes the previous iteration's leftover contents. |

Change: `main.cpp` now declares `if_shifted_samples`, `if_samples`, and
`audiosamples` once, immediately above
`// NOTE: main processing loop from here` (current `main.cpp:931-933`),
and the loop body no longer redeclares them. `iqsamples` and
`if_downsampled_samples` are unchanged.

### Decoder output-parameter sites

| Site (7a73be7) | Before | After (current) | Member buffer's last writer before this line |
| --- | --- | --- | --- |
| `sfmbase/FmDecode.cpp:228` | `audio = std::move(m_buf_mono);` | `std::swap(audio, m_buf_mono);` (`FmDecode.cpp:234`) | `m_pilotcut_mono.process(m_buf_mono_firstout, m_buf_mono)` (`FmDecode.cpp:199`, a `LowPassFilterFirAudio`), then `m_dcblock_mono.process_inplace(m_buf_mono)` (`FmDecode.cpp:201`, in-place, does not depend on prior contents beyond the size `process()` just set). |
| `sfmbase/AmDecode.cpp:222` | `audio = std::move(m_buf_baseband);` | `std::swap(audio, m_buf_baseband);` (`AmDecode.cpp:228`) | `m_afagc.process(m_buf_baseband_demod, m_buf_baseband)` (`AmDecode.cpp:208`, an `AfSimpleAgc`), optionally followed by `m_deemph.process_inplace(m_buf_baseband)` (`AmDecode.cpp:218`, in-place, AM mode only). |
| `sfmbase/NbfmDecode.cpp:97` | `audio = std::move(m_buf_baseband_filtered);` | `std::swap(audio, m_buf_baseband_filtered);` (`NbfmDecode.cpp:103`) | `m_audiofilter.process(m_buf_baseband, m_buf_baseband_filtered)` (`NbfmDecode.cpp:90`, a `LowPassFilterFirAudio`), then `Utility::adjust_gain(m_buf_baseband_filtered, audio_gain)` (`NbfmDecode.cpp:94`, in-place). |

## Safety argument per swap site

For every one of the three sites, the change is safe because:

1. **The line is the function's last statement.** In all three decoders,
   the swap/move is the final line of `process()`; the member buffer
   (`m_buf_mono`, `m_buf_baseband`, `m_buf_baseband_filtered`) is never
   read again after it, whether the old code moved or the new code swaps.
2. **The member buffer is always fully overwritten immediately
   beforehand.** `LowPassFilterFirAudio::process()`
   (`sfmbase/Filter.cpp:108-143`), `AfSimpleAgc::process()`
   (`sfmbase/AfSimpleAgc.cpp:36-53`), and the in-place filters
   (`LowPassFilterRC::process_inplace`, `HighPassFilterIir::process_inplace`,
   `Utility::adjust_gain`) were each read in full. The two `process()`
   functions both `resize()` their output to the current block's length
   and then write every element in a loop (or via a single VOLK dot
   product per output sample, in the FIR case) before returning; the
   in-place functions operate only on `samples.size()` elements, which
   was just set by the preceding `resize()`. None of them read the
   output buffer's prior contents at any point. This means whatever
   `swap` hands back into the member buffer (main's previous, unrelated
   `audiosamples`/`audio` contents) is completely irrelevant: it is
   discarded by the next `resize()`-and-overwrite before it could ever be
   read.
3. **No path relies on the output parameter arriving pre-cleared.** Every
   early-return path in all three decoders calls `audio.resize(0)`
   directly on the caller's buffer, not through the member buffer being
   swapped, so those paths are unaffected by this change.
4. **The pattern matches what `7a73be7` already applied and shipped.**
   The three internal `FmDecoder` member-to-member swaps
   (`m_samples_in_multipathfiltered`/`m_samples_in_after_agc`,
   `FmDecode.cpp:116,132,136`) already use this exact `std::swap`
   reasoning; this change extends it to the three remaining
   `std::move`-into-output-parameter sites the original review (§3.4)
   identified but left open pending the `main.cpp` hoist.

## Rejected / risky candidates

- **`iqsamples`**: not hoisted. As analyzed above, it always arrives
  pre-allocated from `DataBuffer::pull()`'s internal swap with the
  producer thread's block; hoisting it changes direct-initialization into
  a move-assignment but saves no allocation, since the incoming buffer's
  size is set entirely by the producer, not by `iqsamples` growing to fit
  it. Changing it was assessed as pure risk (one more cross-iteration
  state dependency to verify) for zero benefit, so it was left alone.
- **`if_downsampled_samples`**: confirmed dead code (never read or
  written beyond its own declaration) pre-dating this change. Left
  untouched; removing it is an unrelated cleanup, not part of this
  buffer-lifetime change, and touching it would have added an unrelated
  edit to the diff.
- **Modifying `DataBuffer::pull()`'s signature** (e.g., to an output
  parameter the caller could swap into) was considered and rejected: it
  would add no allocation savings for the reasons above (the producer's
  block still has to cross the thread boundary and still gets destroyed
  either as the queue's popped node or as the caller's old buffer), while
  adding real risk by touching a data structure shared between the
  producer and consumer threads. Out of scope for this change.
- **Hoisting `SampleVector audiosamples(0)`'s explicit `(0)` argument**:
  dropped in favor of the default constructor (`SampleVector audiosamples;`)
  since both start at size/capacity 0 and the explicit `(0)` served no
  purpose either before or after hoisting.

## Coverage caveat

The test matrix below uses `filesource` against a 384 kHz IQ WAV file
without `zero_offset`. `FileSource::is_low_if()` returns `!m_zero_offset`,
so `is_low_if()` is true and `enable_fs_fourth_downconverter` is false for
every command in this matrix. This means the `fourth_downconverter.process()`
branch that writes into the hoisted `if_shifted_samples` (the branch real
Airspy HF+/RTL-SDR hardware, or a `filesource` with `zero_offset`, would
take) was **not exercised** by any test run in this report. The `else`
branch (`if_shifted_samples = std::move(iqsamples)`, a pure ownership
transfer) was exercised instead, on every test case. The hoist of
`if_shifted_samples` is still correct and safe by the analysis above
(`FourthConverterIQ::process()` fully overwrites its output), but its
allocation benefit was not independently measured here; only the
`if_samples` (via `IfResampler`, exercised by AM/NBFM) and `audiosamples`
(via the decoder swap, exercised by every mode) hoists have direct
allocation-count evidence in this report.

## What was changed

- `main.cpp`: hoisted `if_shifted_samples`, `if_samples`, and
  `audiosamples` from loop-body declarations to declarations just above
  the main loop. `SampleVector audiosamples(0);` became
  `SampleVector audiosamples;` (equivalent, and consistent with the other
  two hoisted declarations having no explicit initializer).
- `sfmbase/FmDecode.cpp`, `sfmbase/AmDecode.cpp`, `sfmbase/NbfmDecode.cpp`:
  changed the final `audio = std::move(m_buf_...);` to
  `std::swap(audio, m_buf_...);`, each with a comment explaining why the
  swapped-in stale contents are never read.
- All four touched files were formatted with `clang-format -i` per the
  project's `format` skill; `git diff` shows no reformatting beyond the
  touched hunks.
- No numerical/behavioral change is intended or observed (see bit-exact
  results below); this is a pure allocation-lifetime change.

## Test results

### Build

Both a baseline build (`7a73be7`, in a throwaway detached worktree) and
the patched build (this worktree) were built with
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` followed by
`cmake --build build --target all`, matching the project's `build` skill.
Both builds completed with **zero errors** and the same single
pre-existing, unrelated warning in both:

```
sfmbase/IfResampler.cpp:55:28: warning: variable 'output_length_im' set but not used [-Wunused-but-set-variable]
```

This warning is present identically in the unmodified baseline and is not
touched by this change; no new warnings were introduced.

### Bit-exactness

Six file-source decodes were run against
`test-files/joakfm-20260715045930z-iq.wav` (60 s, 384 kHz, 32-bit float
stereo IQ WAV), each writing 32-bit float WAV output via `-G`, with
config `freq=82500000,srate=384000,filename=<file>`:

| Case | Command flags | `cmp` result | Queue overflow in log? |
| --- | --- | --- | --- |
| FM default | `-m fm` | identical | no |
| FM `-E 36` | `-m fm -E 36` | identical | no |
| FM `-X` | `-m fm -X` | identical | no |
| FM `-f narrow` | `-m fm -f narrow` | identical | no |
| AM | `-m am` | identical | no |
| NBFM | `-m nbfm` | identical | no |

All six baseline/patched WAV pairs are byte-for-byte identical
(`cmp` exit code 0, no differing byte/line reported). None of the twelve
logs (6 baseline + 6 patched) contains the string "overflow" in any case.
This confirms the change is numerically a no-op, as expected from a
move-vs-swap substitution on always-fully-overwritten buffers.

### Allocation count

Wall-clock/CPU timing on a DSP pipeline this size was expected to be a
weak signal (see below), so heap allocations were counted directly with a
scratch-only shim: a small translation unit
(`AllocCounter.cpp`, not part of the tracked source tree) overriding the
global `operator new`/`operator new[]`/`operator delete`/`operator
delete[]` to count every call, printed to stderr at process exit. This
was linked into two **separate, throwaway instrumented builds** — a copy
of the `7a73be7` baseline and a copy of this patch — each built and run
outside the tracked worktrees, per the task's request for "a simple
operator new counting shim compiled only in your scratch build." The
instrumented binaries were confirmed to produce byte-identical WAV output
to the non-instrumented builds before trusting their counts.

| Case | Baseline `new`/`delete` calls | Patched `new`/`delete` calls | Reduction | Reduction % | Blocks processed |
| --- | ---: | ---: | ---: | ---: | ---: |
| FM default | 67,616 | 56,368 | 11,248 | 16.6% | ~11,250 (23,040,000 samples / 2048-sample filesource block) |
| AM | 67,640 | 45,145 | 22,495 | 33.3% | ~11,250 |

The reductions line up almost exactly with the theoretical count from the
per-buffer analysis above: FM saves one allocation per block (only the
`audiosamples`/`m_buf_mono` swap actually avoids a reallocation for this
test's configuration, since `if_shifted_samples`/`if_samples` take the
pure-move, no-compute path — see the coverage caveat), while AM saves two
per block (the `IfResampler`-fed `if_samples` hoist, plus the
`audiosamples`/`m_buf_baseband` swap). 11,248 and 22,495 are within
0.02%/0.4% of exactly 1x and 2x the ~11,250 processed blocks,
respectively — as clean a confirmation as this kind of measurement gets.

### CPU / wall-clock time

`/usr/bin/time -l` was run three times each for FM default and AM, with
the baseline and patched binaries run concurrently (`&` + `wait`) to
control for host load variation between repeats, over the same 60 s test
file:

| Case | Metric | Baseline (3 reps) | Patched (3 reps) |
| --- | --- | --- | --- |
| FM default | user CPU (s) | 4.28, 4.49, 4.50 (avg 4.42) | 4.28, 4.50, 4.46 (avg 4.41) |
| FM default | sys CPU (s) | 1.35, 1.12, 1.04 (avg 1.17) | 1.47, 1.21, 0.95 (avg 1.21) |
| FM default | max RSS (bytes) | 9,175,040 / 9,224,192 / 9,191,424 | 9,207,808 / 9,158,656 / 9,158,656 |
| AM | user CPU (s) | 1.43, 1.39, 1.32 (avg 1.38) | 1.41, 1.38, 1.31 (avg 1.37) |
| AM | sys CPU (s) | 1.02, 1.12, 1.23 | 0.94, 1.08, 1.17 |
| AM | max RSS (bytes) | 8,519,680 / 8,503,296 / 8,552,448 | 8,404,992 / 8,486,912 / 8,568,832 |

Wall-clock (`real`) time is dominated by `FileSource`'s real-time pacing
sleep (`sfmbase/FileSource.cpp:450-457`) and is ~60.0 s in every case,
carrying no information here. User/sys CPU time and max RSS both show
patched values within the same spread as baseline's own repeat-to-repeat
variation — i.e., **no measurable CPU or peak-memory benefit at this
granularity.** This is expected and is being stated honestly rather than
oversold: the DSP compute per block (IIR/FIR filtering, resampling,
AGC, phase discrimination, VOLK-accelerated dot products) costs far more
CPU than a few-hundred-KB `malloc`/`free` pair, so removing 1-2
allocations per block is invisible against several seconds of accumulated
compute time, even though it is a clean, real, and reproducible ~17-33%
cut in total allocation *count*. Running the two binaries concurrently
during timing also added scheduler noise (visible in the elevated
"involuntary context switches" counts, in the tens of thousands, in the
raw `/usr/bin/time -l` output), which further limits this experiment's
sensitivity to a difference this small in CPU-time terms.

## Recommendation

**Apply the change as implemented** (all four files, main.cpp hoist plus
all three decoder swaps together — they are not independently useful,
per the dependency this report set out to resolve). Reasoning:

- It is behavior-preserving: six bit-exact WAV comparisons across FM
  (default, `-E 36`, `-X`, `-f narrow`), AM, and NBFM, with no queue
  overflows.
- It introduces no new compiler warnings.
- It has a clear, reproducible, mechanistically-explained benefit: a
  16.6% (FM) to 33.3% (AM) reduction in total heap allocation/deallocation
  calls in the main processing loop, matching the theoretical prediction
  to within a fraction of a percent.
- The CPU/memory benefit is not measurable at the process level with the
  methods available here, and this report says so plainly rather than
  extrapolating from the allocation count. The change is worth taking for
  reduced allocator pressure and cleaner code (it finishes a change the
  prior review started and explicitly deferred), not for a CPU speedup
  that this report did not find.
- The risk is low and well-contained: every swap site was individually
  verified against its immediate caller, the pattern reuses one already
  shipped in `7a73be7`, and the `iqsamples` and `if_downsampled_samples`
  buffers were deliberately left alone because hoisting them would add
  risk for no benefit.

No subset recommendation is needed: the four changed files form one
coherent change (the `main.cpp` hoist is what makes the three decoder
swaps meaningful; shipping only one half would either have no effect
absent the other, or duplicate wasted effort in the future when the other
half eventually lands).
