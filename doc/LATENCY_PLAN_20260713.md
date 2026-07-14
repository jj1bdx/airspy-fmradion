# Output latency analysis and reduction plan (20260713)

**Date:** 2026-07-13
**Author:** Claude Code (claude-sonnet-5), read-only analysis
**Scope:** Where the maintainer's observed ~200 ms end-to-end latency
(antenna to audible output, macOS, PortAudio) comes from, how to measure
it further without a rewrite, and ranked options to reduce it. This
document makes no code changes; it is an analysis and planning
artifact only.

## 0. Starting point: this has already been investigated once

Before re-deriving everything from the DSP chain, it is worth stating
what the repository's own history already shows, because it reframes
the whole problem:

- `CHANGES.md:36` (Known limitations) records the maintainer's own
  measurement: **~200 ms** between a Sony ICF-M780N analog radio's
  audio and airspy-fmradion's audio (`FM -E 100` stereo, USB DAC FiiO
  K7, macOS 26.1, Mac mini 2023), with the explicit note *"the latency
  between PortAudio and USB DAC itself is dominant."*
- That measurement was taken with a macOS-specific low-latency
  PortAudio configuration already applied, on an **unmerged** branch
  `origin/dev-pa-coreaudio` (diverged from `dev` at commit `7fea136`,
  "Version 20260211-0", never merged forward past that point). On that
  branch, `sfmbase/AudioOutput.cpp` uses, under `#ifdef __APPLE__`:
  `Pa_GetDeviceInfo(...)->defaultLowOutputLatency` (instead of
  `defaultHighOutputLatency`), a floor of `minimum_latency_low = 0.025`
  s (`include/AudioOutput.h` on that branch, commit `4c8868b`), and
  `PaMacCore_SetupStreamInfo(&mac_core_flags,
  paMacCoreChangeDeviceParameters)` (commit `30cd700`) — the PortAudio
  CoreAudio host-API flag that permits the host to actually renegotiate
  the physical device's buffer geometry instead of silently converting
  through whatever buffer size CoreAudio already had open.
- Even with that tuning, ~200 ms was still observed, and the
  maintainer's own conclusion was that the audio driver stack (not the
  DSP pipeline) dominates.
- A separate, independent experiment (2023) replaced the blocking
  `Pa_WriteStream()` call with a callback-driven `pa_ringbuffer`
  implementation (commit `5a88090` … `367e281`, reverted in `39a4cff`;
  see also `doc/20230908-portaudio-callback-test.diff`). The revert
  commit message is direct: *"The latency is not improved comparing
  with the one using blocking stream. So this implementation is
  unfortunately a simple overkill."* It also introduced audible
  glitches ("clicky") and broke a Linux gstreamer-RTP loopback setup.

These two facts — a targeted macOS CoreAudio tuning branch that still
saw ~200 ms, and a callback-rewrite that measurably did *not* help —
are strong prior evidence that most of the 200 ms is **not** software
buffering choices inside this program, but CoreAudio's host-side
buffering/format-conversion plus the FiiO K7's own USB Audio Class
driver and internal DSP latency. The analysis below quantifies what
the software chain *can* account for, so the residual can be bounded
with more confidence, and lists why the two "obvious" fixes were
already tried.

## 1. Latency budget for a representative configuration

**Assumed configuration:** RTL-SDR device, `srate=1152000` (the
`RtlSdrSource::configure()` default, `sfmbase/RtlSdrSource.cpp:83`),
default `blklen` (`RtlSdrSource::default_block_length = 16384`,
`include/RtlSdrSource.h:33`), FM broadcast stereo, default filter
(`fmfilter_enable = false`, `main.cpp:809-812`), multipath filter off
(`-E` not given, `multipathfilter_stages = 0`, `main.cpp:295`),
PortAudio output at 48 kHz stereo, on macOS.

| # | Stage | Mechanism | Formula | Estimate (ms) |
|---|-------|-----------|---------|----------------|
| 1 | RTL-SDR block fill | batching: one full block must arrive before the callback pushes it | `blklen / srate` | 16384/1152000 = **14.22** |
| 2 | USB in-flight buffers | jitter tolerance only, not steady-state delay (see below) | — | **~0** (steady state) |
| 3 | `FourthConverterIQ` | per-sample complex multiply, no memory | — | ~0 |
| 4 | `IfResampler` (1152→384 kHz, r8brain `CDSPResampler24`) | FIR/multirate group delay | Harris estimate, see §1.2 | **~1–2** |
| 5 | `fmfilter` (default: `delay_3taps_only_iq`) | 3-tap all-pass delay | (3-1)/2 / 384000 | 0.0026 |
| 6 | `IfSimpleAgc` | memoryless | — | 0 |
| 7 | `MultipathFilter` | disabled by default | (3·stages+1)/384000 | 0 (off) / 0.78 at `-E 100` |
| 8 | `PhaseDiscriminator` | 1-sample state | — | ~0 |
| 9 | `AudioResampler` (384→48 kHz, r8brain `CDSPResampler`) | FIR/multirate group delay | Harris estimate, see §1.2 | **~10–15** |
| 10 | Pilot-cut `LowPassFilterFirAudio` (127 taps @ 48 kHz) | FIR group delay | (127-1)/2 / 48000 | **1.31** |
| 11 | DC block + de-emphasis (biquad IIR + 1-pole IIR) | IIR group delay near DC | ≈ τ (50 µs) + a few samples | ~0.1 |
| 12 | `DataBuffer` queue (IQ) | steady-state ≈ 0 depth | — | ~0 (see §2) |
| 13 | main-loop compute (filters+resample+decode per block) | CPU time, not wall-clock batching | — | <1 (well under real time) |
| 14 | PortAudio `suggestedLatency` floor | requested ring-buffer size | `max(defaultHighOutputLatency, 0.04)` | **40** (floor wins; measured `defaultHighOutputLatency` ≈14.7 ms on Mac mini 2023, `include/AudioOutput.h:105`) |
| 15 | CoreAudio host buffering / USB DAC driver | **not visible from source**, PortAudio's `suggestedLatency` is advisory only | — | **unknown, empirically dominant (§0)** |

Software-side sum (rows 1, 4, 5, 9, 10, 11, 14) ≈ 14.2 + 1.5 + 0.003 +
13 + 1.3 + 0.1 + 40 ≈ **~70 ms**, against an observed ~200 ms. That
leaves **~130 ms unaccounted for**, which §0's prior-art evidence
attributes mainly to row 15: CoreAudio's actual negotiated
device/host buffer (which can exceed the requested
`suggestedLatency`; PortAudio does not guarantee it) plus the FiiO K7
USB DAC's own internal buffering/DSP latency. This is stated as the
best current hypothesis, not a proven number — see §3 for how to
narrow it down.

### 1.1 Why the IF resampler and audio resampler differ

Both use r8brain-free-src's `CDSPResampler` family
(`r8brain-free-src/CDSPResampler.h`), which is a linear-phase,
multirate FIR design. `IfResampler` uses `CDSPResampler24`
(`include/IfResampler.h:41-42`), which fixes `ReqTransBand=2.0`
(percent) and `ReqAtten=180.15` dB (`CDSPResampler.h:804-809`).
`AudioResampler` uses the plain `CDSPResampler`
(`include/AudioResampler.h:41`, `sfmbase/AudioResampler.cpp:28-29`),
whose *default* parameters are `ReqTransBand=2.0`, `ReqAtten=206.91`
dB (`CDSPResampler.h:117-120`) — nearly 27 dB more attenuation than
the IF path, and neither wrapper overrides these defaults.

`ReqTransBand` is specified as a percentage of the *output* Nyquist
frequency when downsampling (`CDSPResampler.h:87-90`). For
`IfResampler` (1152→384 kHz) the output Nyquist is 192 kHz, so the
absolute transition width is `0.02 × 192000 = 3840` Hz. For
`AudioResampler` (384→48 kHz) the output Nyquist is 24 kHz, so the
absolute transition width is only `0.02 × 24000 = 480` Hz — 8× narrower
in Hz even though the *percentage* is identical. Using the standard FIR
length rule of thumb (Harris' approximation,
`N ≈ (Atten_dB − 8) / (2.285 × Δω)` with `Δω = 2π·Δf/Fs`, group delay
`≈ N/(2·Fs)`):

- `IfResampler`: `(180.15−8)/(28.72 × 3840) ≈ 1.6 ms`
- `AudioResampler`: `(206.91−8)/(28.72 × 480) ≈ 14.4 ms`

These are **analytical estimates**, not measured r8brain output —
r8brain builds a multi-stage half-band/polyphase decomposition rather
than a single monolithic FIR, so the true figure will differ somewhat,
but the *ratio* between the two resamplers (audio resampler
dominating) should hold, because it is driven by the absolute
transition bandwidth in Hz, which is fixed by the output rate
regardless of implementation structure. §3 describes how to get the
exact number with zero source changes (the code already has the
instrumentation, just disabled).

### 1.2 r8brain's `DoConsumeLatency` does not remove steady-state delay

`r8brain-free-src/CDSPBlockConvolver.h` has `aDoConsumeLatency = true`
by default (`:64`). When true, the *first* `Latency` output samples
(the filter's own group delay,
`Latency = (int) LatencyFrac` at `:96-101`) are silently discarded
inside the convolver (`LatencyLeft` bookkeeping, `:220-226`,
`:528-538`), so the caller never sees the meaningless
zero-history warm-up samples. This is purely a **startup transient**
fix: after that one-time discard, every subsequent output sample is
still delayed by the filter's true group delay relative to the input
sample it corresponds to — the group delay of a causal FIR filter is a
constant, physical processing delay, not something a "consume latency"
flag can remove. `getInLenBeforeOutStart()`
(`CDSPResampler.h:443-464`) reports exactly that: "the number of input
samples required to advance to the specified output sample position,"
run from a cleared state — i.e., how many input samples of history the
filter needs before the *first* valid output emerges, which is the
group delay in input-sample units. `IfResampler.cpp:30-33` and
`AudioResampler.cpp:30-33` already call this function, gated behind
`DEBUG_IFRESAMPLER` / `DEBUG_AUDIORESAMPLER` macros that are not
defined by default — see §3.

### 1.3 Other devices, for comparison

- **Airspy HF+** at its default `srate=384000`
  (`sfmbase/AirspyHFSource.cpp:282`) exactly matches
  `FmDecoder::sample_rate_if = 384000` (`include/FmDecode.h:38`), so
  `enable_downsampling` is false (`main.cpp:801`) and `IfResampler` is
  skipped entirely — a deliberate optimization
  (`CHANGES.md:130`, 20230526-0). Its native `airspyhf_start()`
  callback (`sfmbase/AirspyHFSource.cpp:404-414`) delivers
  vendor-library-determined block sizes not controlled by this
  project; `main.cpp:695`'s `if_blocksize = 2048` is a display-only
  guess used for the status-line refresh rate
  (`main.cpp:707-709`), **not** a value passed to
  `airspyhf_start()`. If it does approximate the real transfer size,
  batching latency would be `2048/384000 ≈ 5.3 ms` — much smaller than
  RTL-SDR's default 14.2 ms, but this needs runtime confirmation
  (§3).
- **Airspy R2/Mini**: `is_low_if()` returns true
  (`sfmbase/AirspySource.cpp:189`), so `FourthConverterIQ` is skipped
  (`main.cpp:680,935-942`). Its native `airspy_start_rx()` callback
  (`sfmbase/AirspySource.cpp:440-462, 484-494`) again delivers a
  vendor-determined block size; `main.cpp:691`'s `if_blocksize = 65536`
  is display-only (same caveat as above).

## 2. One-time startup delay vs. steady state

Only steady state matters for the user's perception once playback has
settled, so it is important to separate the two:

- **`DataBuffer<IQSample>` (`include/DataBuffer.h`)**: the consumer
  (`main.cpp:912`, `source_buffer.pull()`) runs a tight loop with no
  self-imposed pacing; it blocks on the condition variable only when
  the queue is empty (`DataBuffer.h:102-114`). As long as the CPU keeps
  up with the source's real-time rate (true for the default
  configuration on any machine from the last decade), queue depth
  converges to 0–1 blocks almost immediately after the first block
  arrives — there is no steady-state backlog. The 1024-block
  drop-oldest ceiling (`DataBuffer.h:40`, `:55-56`) exists purely as a
  memory-safety backstop for a *persistently* overloaded consumer
  (e.g. `-E` with very large `stages` on a slow machine, or macOS
  scheduling a higher-priority process — `CHANGES.md:31` documents
  observed audio cracking under that exact condition). If that
  happens, queue depth *does* grow and steady-state latency *does*
  increase (up to the full 1024-block bound), so it is a real risk
  worth monitoring, not just a startup artifact — see §3 for how to
  watch it.
- **No second `DataBuffer` on the audio side.** `main.cpp` calls
  `audio_output->write(std::move(audiosamples))` directly
  (`main.cpp:1030`) once per processed block; there is no intermediate
  `DataBuffer<Sample>` or output thread (that separate output thread
  was removed in 20231216-0, per `CHANGES.md:112`). So there is exactly
  one buffering stage between decoder and PortAudio: PortAudio's own
  ring buffer, sized by `suggestedLatency`.
- **PortAudio's blocking `Pa_WriteStream()`** (`AudioOutput.cpp:299`)
  is self-limiting: it blocks until there is room, so the app can
  never get further ahead of real time than the ring buffer allows.
  There is no unbounded backlog possible on the audio side; underruns
  are treated as benign (`AudioOutput.cpp:303-305`,
  `paOutputUnderflowed`).
- **The `r8brain` startup transient** (§1.2) is a genuine one-time
  effect: the first `Latency` samples of *processing* are consumed
  silently at each resampler's construction. It does not recur and
  does not affect steady-state per-sample latency once the pipeline is
  running.
- **`m_wait_multipath_blocks = 100`** (`sfmbase/FmDecode.cpp:33`) is a
  100-block warm-up before the multipath filter activates
  (`FmDecode.cpp:107-128`) — a startup-only behavior (bypass, not
  extra delay) that does not apply once running, and is irrelevant
  when `-E` is not given.

Conclusion: for the default configuration, essentially all of the
observed latency is steady-state, not a startup artifact that fades
after a few seconds. The startup-only effects (r8brain's internal
discard, the multipath warm-up, initial `DataBuffer` fill) are all
transients well under a second and do not explain any part of a
persistent ~200 ms gap.

## 3. How to measure, without code changes (or with trivial ones)

1. **DSP-pipeline-only latency, no SDR hardware needed.** `FileSource`
   paces itself to real time using `std::chrono::steady_clock`
   (`sfmbase/FileSource.cpp:433-458`, hardened in 20260505-0/V30).
   Build (or obtain) a short raw/WAV IQ file containing one sharp,
   known event (e.g. a modulated 1 kHz tone burst with a hard edge) at
   a known sample offset `N`. Launch
   `airspy-fmradion -t filesource -c filename=...,srate=...,blklen=...
   -P -` and record the wall-clock start time `T0` from the shell
   (e.g. `date +%s.%N` immediately before exec). The event's expected
   real-time emission is `T0 + N/srate`. Record the actual system
   audio output (e.g. a loopback capture via macOS `Audio MIDI Setup`
   aggregate/loopback device, or a microphone next to the speaker) and
   find when the event is actually audible, `T_actual`. The
   difference `T_actual − (T0 + N/srate)` is the **entire** software +
   PortAudio + CoreAudio + DAC path latency, with no device-clock
   ambiguity, and needs zero source changes.
2. **Isolate the resampler contribution exactly** (needs a rebuild,
   but zero logic change): the two resampler wrappers already contain
   the necessary instrumentation, compiled out by default.
   `IfResampler.cpp:30-33` and `AudioResampler.cpp:30-33` call
   `getInLenBeforeOutStart()` and print the result when
   `DEBUG_IFRESAMPLER` / `DEBUG_AUDIORESAMPLER` are defined. Adding
   `-DDEBUG_IFRESAMPLER -DDEBUG_AUDIORESAMPLER` to the build (a
   compiler-define-only change, no source edit) prints the exact
   r8brain-reported latency in input samples for the actual configured
   rates; divide by the relevant sample rate for milliseconds. This
   replaces the analytical Harris estimates in §1.1/§1.2 with r8brain's
   own numbers.
3. **DataBuffer backlog / drop monitoring.** `DataBuffer::queue_size()`
   (`include/DataBuffer.h:90-96`) and `dropped_blocks()` (`:71-77`)
   already exist but are not called from `main.cpp`. Temporarily
   logging `source_buffer.queue_size()` alongside the existing
   per-block status line (`main.cpp:1067-1094`) would show directly
   whether queue depth is 0 (as expected) or growing (indicating a
   CPU/scheduling problem inflating steady-state latency) — this is a
   minimal, additive instrumentation change, not a logic change.
4. **PortAudio's actually negotiated latency.** `Pa_OpenStream()`
   (`AudioOutput.cpp:231-238`) only *requests* `suggestedLatency`; the
   host API may allocate something different. `Pa_GetStreamInfo()`
   (not currently called anywhere in this file) returns the actual
   negotiated `outputLatency` after the stream opens. Adding one
   `fmt::println` of `Pa_GetStreamInfo(m_stream)->outputLatency` right
   after `Pa_OpenStream()` succeeds (`AudioOutput.cpp:238`) would show
   whether CoreAudio honored the ~40 ms request or silently used
   something larger — directly testing the §0 hypothesis with a
   one-line, non-invasive change.
5. **macOS system-level cross-check, no app changes.** `Audio MIDI
   Setup` shows the configured nominal buffer size/sample rate for the
   output device; the `system_profiler SPAudioDataType` command and
   Instruments' Core Audio template can show the actual I/O buffer
   duration CoreAudio is running the FiiO K7 at. Comparing that number
   against the ~40 ms this program requests would confirm or rule out
   CoreAudio/driver buffer inflation as the source of the missing
   ~130 ms without touching the binary at all.
6. **The maintainer's own side-by-side method** (already used to get
   the ~200 ms figure, `CHANGES.md:36`) — a second, independent analog
   receiver playing the same broadcast next to the computer's speaker,
   recorded together on a phone and compared in an audio editor — is a
   valid end-to-end sanity check and should continue to be the final
   validation step for any change below, since it is the only method
   that has already produced a trusted number.

## 4. Workarounds (no code change, or a one-line config/parameter change)

| Change | Mechanism | Expected saving | Risk |
|---|---|---|---|
| Reduce RTL-SDR `blklen` from 16384 to 4096 (`-c srate=1152000,blklen=4096`) | Shrinks the batching delay of §1 row 1 | ~10.7 ms (14.2→3.6 ms) | Shrinks the async in-flight jitter-tolerance margin from ≈213 ms to ≈53 ms at 1.152 Msps (`doc/RTL_READ_ASYNC_20260713.md:242-247`) — still ample headroom versus typical scheduling hiccups, low risk |
| Prefer Airspy HF+ at its default 384 kHz over RTL-SDR at 1152 kHz, where device choice is flexible | Skips `IfResampler` entirely and (likely) uses a smaller native block (§1.3) | Removes IfResampler's ~1–2 ms and most of the 14.2 ms RTL batching term; net likely ~10 ms+ | None from software; depends on which SDR the antenna setup actually has |
| Explicitly set `PortAudioOutput::minimum_latency` lower and use `defaultLowOutputLatency` (this is exactly what `origin/dev-pa-coreaudio` already does, unmerged) | Reduces the requested ring-buffer floor (§1 row 14) | ~15 ms best case (40→25 ms), **possibly 0** if CoreAudio does not honor the lower request without also setting `paMacCoreChangeDeviceParameters` (§0) | Already measured on that branch to still yield ~200 ms overall; low risk to try (macOS-gated), but do not expect it alone to close the gap |
| Reduce block size for other sources' `blklen` where configurable (`FileSource`) | Same batching-delay mechanism as RTL-SDR | Device/rate dependent | Low; `FileSource` already clamps and paces (`sfmbase/FileSource.cpp:239-260`) |
| Avoid large `-E` (multipath filter) stage counts unless needed | Removes `(3·stages+1)/384000` s of group delay, and the CPU headroom risk noted in §2 | Up to ~8 ms saved at `stages=1024`; ~0.78 ms at the maintainer's own `-E 100` | None beyond losing multipath correction quality |

None of these touches source files; they are CLI/config changes (or,
for the last row, restraint in choosing a flag value), consistent with
"no code change or trivial parameter change."

## 5. Architectural changes (code changes, ranked by benefit/effort)

1. **Revive and finish the `origin/dev-pa-coreaudio` macOS tuning,
   properly instrumented.** That branch already has the
   `#ifdef __APPLE__` / `PaMacCore_SetupStreamInfo(...,
   paMacCoreChangeDeviceParameters)` + `defaultLowOutputLatency` +
   25 ms floor code, gated so Linux/Raspberry Pi behavior is
   unchanged. Rebasing it onto current `dev` (it is currently stuck at
   the pre-`RTL_READ_ASYNC`/pre-vulnerability-fix commit `7fea136` and
   has drifted significantly, per the 42-file diff against `dev`) and
   adding the `Pa_GetStreamInfo()` logging from §3 item 4 would let the
   maintainer directly confirm the effect the very first time this is
   run, rather than repeating the "it still measured ~200 ms" outcome
   blind. **Estimated saving:** unknown until measured — could be
   as little as 15 ms (just the floor change) or could be much larger
   if `paMacCoreChangeDeviceParameters` genuinely lets CoreAudio open
   the FiiO K7 at a smaller native buffer (untested on this branch
   with the instrumentation that would prove it). **Complexity:** low
   (the code already exists; work is a rebase + one log line).
   **Regression risk:** low if kept `#ifdef __APPLE__`-gated as it
   already is; no effect on Linux/RPi.
2. **Add an explicit `--low-latency` profile** bundling: smaller
   default `blklen`, the macOS tuning from item 1, and (optionally)
   relaxed r8brain parameters from item 3. This revives the spirit of
   the old `-L` flag (`git commit 444dd51`, later removed) with the
   benefit of everything learned since. **Estimated saving:**
   the sum of §4's rows, roughly 25–40 ms of *known* software latency,
   plus whatever item 1 recovers from CoreAudio. **Complexity:**
   moderate (new CLI flag, wiring through `main.cpp`, `AudioOutput`,
   and the two resamplers). **Regression risk:** low — opt-in, default
   behavior unchanged.
3. **Pass explicit, looser `ReqTransBand`/`ReqAtten` to
   `AudioResampler`'s `r8b::CDSPResampler`** instead of relying on the
   library defaults (`ReqAtten=206.91` dB is unusually strict; see
   §1.1). Adding a constructor overload
   (`AudioResampler(input_rate, output_rate, ReqTransBand, ReqAtten)`)
   and passing something closer to `IfResampler`'s `180.15` dB /
   `2.0`%, or slightly looser, would shrink the ~14 ms estimate in
   §1.1. **Estimated saving:** roughly proportional — dropping
   attenuation to, say, 140 dB could shave several ms (needs the §3
   item 2 instrumentation to confirm before/after). **Complexity:**
   low-moderate (small code change, but audio-quality verification by
   ear/spectrum is required — no automated tests exist per
   `CLAUDE.md`, and `-ffast-math` is explicitly forbidden so this
   cannot be "optimized around" numerically). **Regression risk:**
   reduced stop-band attenuation could reintroduce aliasing artifacts
   audible as noise/imaging near 24 kHz-derived images; must be
   listened to on a real broadcast, not just measured.
4. **Decouple source block size from decoder block size** (stream
   sub-blocks through `IfResampler`/`FmDecoder` more often than once
   per full source push) to shrink §1 row 1's batching term toward its
   theoretical minimum. r8brain's `process()` accepts arbitrary chunk
   lengths, so this is mechanically feasible. **Estimated saving:**
   bounded by the batching term itself — at most ~14 ms for RTL-SDR's
   default configuration (less for Airspy HF+, which is already small).
   **Complexity:** high — touches `DataBuffer`, the main loop's
   pull/process granularity, and interacts with the very-recent
   (same-day) `rtlsdr_read_async()` rewrite
   (`doc/RTL_READ_ASYNC_20260713.md`) whose entire purpose was to
   *increase* the in-flight buffering for dropout robustness; smaller
   processing chunks work against that goal by increasing wake-up
   frequency and CPU overhead per sample, and reduce slack for
   scheduling hiccups. **Regression risk:** high relative to payoff —
   given the ceiling is only ~14 ms and there is no test suite to catch
   a regression, this is the lowest-priority item here despite sounding
   like the "proper" fix.
5. **Callback-based PortAudio with a small ring buffer — not
   recommended without new evidence.** This was already implemented
   and measured once (`git commit 367e281`/`39a4cff`, §0): it did not
   reduce latency and introduced audible glitches plus a Linux
   regression. Revisiting it only makes sense *after* item 1's
   instrumentation (`Pa_GetStreamInfo()`) shows that the blocking API
   is specifically the reason CoreAudio negotiates a larger buffer
   than requested — which the historical attempt suggests is not the
   case. If ever retried, it should be `#ifdef __APPLE__`-gated so the
   2023 Linux/gstreamer-RTP regression cannot recur.
6. **Reduce FIR filter orders (e.g. the 127-tap pilot-cut filter) —
   not worth it.** At 48 kHz this filter contributes only ~1.3 ms
   (§1, row 10); shortening it trades measurable stereo-pilot/audio
   filtering quality for a saving that is noise relative to the
   ~130 ms unexplained residual in §1. Not recommended.

## 6. macOS-specific factors

- **`defaultHighOutputLatency` vs. `defaultLowOutputLatency`.** The
  current code always requests `defaultHighOutputLatency`
  (`AudioOutput.cpp:219-220`), then raises it to a 40 ms floor if
  smaller (`:224-226`). The measured `defaultHighOutputLatency` on a
  Mac mini 2023 was ~14.7 ms (`AudioOutput.h:105`), so **the 40 ms
  floor is what actually governs today**, not the device's reported
  "high" latency. `defaultLowOutputLatency` would likely be a few
  milliseconds lower still, but as §0/§4 note, this alone was already
  tried (on `dev-pa-coreaudio`) without resolving the ~200 ms
  observation.
- **CoreAudio safety offsets and format conversion.** PortAudio's
  `paFramesPerBufferUnspecified` (`AudioOutput.cpp:234`) delegates the
  actual host buffer size to CoreAudio/PortAudio's `paMacCore` host
  API, which — *without* `paMacCoreChangeDeviceParameters` — does not
  necessarily reopen the physical device at the app's requested
  parameters; it may run the device at whatever buffer size/format it
  already had configured and convert in the host API layer, adding
  latency invisible to `suggestedLatency`. This is exactly the
  mechanism the unmerged branch's `paMacCoreChangeDeviceParameters`
  flag targets, and is the leading hypothesis for the ~130 ms residual
  in §1.
- **The USB DAC itself (FiiO K7)** sits below CoreAudio's own
  driver/HAL layer; USB Audio Class devices commonly add their own
  internal buffering/DSP latency (often tens of milliseconds) that no
  amount of `PortAudio`/CoreAudio-side tuning in this program can
  remove — it would show up identically for *any* CoreAudio
  application feeding that same DAC. Confirming this (§3 items 4–5, or
  simply testing with the Mac's built-in speakers instead of the FiiO
  K7) would be the single most informative next experiment, since it
  would either confirm or rule out the device as the dominant
  contributor independent of anything in this codebase.

## Summary

The DSP pipeline itself, for the default RTL-SDR configuration, is
estimated at roughly **70 ms** of the observed **~200 ms**
(§1): ~14 ms of unavoidable block-batching at the source, ~12–16 ms of
r8brain resampler group delay (mostly from the audio resampler's
stricter-than-necessary default attenuation), ~1.3 ms of pilot-cut
filtering, and a 40 ms PortAudio ring-buffer floor that this program
explicitly requests. The remaining **~130 ms is not explained by
anything in this repository's source** and, per the maintainer's own
prior measurement using an already-tuned macOS/CoreAudio configuration
(§0), most likely lives in CoreAudio's host-side buffer negotiation
and/or the USB DAC's own driver latency — neither of which the 2023
callback-rewrite experiment was able to move. The highest-value next
step is measurement (§3), specifically instrumenting
`Pa_GetStreamInfo()` and testing against the Mac's built-in audio
output, before investing in further DSP-side changes whose combined
ceiling (§5) is well under half of the unexplained residual.

**Note: §7 below (added on review) supersedes the resampler figures in
§1 rows 4 and 9, §1.1, §5 item 3, and the residual estimate in this
summary.**

## 7. Review addendum (Claude Fable 5, 2026-07-13): measured r8brain latencies correct the budget

This section records the results of an independent review of the
analysis above. All sampled file:line claims in §0-§6 were verified
correct against the source, with one major exception: the analytical
(Kaiser-formula) resampler latency estimates in §1.1 are wrong by
roughly an order of magnitude, and correcting them reverses the
document's main conclusion about where the ~200 ms lives.

### 7.1 Method

A standalone probe was compiled directly against the
`r8brain-free-src` submodule with the project's exact build defines
(`R8B_EXTFFT=1 R8B_FASTTIMING=1 R8B_PFFFT_DOUBLE=1`,
`CMakeLists.txt:292-295`) and the exact constructor arguments the
wrappers use (`CDSPResampler24(in, out, 65536)` per
`sfmbase/IfResampler.cpp:26-29`; `CDSPResampler(in, out, 65536)` with
library-default `ReqTransBand`/`ReqAtten` per
`sfmbase/AudioResampler.cpp:28-29`). The probe reads
`getInLenBeforeOutStart()` — the same quantity the existing
`DEBUG_IFRESAMPLER`/`DEBUG_AUDIORESAMPLER` macros print (§3 item 2) —
which is the chain's group delay in input samples, i.e. the
steady-state latency per §1.2. No repository file was modified.

### 7.2 Measured latencies

| Configuration | Latency (input samples) | ms |
|---|---|---|
| `IfResampler` as built, RTL-SDR 1152k→384k | 15321 | **13.30** |
| `IfResampler` as built, RTL-SDR 960k→384k | 7318 | 7.62 |
| `IfResampler` as built, Airspy R2 10M→384k | 122005 | 12.20 |
| `AudioResampler` as built, 384k→48k (tb=2, att=206.91) | 29591 | **77.06** |
| `AudioResampler` tb=2, att=140 | 30603 | 79.70 |
| `AudioResampler` tb=2, att=120 | 14513 | 37.79 |
| `AudioResampler` tb=5, att=140 | 7355 | 19.15 |
| `AudioResampler` tb=10, att=120 | 3753 | 9.77 |
| `AudioResampler` tb=15, att=120 | 1833 | **4.77** |
| `AudioResampler` tb=37, att=120 | 957 | 2.49 |
| `CDSPResampler` 1152k→384k, tb=10, att=140 | 1882 | 1.63 |
| `CDSPResampler` 1152k→384k, tb=25, att=120 | 455 | 0.40 |

(tb = `ReqTransBand` in percent, att = `ReqAtten` in dB. The mono and
stereo `AudioResampler` instances run in parallel on the same block,
so the audio-path figure counts once, not twice.)

The §1.1 Kaiser estimates (~1.6 ms and ~14.4 ms) understate the real
figures (13.3 ms and 77.1 ms) by 8x and 5x. r8brain's actual
multi-stage decomposition carries far more group delay than a
minimal-length single FIR would.

### 7.3 Corrected latency budget

For the §1 reference configuration (RTL-SDR, 1152 kHz, blklen 16384):
14.2 (batching) + 13.3 (IfResampler) + 77.1 (AudioResampler) + 1.3
(pilot-cut FIR) + ~0.1 (IIR) + ≤40 (PortAudio request) ≈ **146 ms**.
If the maintainer's ~200 ms measurement was taken with an Airspy HF+
at 384 kHz (no IfResampler, ~5 ms native batching per §1.3), the
software side is still ≈ 123 ms. Either way, **the unexplained
CoreAudio/USB-DAC residual is roughly 55-75 ms, not ~130 ms** as §1
concluded — consistent with the "PortAudio and USB DAC latency is
dominant" note in `CHANGES.md:36` only in the sense that the
*output-side* stack is the largest single block; the single largest
*component* in the whole chain is the `AudioResampler` at 77 ms.

### 7.4 Correction to §5 item 3: the lever is ReqTransBand, not ReqAtten

§5 item 3 proposes lowering `ReqAtten` toward 140 dB. The measurements
show this is ineffective and can even backfire: at tb=2, att=140 is
*slower* (79.7 ms) than the default att=206.91 (77.1 ms), because
r8brain switches internal decompositions non-monotonically. The
effective parameter is the transition band, because the default 2% of
the 24 kHz output Nyquist (480 Hz) forces a very long final-stage
filter. Widening it collapses the latency:

- tb=15, att=120: 4.8 ms — **saves ~72 ms** on the audio path alone.
- Applying loose explicit parameters to the IF path as well (switching
  `IfResampler` from `CDSPResampler24` to a parameterized
  `CDSPResampler`, e.g. tb=10, att=140): 13.3 → 1.6 ms, saving another
  ~12 ms for RTL-SDR configurations.

Combined, **~85 ms of the ~200 ms is recoverable by resampler
parameters alone** — several times the entire ceiling §5 previously
assigned to DSP-side changes. This should displace the CoreAudio work
as the top code-change candidate (it also benefits Linux/RPi, not just
macOS).

Signal-quality feasibility: the FM audio band ends at 15 kHz, and the
19 kHz pilot is removed downstream by the 127-tap pilot-cut FIR
(§1, row 10). With tb=15 the resampler passband still extends to
~20.4 kHz, beyond both. Spectral content that could alias into the
audible 0-15 kHz band lies above 33 kHz, where even 120 dB stopband
attenuation (still below the 16-bit noise floor) applies; the strong
MPX components there (19 kHz pilot images, 23-53 kHz stereo
subcarrier, 57 kHz RDS) are exactly what the stopband must and does
suppress. Aliases can land only in the inaudible 20.4-24 kHz
transition region, which the pilot-cut FIR then attenuates further.
This must still be verified by ear and by spectrum on a real broadcast
(no automated tests exist), and tb should be chosen conservatively —
tb=37 is measurably faster but leaves no margin. A/B recording of the
same broadcast segment via `-W` before and after is the practical
verification method.

### 7.5 New finding: output latency is not stationary (no clock discipline)

The 40 ms of §1 row 14 is the *size* requested for PortAudio's ring
buffer, not necessarily its steady-state occupancy. With the blocking
write API, occupancy — and therefore this latency term — is set by the
startup fill and then drifts with the frequency offset between the
SDR's sample clock and the DAC's clock, because nothing in the chain
disciplines one to the other (there is no adaptive resampling):

- If the DAC clock is effectively faster, the ring drains; occupancy
  and this latency term stay near zero, paid for by occasional benign
  `paOutputUnderflowed` events (`AudioOutput.cpp:303-305`).
- If the SDR clock is effectively faster, the ring fills to its 40 ms
  cap, `Pa_WriteStream()` back-pressures the main loop, and the excess
  then accumulates in `DataBuffer<IQSample>` at the clock-skew rate.
  A ±100 ppm crystal offset corresponds to latency growth of roughly
  0.36 s per hour of continuous reception, bounded only by the
  1024-block drop-oldest cap — about 14.6 s of IQ at the RTL-SDR
  defaults — after which periodic block drops (audible glitches)
  replace further growth.

So the same binary can exhibit anywhere from ~0 to ~40 ms here on
different SDR/DAC pairs, and on a "SDR-faster" pair the end-to-end
latency slowly *increases* during a long session. This is directly
observable with §3 item 3's instrumentation: log
`source_buffer.queue_size()` and `dropped_blocks()` once per status
line over an hours-long run. A monotonically growing queue depth
confirms the SDR-faster case and would also mean any single ~200 ms
measurement is a snapshot of a moving value.

### 7.6 Revised recommendation ranking

1. Measure first, as §3 already says — and add the §7.5 long-run
   `queue_size()` observation to the checklist. The
   `-DDEBUG_IFRESAMPLER -DDEBUG_AUDIORESAMPLER` build will print
   exactly the §7.2 "as built" numbers, confirming the probe.
2. **Widen `AudioResampler`'s transition band** (explicit
   `ReqTransBand`/`ReqAtten` constructor arguments): ~72 ms saving,
   small code change, needs listening verification (§7.4).
3. **Parameterize `IfResampler`** the same way: ~12 ms further saving
   for RTL-SDR and Airspy R2/Mini configurations (§7.4).
4. The macOS CoreAudio work (§5 items 1-2) now targets a residual of
   ~55-75 ms rather than ~130 ms; still worthwhile, but second to the
   resampler change in expected payoff and portability.
5. `blklen=4096` and the other §4 workarounds remain valid as-is.

## 8. AudioResampler alternatives (Claude Fable 5, 2026-07-13)

This section investigates how far the `AudioResampler` latency (77 ms,
§7.2) can be reduced, including replacing r8brain with another
open-source implementation. All figures below are measured, not
estimated.

### 8.1 What the AudioResampler actually has to do

- It is used **only** by `FmDecoder` (`include/FmDecode.h:152-153`).
  `AmDecoder` and `NbfmDecoder` run at `internal_rate_pcm = 48000`
  (`include/AmDecode.h:36`, `include/NbfmDecode.h:34`) and perform no
  audio-rate resampling.
- The ratio is a compile-time constant integer: `sample_rate_if` /
  `sample_rate_pcm` = 384000/48000 = **exactly 8:1**
  (`include/FmDecode.h:38-40`). The project pays for a general
  arbitrary-ratio resampler where a fixed x8 FIR decimator suffices.
- Data is real-valued `double` (`SampleVector`), two independent
  instances (mono and stereo paths) per block. Downstream, the signal
  passes a 127-tap FIR, two IIRs, and is then converted to `float32`
  for PortAudio (`sfmbase/AudioOutput.cpp:297`), so `float32`
  processing precision is not a real regression.
- Quality requirement: the FM audio band ends at 15 kHz; everything
  from 19 kHz up (pilot residue, 23-53 kHz stereo subcarrier, 57 kHz
  RDS) must be suppressed. FM broadcast stereo SNR tops out around
  60-70 dB, so a 120 dB stopband is already generous; r8brain's
  default 206.91 dB targets mastering-grade sample-rate conversion,
  a spec this signal chain cannot benefit from.

### 8.2 Measured real-time latencies of alternatives

Method: a unit impulse is streamed through each resampler in
128-input-sample chunks; the latency is the number of input samples
consumed at the moment the output peak is emitted, minus the impulse
position (granularity 0.33 ms). This is the same quantity as
r8brain's `getInLenBeforeOutStart()`; as a cross-check, the as-built
`AudioResampler` measures 77.33 ms by this method vs. 77.06 ms from
the API. (Note: a naive impulse-position measurement gives 0 ms for
soxr and r8brain because both discard the initial filter delay from
the output *timeline*; that discarding does not reduce the *real-time*
delay, which is what is measured here.) Probes ran on macOS/arm64
against Homebrew libsoxr 0.1.3, speexdsp 1.2.1, and liquid-dsp 1.8.0.

| Implementation, 384k->48k | Latency | License / I/O |
|---|---|---|
| r8brain as built (tb=2%, att=206.91 dB) | **77.1 ms** | project-current, double |
| r8brain tb=15%, att=120 dB (§7.4) | 4.8 ms | 2-line change, double |
| soxr `SOXR_VHQ` linear-phase | 16.0 ms | LGPL-2.1+, double |
| soxr `SOXR_HQ` linear-phase | 7.0 ms | LGPL-2.1+, double |
| soxr `SOXR_VHQ` minimum-phase | 13.3 ms | LGPL-2.1+, double |
| soxr `SOXR_HQ` minimum-phase | 4.7 ms | LGPL-2.1+, double |
| speexdsp quality 10 | 3.0 ms | BSD-3, float32 |
| speexdsp quality 7 | 1.7 ms | BSD-3, float32 |
| speexdsp quality 5 | 1.0 ms | BSD-3, float32 |
| liquid-dsp `msresamp` As=120 dB | 0.67 ms | MIT, float32 |
| liquid-dsp `msresamp` As=80 dB | 0.33 ms | MIT, float32 |
| custom 8:1 polyphase FIR, 15k/24k, 120 dB, 333 taps | 0.67 ms | none needed, any |
| custom 8:1 polyphase FIR, 15k/33k, 120 dB, 167 taps | 0.33 ms | none needed, any |
| custom 8:1 polyphase FIR, 15k/19k, 120 dB, 749 taps | 1.0 ms | none needed, any |

The "custom" rows are single-stage Kaiser-windowed decimating FIRs
(designed and run via liquid-dsp in the probe, but the design is
trivially reproducible offline with scipy/Octave and needs no runtime
library): passband to 15 kHz, stopband from the stated frequency,
executed polyphase (only every 8th output computed). The last row is
special — its stopband starts at 19 kHz, meaning it meets the
*pilot-cut* specification as well, so it can replace both the
`AudioResampler` **and** the downstream 127-tap
`jj1bdx_48khz_fmaudio` filter (§1 row 10) in one stage: 78.4 ms of
audio-path filtering becomes 1.0 ms.

### 8.3 Assessment

- **soxr** is the only library that is a near-drop-in for the current
  API shape (streaming, double I/O, arbitrary ratios), but at 4.7-16 ms
  it is no better than simply re-parameterizing r8brain (4.8 ms), and
  it adds a dependency. Not compelling here.
- **speexdsp** reaches 1-3 ms but is float32-internal with a ~96 dB
  (quality 10) stopband — acceptable against FM SNR, but it is an
  arbitrary-ratio design whose flexibility this fixed 8:1 path never
  uses. Marginal gain over the custom filter, plus a dependency.
- **liquid-dsp / custom polyphase decimator** exploits the structural
  fact of §8.1: with a fixed integer ratio, a precomputed ~170-750 tap
  linear-phase FIR run polyphase achieves 0.3-1.0 ms with an exactly
  specified response. Cost per output sample is one N-tap dot product:
  even the 749-tap variant is 749 x 48000 x 2 channels = 72 MMAC/s,
  negligible next to the rest of the pipeline and almost certainly
  cheaper than r8brain's FFT-based chain. The coefficients can be
  generated offline and checked into `FilterParameters.cpp` exactly
  like `jj1bdx_48khz_fmaudio`, so **no new runtime dependency at
  all**; the implementation is a small decimating variant of the
  existing FIR filter classes in `sfmbase/Filter.cpp` (a `double`
  dot-product kernel via VOLK or a plain loop — the compiler
  auto-vectorizes this shape well under the existing
  `-O3 -ftree-vectorize`).
- Not measured: **libsamplerate** (BSD-2) and **zita-resampler**
  (GPL-3) were not installed locally. Neither has a plausible
  advantage over the custom decimator for a fixed 8:1 ratio;
  libsamplerate is float32 and provides no latency query, and
  zita-resampler's own documentation targets ~3 ms-class delays,
  comparable to speexdsp.
- Minimum-phase options (soxr's `SOXR_MINIMUM_PHASE`) trade waveform
  symmetry for delay but still measure worse than the custom
  linear-phase decimator here, so the phase-distortion trade-off buys
  nothing in this comparison.

### 8.4 Recommendation

1. **Near term** (2-line change, no new code): explicit
   `ReqTransBand`/`ReqAtten` on the existing r8brain resamplers
   (§7.4) — 77.1 -> 4.8 ms.
2. **Best end state** (small, dependency-free code change): replace
   `AudioResampler` in `FmDecoder` with a fixed 8:1 polyphase
   decimating FIR whose coefficients live in `FilterParameters.cpp`.
   With the 15k/19k design it also absorbs the pilot-cut filter,
   taking the audio path's total filtering latency from 78.4 ms to
   1.0 ms — about 6 ms better than option 1 — while *removing* a
   pipeline stage and likely reducing CPU. Needs a decimating-FIR
   class (~50 lines modeled on the existing `LowPassFilterFirAudio`),
   offline coefficient generation, and the usual on-air listening
   verification. `AudioResampler` itself stays untouched for any
   future non-integer-rate use.
3. Adopting soxr, speexdsp, or liquid-dsp as a replacement dependency
   is **not recommended**: none beats the custom decimator, and all
   add a dependency for a task that is structurally a single FIR.

Combined with §7's IfResampler fix (13.3 -> 1.6 ms), `blklen=4096`
(§4), and the corrected budget of §7.3, the achievable software-side
latency for the RTL-SDR default configuration is roughly
3.6 + 1.6 + 1.0 + 0.1 + 40 (PortAudio request) ≈ **46 ms**, at which
point the PortAudio/CoreAudio output stage becomes by far the
dominant remaining term and the §5 macOS work becomes the next
frontier.

## 9. Measured executable-level latency difference: dev vs. dev-resampler-lowlatency (Claude Fable 5, 2026-07-14)

This section measures the latency difference between the release
executables built from `dev` (ce34651) and `dev-resampler-lowlatency`
(c33e03d, the §7.4/§8.4-option-1 parameter change) — i.e. the whole
binary, not isolated library probes. The measurement also uncovered a
build-system fact that corrects the absolute r8brain figures given in
§7 and §8 (see §9.4); the *conclusions* of those sections stand, but
their r8brain millisecond values describe a configuration the shipped
binary does not actually run.

### 9.1 Method

Both binaries were built with the standard release flags
(`-O3 -ftree-vectorize`, macOS/arm64) and fed the identical input: a
6.000 s synthetic FM IQ file (S16LE, 1152 kHz; unmodulated carrier for
the first 1.0 s, then a 1 kHz tone at 75 kHz deviation), via

```
airspy-fmradion -t filesource -m fm \
  -c "filename=fm_onset_1152k_s16.raw,srate=1152000,freq=82500000,raw,format=S16_LE" \
  -W out.wav
```

`FileSource` paces blocks at real time (`sfmbase/FileSource.cpp:434`),
so each run is a faithful 6-second reception. Two observables from the
48 kHz output WAV:

1. **Output-duration deficit** = 6.000 s − WAV duration. Because
   r8brain consumes its filter latency from the output *timeline*
   (§8.2), the delay does not appear as a waveform shift; it appears
   as samples still held inside the resamplers when the input ends.
   The deficit is therefore exactly the steady-state real-time group
   delay of the resampler chain — the quantity this branch changes.
   (The downstream FIR/IIR stages emit one output per input and
   contribute nothing to the deficit.)
2. **Tone-onset position**: verifies that the output timeline is
   unchanged, i.e. the branch alters real-time delay only, not
   content alignment.

### 9.2 Results

| Observable | dev | dev-resampler-lowlatency | difference |
|---|---|---|---|
| WAV frames (48 kHz) | 286053 | 287864 | +1811 |
| WAV duration | 5.95944 s | 5.99717 s | +37.73 ms |
| duration deficit | 40.56 ms | 2.83 ms | **−37.73 ms** |
| 1 kHz onset | 1001.375 ms | 1001.375 ms | 0 |

**The branch removes a measured 37.7 ms of steady-state latency from
the executable** (resampler chain 40.6 ms -> 2.8 ms), with the output
timeline bit-aligned between the two builds (identical onset; the
+1.375 ms vs. the nominal 1000 ms onset is the downstream pilot-cut
FIR group delay plus tone build-up, identical in both).

### 9.3 Cross-validation

Rebuilding both branches with
`cmake -DEXTRA_FLAGS="-DDEBUG_AUDIORESAMPLER -DDEBUG_IFRESAMPLER"`
(the existing debug hooks; no code change) gives the as-built
per-stage figures and a complete sample accounting:

| Stage | dev | dev-resampler-lowlatency |
|---|---|---|
| IfResampler 1152k->384k, `getInLenBeforeOutStart()` | 7129 in-samples = 6.19 ms | 858 = 0.75 ms |
| AudioResampler 384k->48k, `getInLenBeforeOutStart()` | 13207 in-samples = 34.39 ms | 809 = 2.11 ms |
| chain total | 40.58 ms | 2.85 ms |

The per-stage holds sum to the WAV deficits of §9.2 to within
0.02 ms, and the AudioResampler's total output sample count equals
the WAV frame count exactly — every sample is accounted for.

### 9.4 Correction to §7/§8 absolute figures: the `R8B_*` defines never reach the resampler code

The §7/§8 probes were compiled with
`R8B_EXTFFT=1 R8B_FASTTIMING=1 R8B_PFFFT_DOUBLE=1`, copied from
`CMakeLists.txt:294`. But those are
`target_compile_definitions(r8b PUBLIC ...)`, and PUBLIC definitions
propagate only to targets that link `r8b`. The `sfmbase` library —
where `AudioResampler.cpp` and `IfResampler.cpp` instantiate the
(header-only) r8brain templates — does not, so they compile **with no
`R8B_*` defines at all** (verified in `compile_commands.json`). The
shipped binary therefore runs r8brain's built-in FFT and default
sample-timing code path, whose filter design has roughly *half* the
group delay of the with-defines design measured in §7/§8:

| Configuration | §7/§8 probe (with defines) | as built (no defines) |
|---|---|---|
| audio, tb=2.0, att=206.91 (dev) | 77.06 ms | 34.39 ms |
| audio, tb=15.0, att=120.0 (branch) | 4.77 ms | 2.11 ms |
| IF, CDSPResampler24 preset (dev) | 13.30 ms | 6.19 ms |
| IF, tb=10.0, att=140.0 (branch) | 1.63 ms | 0.75 ms |

(Standalone no-defines probe figures; they match the instrumented
executables of §9.3 exactly.)

Consequences:

- §1 rows 4/9 as corrected by §7, §7.2-§7.3, and the r8brain rows of
  §8.2 overstate the as-built latencies about 2x. In the §7.3 budget,
  "software total ≈ 146 ms" becomes ≈ **96 ms**, so the unattributed
  CoreAudio/USB-DAC residual is correspondingly *larger* (~105 ms of
  the observed ~200 ms), strengthening §5/§6's point that the macOS
  output stage is the dominant remaining term.
- The expected gain of this branch was 84 ms by §7/§8 arithmetic; the
  real gain is **37.7 ms**. The relative ranking and every
  recommendation in §8.3-§8.4 are unaffected (the no-defines design is
  uniformly lower-latency, in both branches' favor).
- The comment blocks added in `include/AudioResampler.h` and
  `include/IfResampler.h` on this branch quote the with-defines
  figures (77 -> 4.8 ms, 13.3 -> 1.6 ms); they should be amended to
  the as-built values (34.4 -> 2.1 ms, 6.2 -> 0.7 ms).
- Latent build inconsistency, independent of this branch: `libr8b`'s
  `pffft_double.c` object is linked into the executable but the
  r8brain code instantiated inside `sfmbase` cannot call it
  (`R8B_EXTFFT` is unset there), so the pffft objects are dead weight
  and the three `R8B_*` defines currently configure nothing that
  runs. Either drop them (and the `pffft_double.c` compilation) or
  propagate them to `sfmbase` deliberately — but note the with-defines
  design *costs more latency* in all four configurations above, so
  propagation would be a latency regression and should only be done
  for a measured CPU win. If they are ever propagated to some TUs but
  not others, two different inline definitions of the same r8brain
  symbols would coexist (ODR hazard).

### 9.5 Bottom line

- Measured end-to-end, `dev-resampler-lowlatency` cuts **37.7 ms** of
  steady-state FM reception latency relative to `dev` (resampler
  chain 40.58 ms -> 2.85 ms), with no change to output timing
  alignment or audio content (§9.2; spur check in the branch
  verification: all spurs ≤ −99 dBc).
- With the corrected as-built numbers, the remaining software-side
  terms for the RTL-SDR default configuration are source batching
  (~14 ms), the 40 ms PortAudio request, the pilot-cut FIR (1.3 ms),
  and 2.85 ms of resamplers — the next fronts are `blklen` (§4) and
  the PortAudio/CoreAudio output stage (§5/§6), not the DSP chain.
