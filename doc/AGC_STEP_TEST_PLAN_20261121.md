# Plan: tests of the AGC gain-ratio limit under sudden level changes (2026-11-21)

Branch: `dev-macos-realtime-thread` at `6725655` (contains the AGC
change `e277d87`).
Background: doc/AGC_Z_FLOOR_REVIEW_20261120.md, section 9, open item:
"The effect of the 20 dB per sample gain step on AF has not been checked
by listening tests."

Revision (2026-11-21, same day): case 2 now covers FM broadcast as well
as AM (section 5); variant V3 is described in section 2 so that the user
can decide whether to include it. The first version called V3 "the
ideal reference"; the simulation in section 2.3 shows that it is not.

Decision (the user, 2026-11-21): option A of section 2.6. V3 is left
out; the variants are V0, V1, V2, and VF. Section 2 is kept as the
record of why.

Status (2026-11-21): steps 1 to 6 of section 7 are done, with the
departures listed in section 2 of the results document. The listening
test of section 5.3 was done on a reduced set of 64 clips (V1 and V2
only). Results: doc/AGC_STEP_TEST_RESULTS_20261121.md.

Goals:

1. Case 1: how extreme changes of the IF signal level affect the IF AGC
   and the output in FM broadcast reception.
2. Case 2: how sudden level changes that make the gain-ratio limit act
   affect the IF and AF AGCs and the audio output, in FM and AM.

## 1. Facts from the code that shape the plan

1. FM has no AF AGC. `AfSimpleAgc` is used only in `AmDecode.cpp` (AM,
   DSB, USB, LSB, CW, WSPR). In FM the only AGC is `IfSimpleAgc`
   (`FmDecode.cpp`, `m_ifagc(1.0, 100000.0, 0.0001)`). In FM, "AF"
   in the open item means the FM audio output as affected by the IF
   AGC.
2. The FM demodulator ignores the magnitude of the IQ samples:
   `PhaseDiscriminator` uses `atan2`. It does not ignore the sign: a
   gain that changes sign adds a phase jump of π, which the
   discriminator turns into a full-deviation spike. A gain of exactly
   zero gives `atan2(0, 0) = 0`, which is silence. So in FM, the IF
   AGC's 20 dB per sample step can reach the audio only through the
   multipath filter (`-E`), but a sign change (V0) or a zero gain (V3,
   section 2) reaches it directly.
3. The multipath filter reports failure, and `FmDecoder` resets it, when
   the filter output exceeds `divergence_limit` (10), or when
   |1 − |y|²| > 10, that is, |y| > 3.3. So with `-E`, an IF step of about
   +11 dB or more should reset the filter, whether or not the z limit
   is reached.
4. `reset_gain()` is called only from the constructor and when the gain
   is not finite. A gain of exactly zero is finite and never recovers:
   0 × z = 0.
5. `IfSimpleAgc` computes in float, `AfSimpleAgc` in double.
6. Values at the call sites (thresholds from the review's formulas; the
   settle times agree with the simulation in section 2.3):

| AGC | Rate | d | max_gain | Limit acts when | From silence, input above | Rise rate | Settle to 1 dB |
|---|---|---|---|---|---|---|---|
| FM IF | 384 kHz | 1e-4 | 1e5 | step > +39.5 dB | 9.5e-4 (−60.5 dBFS) | 333 dB/s | ≈20 ms |
| AM IF | 48 kHz | 3e-4 | 1e6 | step > +34.8 dB | 5.5e-5 (−85 dBFS) | 125 dB/s | ≈55 ms |
| AM AF | 48 kHz | 1e-3 | 1.5 | AF input > ≈20 | — | 417 dB/s | ≈16 ms |

A normal AM signal cannot reach the AF AGC limit. It can be reached
during an IF AGC overshoot: the first sample after a +40 dB step passes
at about 100 times the target level through the AM demodulator and the
60 Hz DC block. That is the case where both AGCs react.

## 2. Variant V3: the exact exp() form (left out)

### 2.1 What V3 is

V3 replaces the first-order update of eq. (5) with eq. (4) of the
paper, without the Taylor approximation and without the lower limit:

```
// V1 (current):  z = 1 + d * (1 - y^2);  if (z < 0.1) z = 0.1;
// V3:            z = exp(d * (1 - y^2));
```

One line in each of `IfSimpleAgc.cpp` (`std::exp` on float) and
`AfSimpleAgc.cpp` (double). The exponential is always positive, so the
gain cannot change sign, and no lower limit seems necessary. The cost
is one `exp()` per sample: about 0.4 million calls per second for FM,
which is small next to the rest of the FM chain.

### 2.2 What V3 was meant to show

The first version of this plan treated V3 as the ideal AGC: the
difference V1 − V3 would isolate what the linear form plus the 0.1
limit costs, compared with the AGC law as the paper derives it. That
holds only if V3 behaves well for large steps. It does not.

The gain drop per sample in V3 is 4.34 · d · (y² − 1) dB. It grows with
the square of the overshoot, while the ideal one-sample correction is
20 · log₁₀(y) dB. For a large overshoot V3 removes far more gain than
needed:

- d = 1e-4, y = 1000 (+60 dB): exponent −100, gain drop 434 dB in one
  sample.
- The float `exp()` underflows to a subnormal number below an exponent
  of about −87, and to zero below about −104. Double `exp()`
  underflows to zero below about −745.
- A subnormal gain of a few units of 1.4e-45 cannot rise: multiplying
  it by z ≈ 1.0001 rounds back to the same value. A zero gain stays
  zero (fact 4).

### 2.3 Simulation

C programs in `work/agc-sim/` (untracked): `agcsim.c` in float and
`agcsim_d.c` in double, with their outputs in `agcsim-output.txt` and
`agcsim_d-output.txt`. `agcsim_d.c` was made from `agcsim.c` by
replacing `float` with `double` and the float math functions with their
double forms; only its AM AF rows are used below, because the IF AGC
computes in float. Build with `cc -O2 -o agcsim agcsim.c -lm` (the same
for `agcsim_d`). Each one runs the per-sample recursion of
the code, with the `max_gain` clamp and the non-finite reset, on a
constant-envelope input for 10 s after the step. "Step" is from a
settled gain (output at the target level); "from max" is from the gain
at `max_gain` after silence. The table gives the lowest gain relative
to the final gain, and the time to settle within 1 dB.

| Event | FM IF (float, d = 1e-4) | AM IF (float, d = 3e-4) | AM AF (double, d = 1e-3) |
|---|---|---|---|
| Step +30 dB | same as V1 | same as V1 | same as V1 |
| Step +40 dB | no undershoot, 20.6 ms (47 samples above 10, V1: 1) | same as V1 | −46.8 dB, 126 ms |
| Step +50 dB | −36.9 dB, 128 ms | −211 dB, 1.73 s | −819 dB, 1.98 s |
| Step +60 dB | −808 dB, gain stuck at a subnormal value, never settles | gain 0, permanent | gain 0, permanent |
| Step +80 dB | gain 0, permanent | gain 0, permanent | gain 0, permanent |
| From max, −60 dBFS input | same as V1 | gain 0, permanent | — |
| From max, −40 dBFS input | −808 dB, 2.44 s | gain 0, permanent | — |
| From max, −20 dBFS input | gain 0, permanent | gain 0, permanent | — |

V1 in the same runs: no undershoot in any row, settling in 20.5 ms (FM
IF), 54.5 ms (AM IF), and 15–16 ms (AM AF), with 1–4 samples above 10
except where the first step lands just above 10 (about 45 samples for
FM IF at +50 dB). V0 changed sign and never settled in every row from
+40 dB (FM IF from +50 dB), which matches the review.

Consequences for the receiver, if V3 were the product code:

- FM: a step of +60 dB or more, or a signal of −20 dBFS after silence,
  sets the IF gain to zero. The output is then silent until the program
  restarts (fact 2).
- AM: any IF step of +60 dB or more, and any signal above −60 dBFS
  after silence, does the same. In case 2 the IF overshoot also reaches
  the AF AGC, which V3 would set to zero as well.

### 2.4 What including V3 would add

- Within its working range (FM and AM IF steps up to +40 dB, AF steps
  up to +30 dB) V3 and V1 give nearly the same gain trajectory. V1 has
  a shorter overshoot at +40 dB. Listening clips there would compare
  two nearly equal outputs.
- Outside that range V3 fails, in a different way from V0 (a dead
  receiver instead of a limit cycle). This can be shown with the trace
  counters alone; listening clips would be silence.
- So V3 is not usable as a reference for judging the 20 dB step. It is
  useful only as evidence that the paper's exact form also needs a
  limit for large steps.

Cost of including V3: one more worktree build with a one-line change
in each class (cpp-expert), about one third more decode runs, and one
more clip per event in the listening package.

### 2.5 A reference that does what V3 was meant to do: VF

For the questions "what would the audio be with no AGC artifact", a
fixed-gain build is the better reference:

- VF: `IfSimpleAgc` applies a constant gain of 1 and does not adapt;
  `AfSimpleAgc` is unchanged. The test signals are generated so that,
  outside a gate, the IQ level is at the AGC target (|x| = 1).
- For gain-only events (steps, fades, impulses), the reference is the
  clip without the event, decoded by V1, as before. VF is needed only
  for events that change the content of the signal: silence followed
  by signal, and dropouts (1a, 1d, F2, F3, A1, A3).
- In FM with `-E 0`, VF output equals V1 output except where V1 has
  its overshoot, because the demodulator ignores magnitude. VF matters
  for FM with `-E 36` and for AM.

### 2.6 Options and decision

- A (recommended): leave V3 out; add VF as the reference for gated
  events. Variants: V0, V1, V2, VF.
- B: include V3 only as a trace-only run (no listening clips) on the
  section 4 and section 5 events, to document where the exact form
  fails. Variants: V0, V1, V2, VF, and V3 without listening.
- C: include V3 in everything, as the first version planned. Not
  recommended: about half of its large-step clips would be silent, and
  the rest would match V1.

Decision (the user, 2026-11-21): option A. The rest of this plan uses
V0, V1, V2, and VF only.

## 3. Shared setup (step 0)

- Builds, all in scratchpad git worktrees with r8brain copied in;
  `build/` is not touched:
  - V0: `9570a05`, no limit.
  - V1: `6725655`, limit 0.1 (the current code).
  - V2: limit 0.5.
  - VF: fixed IF gain (section 2.5).
- A compile-time `AGC_TEST_TRACE` flag in those builds, never
  committed: per block, the minimum and maximum gain and the clamp count
  of each AGC, and a multipath filter reset counter, printed to stderr.
  cpp-expert writes it.
- Signals: a Python script (numpy and soundfile are present) writes
  float32, 2-channel, 384 kHz WAV files. Each clip is about 8 s: 3 s
  before the event, the event, then 5 s after.
- Decodes: `-q -G` (float WAV) to measure overshoot without S16
  clipping, and `-W` (S16) for listening. Run in parallel batches in the
  foreground, never with `-P`.
- Reference: for gain-only events, the same clip with no level change,
  decoded with the same binary; for gated events, the VF decode. Each
  result is measured as the difference from its reference.

## 4. Case 1: extreme IF changes in FM broadcast (IF AGC only)

Sources: `piano_iqtest.wav` (clean), and
`AirSpy_20260727_125800Z_89700kHz_IQ.wav` and
`interfm-20260724102822z-iq.wav` (real off-air signals with multipath).
Each clip is decoded with `-E 0` and `-E 36`.

| ID | Event | Values | What it tests |
|---|---|---|---|
| 1a | Exact silence (≥0.5 s, gain reaches 1e5), then signal | −80, −60, −40, −20, 0 dBFS | Clamp starts at about −60 dBFS; V0 limit cycle |
| 1b | Step up from a settled level | +20, +30, +40, +50, +60, +80 dB | Clamp threshold at +39.5 dB |
| 1c | Step down (fade), then back up | −20, −40, −60 dB, each held 1 s | Recovery at 333 dB/s; return step from a raised gain |
| 1d | Dropout to zero, then back | 1, 10, 100, 300 ms, 1 s | Simulates a USB drop; predicted clamp from about 120 ms up |
| 1e | Square-wave amplitude change, 40 dB depth | 10, 100, 1000 Hz | Repeated triggering (flutter, pulsed interference) |
| 1f | Single-sample impulses on the signal | +20, +30, +40, +60 dB | Gain dip of up to 20 dB, then 60 ms recovery |
| 1g | Signal below the gain ceiling | −110 dBFS | `max_gain` reached; the multipath filter sees amplitude < 1 |

Measurements:

- Difference from the reference: length, peak, and energy of the
  transient.
- NaN count.
- Output peak and clip count.
- Stereo pilot loss (the `-v` stereo events).
- From the trace: clamp count, minimum gain, time to settle within
  1 dB, and multipath filter resets.

Expected results and pass criteria:

- `-E 0`: in 1b, 1c, 1e, 1f, and 1g the IQ is only rescaled, so the
  V1 audio should match the reference within float rounding (difference
  below −90 dBFS). If it does not, something other than magnitude is
  involved, and that is a finding in itself.
- `-E 36`: one multipath filter reset per step of +11 dB or more. The
  audio returns to the reference within the filter's convergence time.
  No repeated resets.
- V1 (current code): no limit cycle in any case. The gain settles
  within the times in the table in section 1.
- V0 (no limit): should show the limit cycle in 1a, 1b (+50 dB and
  up), and 1d. The sign changes reach the audio even with `-E 0`
  (fact 2), and with `-E 36` the multipath filter should reset
  continuously. This shows what the fix prevents.
- V1 against V2: V1 should have a shorter overshoot (1–5 samples above
  10, against about 50 for V2) and no added undershoot.

## 5. Case 2: sudden changes that make the gain-ratio limit act (FM and AM)

Case 1 covers the full range of events, most of them below the limit
threshold, and measures the IF AGC. Case 2 uses only events at or above
the threshold, measures their effect on the audio, and supplies the
clips for the listening test (section 5.3).

### 5.1 FM broadcast (IF AGC only)

Sources: the same three recordings as case 1, chosen for program
content: `piano_iqtest.wav` for music, the two off-air recordings for
real program audio with multipath. Each clip is decoded with `-E 0` and
`-E 36`, in stereo (the default).

| ID | Event | Values | Expected |
|---|---|---|---|
| F1 | Step up from a settled level | +40, +60, +80 dB | V1: clamp for 1–3 samples; with `-E 36`, one multipath filter reset |
| F2 | Exact silence (≥0.5 s), then signal | −40, −20, 0 dBFS | The same, from max gain |
| F3 | Dropout to zero, then back | 300 ms, 1 s | Gain reaches max during the dropout, so the return step clamps |
| F4 | Single-sample impulses | +40, +60 dB, one per second for 5 s | 20 dB gain dip per impulse, 60 ms recovery; possible multipath filter reset per impulse |
| F5 | F1 and F3 with a fixed noise floor | complex Gaussian noise at −80 dBFS added after scaling | Models a real receiver, where a fade also lowers the SNR; the step then changes both level and noise |

Expected results:

- `-E 0`, V1: no audible change from the reference for F1 and F4
  (magnitude only). F2 and F3 are compared with VF; any difference is
  the IF overshoot, which the demodulator should ignore.
- `-E 36`, V1: each reset sets the multipath filter back to its initial
  coefficients, so the multipath distortion of the off-air recordings
  returns until the filter converges again. This is the main audible
  effect expected in FM; the listening test measures how long it lasts
  and whether it is noticeable.
- V0: loud spikes from the sign changes, with and without `-E`.
- V2: the same as V1 except a longer overshoot (about 50 samples); with
  `-E 36` the reset count should be the same, one per event.

### 5.2 AM (IF and AF AGCs)

The signal is synthesized AM IQ at 384 kHz: a carrier with 30%, 80%,
and 100% modulation. Two programs are used:

- Music: the float decode of `piano_iqtest.wav`.
- A 1 kHz tone, for measurement.

Each clip is decoded with `-m am`. CW is optional, since it uses
different rates (6e-4 and 1.25e-3).

| ID | Event | Values | Expected |
|---|---|---|---|
| A1 | Silence (IF gain at 1e6), then AM signal | −80, −60, −40, −20 dBFS | Both AGCs clamp from about −85 dBFS |
| A2 | Step up from a settled carrier | +20, +30, +35, +40, +60 dB | IF clamp from +34.8 dB; AF clamp expected from about +30 dB (to be confirmed) |
| A3 | Dropout to zero, then back | 10, 100, 300 ms, 1 s | IF gain rises 125 dB/s, so the return step clamps for longer dropouts |
| A4 | Single-sample impulses on the carrier (static, ignition noise) | +20, +30, +40, +60 dB | IF gain dip of up to 20 dB, lasting about 160 ms. The AF AGC cannot make up for it (max_gain is +3.5 dB) |
| A5 | A2 at 100% modulation | — | IF undershoot after a clamped peak, up to the crest factor (6 dB) |
| A6 | A2 and A3 with a fixed noise floor | as F5 | As F5, for AM |

Measurements:

- The same as case 1.
- The AF level against time after the event: depth and length of any
  dip.
- The AF AGC's lowest gain and its recovery time.
- Whether the IF and AF clamps happen in the same samples (from the
  trace).

### 5.3 Listening test (the open item; the user listens)

- S16 clips, 2 s before and 3 s after each event in sections 5.1 and
  5.2, for V0, V1, V2, and VF. The file
  names are randomized and come with an answer sheet. FM clips are
  stereo at 48 kHz; AM clips are mono at 48 kHz.
- Before handing them over, the peak of every clip is scanned and
  limited. V0 can produce loud garbage, and the first-sample overshoot
  can reach full scale.
- Questions per clip:
  - Is a click audible?
  - Is there a dip, pumping, or (FM with `-E 36`) a period of
    multipath distortion after the event, and how long does it last?
  - Is V1 different from VF?
- Main question: is the 20 dB per sample step (V1) audibly worse than
  V2 or VF at the same event? Whether impulse noise (A4, F4) causes
  audible artifacts is a second finding, unrelated to the choice of
  limit.

## 6. Optional: real hardware

With an Airspy on FM broadcast and a step attenuator, or by
disconnecting and reconnecting the antenna, watch IF level changes of
±40 dB. The user runs this, because it needs a live SDR. File tests
cover the same cases and can be repeated, so do this only if the file
results show something.

## 7. Order and cost

1. Variant builds and trace code (cpp-expert).
2. Signal script.
3. Case 1: about 25 clips × 2 `-E` settings × 3 variants (V0, V1, V2),
   plus VF for the gated events, at 8 s each in real time. In parallel
   batches, about 15–20 minutes.
4. Case 2 FM: about 15 clips × 2 `-E` settings × 4 variants, about
   10 minutes.
5. Case 2 AM: about 25 clips × 4 variants, about 10 minutes.
6. Analysis script and listening package.
7. A results document in `doc/`, which updates the open item in the
   review. Written only when the user asks.
