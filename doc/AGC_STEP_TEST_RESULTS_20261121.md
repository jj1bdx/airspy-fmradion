# Results: tests of the AGC gain-ratio limit under sudden level changes (2026-11-21)

Plan: doc/AGC_STEP_TEST_PLAN_20261121.md (steps 0 to 6 of its section
7, and its section 5.3 on a reduced set). Branch: `dev-macos-realtime-thread` at `f012f95` (the same code as
`6725655`, which contains the AGC change `e277d87`).
Open item addressed: doc/AGC_Z_FLOOR_REVIEW_20261120.md, section 9,
"The effect of the 20 dB per sample gain step on AF has not been checked
by listening tests."

Status: the file decodes and the measurements are done. The user did
the listening test (plan section 5.3) on a reduced set of 64 clips on
2026-11-21; its results are in section 8.2. The full set of 736 clips
has not been listened to.

Variants (plan section 2.6, option A):

- V0: no limit (the AGC code of `9570a05`).
- V1: limit z ≥ 0.1 (the current code).
- V2: limit z ≥ 0.5.
- VF: `IfSimpleAgc` with a fixed gain of 1; `AfSimpleAgc` as V1.

## 1. Summary

1. FM without the multipath filter (`-E 0`): V1 and V2 audio equals the
   reference within float rounding (difference peak −138.5 dBFS or
   lower) for every event of case 1 and case 2, including silence then
   signal and dropouts. The 20 dB per sample step has no effect on the
   FM audio here.
2. FM with `-E 36`: every level step of +20 dB or more resets the
   multipath filter once, in V0, V1, and V2. After the reset, the
   output differs from the reference for 1.0 s (piano) to 2.3 s
   (off-air recordings), with a difference peak of −7.5 to −11.5 dBFS.
   For steps, V1 and V2 give the same output.
3. FM with `-E 36`, single-sample impulses of +40 dB or more (1f, F4):
   V1 is measurably worse than V2. The V1 gain drops about 20 dB
   (V2: about 6 dB). The difference from the reference is 2.8 to 4.4 dB
   higher in rms and lasts 4.3 to 5.0 s, against 0.3 to 2.7 s for V2.
   This is the only measured case where the 0.1 limit is worse than
   0.5. The listening test confirmed it: V1 was judged worse than V2 in
   all 4 impulse-train pairs (section 8.2).
4. V0 shows the limit cycle the fix prevents: from the event to the end
   of the clip, z ≤ 0 on every sample, and the multipath filter resets
   on every block (938 resets in 5 s). The sign changes reach the audio
   even with `-E 0` (difference peak +2.5 to +6.5 dBFS). V1 and V2
   have no limit cycle and no negative gain in any decode.
5. AM: no decode in any variant had z ≤ 0, including V0. The limit
   acted on at most one sample per clip in V1, in 10 of 82 clips (tone
   program only), and never in the AF AGC. V1 and V2 give nearly the
   same AF level after each event. The AF level dips after a step
   (−4.5 dB at +30 dB, −7 to −9 dB at +40 dB and above, for 20 to
   40 ms) happen in V1 and V2 alike and come from the AGC time
   constants, not from the limit.
6. Two plan predictions did not hold: the AM IF limit acts much less
   often than the thresholds in plan section 1 predict (section 6.2),
   and VF is not a usable reference for `-E 36` or for AM at levels
   other than 0 dBFS (section 6.1).
7. Listening test (64 clips, one listener): V1 and V2 got the same
   answers in all 24 pairs of level steps (FM and AM) and AM impulses.
   In the 4 pairs of FM `-E 36` impulse trains, V1 had a click or a
   period of multipath distortion that V2 did not have. No case was
   found where 0.5 is worse than 0.1 (section 8.3).

## 2. What was run

- Builds (plan step 0): a scratchpad worktree at `f012f95` with two
  compile-time macros, written by the cpp-expert agent and never
  committed. `AGC_TEST_VARIANT` selects V0, V1, V2, or VF;
  `AGC_TEST_TRACE` prints, to stderr, one record per 256 samples per
  AGC instance (minimum, maximum, and end gain; counts of clamped z,
  of unclamped z ≤ 0, of `max_gain` clamps, and of non-finite resets),
  and one record per multipath filter reset. Without the macros the
  code is the current code.
- Build checks, on `piano_iqtest.wav` with FM, FM `-E 36`, and AM, all
  byte-identical:
  - the build without macros against `build/airspy-fmradion`;
  - V1 with the trace against the build without macros (the trace does
    not change the audio);
  - V0 against an unmodified build of `9570a05`.
- Signals (plan step 2): float32 I/Q WAV files at 384 kHz, 8 s each,
  event at 3.0 s, 0 dBFS = rms |x| of 1 (the AGC target), written by a
  script from the numpy-expert agent and reviewed by the python-expert
  agent. 190 clips: 36 per FM source (`piano_iqtest.wav` from 2 s,
  `AirSpy_20260727_125800Z_89700kHz_IQ.wav` from 30 s,
  `interfm-20260724102822z-iq.wav` from 20 s), and 41 per AM program
  (1 kHz tone, and the FM decode of the piano file), with the AM carrier
  at DC.
- Decodes (plan steps 3 to 5): every clip with every variant; FM clips
  with `-E 0` and `-E 36`, AM clips with `-m am`; float WAV output
  (`-G`), `-q`, never `-P`. 1192 decodes in 605 s, 16 in parallel, no
  failures, no error messages.
- Analysis (plan step 6): metrics per decode, from the trace and from
  the difference d = output − reference. References: for gain-only
  events, the event-free clip decoded by the same binary; for gated
  events (silence then signal, dropouts), the VF decode of the same
  clip. "Transient length" is the time from the event until |d| stays
  below −60 dBFS (1 ms windows). "Settle time" is the time from the
  event until the gain stays within 1 dB of the gain of the event-free
  reference decode (256-sample resolution).

Departures from the plan:

- V0 is the current code with the AGC code of `9570a05`, not a build of
  `9570a05` itself, so that only the AGC differs between variants. The
  byte-identical check above shows that the two give the same output.
- Listening clips are converted from the float decodes, not decoded
  again with `-W`.
- Stereo pilot loss was not measured: the decodes ran with `-q`.
- Additions: AM A1 at 0 dBFS, A2 at 30% modulation (A2m30). The
  impulse trains (F4, A4) have one impulse per second at 3 to 7 s. The
  1e square wave lasts 2 s and starts with the low level.

## 3. Case 1: FM, IF AGC only

Counts are per decode unless stated. "Sources" means the three FM
recordings.

### 3.1 When the limit acts (V1, FM_IF)

| Event | Clamped samples per decode | Plan prediction |
|---|---|---|
| 1b +20, +30 dB | 0 | none below +39.5 dB |
| 1b +40 dB | 1 (piano, interfm), 0 (airspy897) | threshold +39.5 dB |
| 1b +50 dB | 1 | — |
| 1b +60 dB | 1–2 | — |
| 1b +80 dB | 2–3 | — |
| 1a −80 dBFS | 0 | from about −60.5 dBFS |
| 1a −60 dBFS | 0–1 | — |
| 1a −40, −20, 0 dBFS | 1–2, 2–3, 3–4 | — |
| 1d 1, 10, 100 ms | 0 | from about 120 ms |
| 1d 300, 1000 ms | 4 | — |
| 1f +40, +60 dB | 0–1, 1 | — |
| 1e (all) | 0 | — |
| 1g −110 dBFS | 0 | — |

The thresholds match the plan. In 1e the gain cannot recover between
transitions (it rises at 333 dB/s, about 17 dB in a 50 ms half period),
so no transition reaches the threshold.

V1 FM_IF settle time after a 1b step: median 20.0 ms, maximum 20.7 ms
(36 decodes); the plan predicts about 20 ms.

### 3.2 `-E 0`

- V1 and V2: difference peak −138.5 dBFS or lower in every 1a to 1g
  decode, against the reference or against VF. The plan's pass
  criterion (below −90 dBFS) is met for 1b, 1c, 1e, 1f, and 1g, and
  also for the gated events 1a and 1d.
- V0: the limit cycle appears in 1a at −40, −20, and 0 dBFS, 1b at
  +50 dB and above, 1c at −60 dB (on the return step), and 1d at 300 ms
  and above, in every source. The difference peak is +2.5 to +6.5 dBFS
  until the end of the clip. Below these levels V0 has single sign
  changes, each giving one click of about 2 ms (difference peak −17 to
  −27 dBFS): 1a −60 dBFS
  (one source), 1c −40 dB, 1f +40 and +60 dB.

### 3.3 `-E 36`

| Event | Multipath resets (V1) | Transient length (V1) | Notes |
|---|---|---|---|
| 1a −80 to 0 dBFS | 1 at the start of the signal | — | all levels, V0 to V2 (V0: 938 from −40 dBFS) |
| 1b +20 to +80 dB | 1 | 1.0 s (piano), 2.2 s (airspy897), 2.3 s (interfm) | V1 and V2: same d rms and length; V0: 1 up to +40 dB, 938 from +60 dB (piano from +50 dB) |
| 1c −20, −40, −60 dB | 1 on the return; interfm −60: 2 | 3.2–4.3 s | V1 = V2 |
| 1d 1, 10 ms | 0 | — | |
| 1d 100, 1000 ms | 1 | — | |
| 1d 300 ms | 1–2 (V2: 2), 5.3 ms apart | — | two consecutive blocks |
| 1e 10 Hz | 25–27 in 2 s | 3.9–4.3 s | V0 = V1 = V2 |
| 1e 100 Hz | 200 (one per up-step) | 2.9–4.3 s | V0 = V1 = V2 |
| 1e 1000 Hz | 0 | — | |
| 1f +20 dB | 0–1 | | |
| 1f +30 dB | 1 | 0.3–2.3 s | V1 = V2 |
| 1f +40, +60 dB | 1 | 4.3–5.0 s (V2: 0.3–2.7 s) | see section 3.4 |
| 1g −110 dBFS | 0 | — | output differs from the reference from the start |

- One reset per step of +20 dB or more, and no repeated resets after a
  single step, as the plan expects. Steps between +11 and +20 dB were
  not tested.
- The transient length for gated events (1a, 1d) is not given: the VF
  reference differs from the adaptive variants already before the event
  (section 6.1).
- 1g: the IF gain is held at `max_gain`, so the multipath filter input
  is at about 0.32 instead of 1. The output differs from the reference
  from the start of the clip (up to −3.2 dBFS), with no resets.
- VF: after any 1b step the multipath filter resets on every block
  (938 resets). The multipath filter does not work without the IF AGC.

### 3.4 V1 against V2

- Steps (1a, 1b, 1d): V1 has fewer clamped samples than V2 (for
  example 1a 0 dBFS: 3–4 against about 11), the same or shorter settle
  time, and no larger undershoot (all 24 1b and 30 1a comparisons).
  With `-E 36`, the rms and length of the difference from the
  reference are the same.
- Impulses with `-E 36` (1f +40 and +60 dB, F4):

| Clip | V1 gain dip | V2 gain dip | d rms V1 / V2 | Transient V1 / V2 |
|---|---|---|---|---|
| piano 1f +40 | 20.0 dB | 6.0 dB | −49.0 / −51.9 dBFS | 4.3 / 0.3 s |
| airspy897 1f +40 | 14.9 dB | 5.3 dB | −44.0 / −46.8 dBFS | 4.8 / 2.2 s |
| interfm 1f +40 | 20.3 dB | 6.3 dB | −40.1 / −44.5 dBFS | 5.0 / 2.7 s |
| piano F4 +60 | 20.1 dB | 6.1 dB | −40.9 / −43.6 dBFS | 4.8 / 4.4 s |
| interfm F4 +60 | 20.3 dB | 6.3 dB | −34.3 / −38.9 dBFS | to end / 5.0 s |

  Both variants reset the multipath filter once per impulse. After the
  reset, the V1 gain is still about 20 dB low for up to 60 ms, while
  the filter converges again, and the output stays farther from the
  reference. With `-E 0` the impulses have no effect on the audio in
  either variant.

## 4. Case 2: FM

Case 2 FM reuses the case 1 clips (F1 = 1b +40/+60/+80, F2 = 1a
−40/−20/0, F3 = 1d 300/1000 ms) and adds F4 and F5.

- F1: V1 clamps 0–3 samples (plan: 1–3; 0 for airspy897 at +40 dB).
  One multipath reset with `-E 36`.
- F2, F3: with `-E 0`, V1 equals VF within float rounding. The IF
  overshoot does not reach the audio, as the plan expects.
- F4 (+40, +60 dB, five impulses): five multipath resets with `-E 36`,
  one per impulse, in V1 and V2. V0 at +60 dB entered the limit cycle
  in one of three sources (751 resets with `-E 36`); another source had
  191 resets.
- F5 (−80 dBFS noise floor): with `-E 0`, the step clips differ from
  the noisy reference by −86 to −94 dBFS, because the step changes the
  SNR; this is expected. With `-E 36`, one reset per step. In F5_1d
  the multipath filter also resets during the gate, when only noise is
  present (300 ms gate: at 280 ms; 1000 ms gate: 9 resets between 280
  and 1000 ms), in V1 and V2 alike. The AGC raises the gain until the
  noise is at the target level, about 240 ms for 80 dB, and the filter
  then fails on the noise. This is not related to the limit.

## 5. Case 2: AM

### 5.1 When the limits act

| Variant | IF clamped samples (clips) | AF clamped samples (clips) | z ≤ 0 (IF, AF) |
|---|---|---|---|
| V0 | — | — | 0, 0 |
| V1 | 10 (10) | 0 (0) | 0, 0 |
| V2 | 40 (20) | 12 (12) | 0, 0 |
| VF | — | 8 (8) | —, 8 (clamped by the V1 law) |

- V1 clamps one IF sample in these tone clips only: A1 at −60 dBFS and
  above, A2, A5, A2m30, and A6_A2 at +60 dB, and A3 and A6_A3 at
  1000 ms. It never clamps in the music clips.
- V0 and V1 outputs differ in 29 of 82 decodes, by at most 0.14 dB in
  the rms of the difference from the reference. V0 never changed sign
  in AM in these tests.

### 5.2 Settle times and AF level

- IF settle time (V1 and V2): 56 ms after A1, A2, and A5 events (plan:
  about 55 ms). For A3 the settle time includes the dropout.
- AF settle time: 19 to 51 ms after A2 steps (plan: about 16 ms). The
  AF AGC follows the IF recovery.
- AF level dip (10 ms rms of output over reference, minimum, and time
  below −1 dB), V1 / V2:

| Event | Dip V1 | Dip V2 | Time below −1 dB |
|---|---|---|---|
| A2 +20 dB | −1.8 dB | −1.8 dB | 0–20 ms |
| A2 +30 dB | −4.5 dB | −4.5 dB | 0–30 ms |
| A2 +35 dB | −5.6 dB | −5.6 dB | 10–30 ms |
| A2 +40 dB | −7.0 dB | −6.5 dB | 10–40 ms |
| A2 +60 dB | −7.1 dB | −8.3 dB | 20–40 ms |
| A5 +60 dB (m = 1.0) | −7.5 dB | −8.7 dB | 20–50 ms |
| A2m30 +60 dB (m = 0.3) | −4.7 dB | −6.1 dB | 0–30 ms |
| A3 300 ms | −6.4 dB | −6.4 dB | 20–30 ms |
| A3 1000 ms | −7.9 dB | −8.6 dB | 30–40 ms |
| A1 0 dBFS | −7.9 dB | −8.6 dB | 20–40 ms |
| A4 +20 to +40 dB | −0.2 dB or less | same | 0 |
| A4 +60 dB (five impulses) | −8.9 dB | −8.9 dB | 300–400 ms in total |

- A4 +60 dB: VF has the same dip depth (−9.0 dB) for a shorter time
  (100–120 ms). The dip comes from the AF AGC reacting to the impulse,
  and the IF AGC (max_gain is +3.5 dB) cannot compensate it, as the
  plan expects.
- A6 (noise floor) gives the same numbers as A2 and A3 within 0.6 dB.
- The IF and AF limits never acted in the same window in V1.

## 6. Plan expectations that did not hold

### 6.1 VF as a reference

VF is a valid reference only where the IF input is at the AGC target
and the IF gain does not matter:

- FM `-E 0`: valid (the demodulator ignores magnitude).
- FM `-E 36`: not valid. The multipath filter output depends on the IF
  level, so VF differs from the adaptive variants from the start
  (difference up to −35 dBFS before the event). After a level step VF
  resets the multipath filter on every block.
- AM: valid only at 0 dBFS (A3, A1 at 0 dBFS). For A1 below 0 dBFS
  the VF output is lower by the level of the signal.

The automated check "difference before 2.9 s is zero" failed only for
these VF-referenced decodes and for 1g; all decodes with a
same-binary reference passed.

### 6.2 AM IF limit thresholds

The plan predicts that the AM IF limit acts for steps above +34.8 dB
and for signals above −85 dBFS after silence. In V1 it acted in 4 of
the 14 A1 (−80 to −20 dBFS) and A2 (+35 to +60 dB) decodes where the
plan predicts a clamp, on one sample each, and in 10 of all 82 AM
decodes. It never acted in the music program. The trace shows that
the gain still falls by the full step within the first 256 samples
(for example 1 → 0.021 after a +60 dB step) with at most one clamped
sample. The most likely cause is that in AM the IF AGC runs at 48 kHz,
after the 384 to 48 kHz IF resampler, whose low-pass filter spreads the
step over several samples, so that no single sample reaches the
threshold. This has not been checked in the code. The plan's
predictions for FM, where the IF AGC runs at the input rate, held.

## 7. Answers to the plan's questions

- Is the 20 dB per sample step (V1) worse than V2 or VF at the same
  event? In FM without the multipath filter, and in AM: no measurable
  difference. In FM with `-E 36`, for steps: no difference, measured or
  heard. In FM with `-E 36`, for single-sample impulses of +40 dB or
  more: yes, V1 gives a 2.8 to 4.4 dB larger difference from the
  reference, lasting about 2 s longer, and the difference was heard in
  all 4 pairs tested (section 8.2). VF was not in the listening set.
- Impulse noise (A4, F4): in AM, impulses of +60 dB dip the AF level by
  up to 9 dB in every variant. In FM with `-E 36`, every impulse of
  +30 dB or more resets the multipath filter in every variant. These
  are effects of the AGC and multipath filter design, not of the
  choice of limit.

## 8. Listening test

### 8.1 Packages

- Location: work/agc-test/listen/ (untracked). 736 clips of 5 s
  (61.3 minutes): the case 2 events for V0, V1, V2, and VF, plus the
  reference clips as controls. FM clips are stereo, AM clips are mono,
  48 kHz, S16. Each clip starts 2 s before the event.
- 304 clips were scaled down so that their peak is −1 dBFS; 70 are
  marked loud (rms above −10 dBFS after the event, mostly V0 limit
  cycles). No clip had NaN or Inf.
- Files: `README.md`, `questions.csv`, and the answer sheet
  `key/answers.csv` (do not open before listening). The file names are
  random (seed 20261121).
- `make_listening.py` has filters (`--sources`, `--settings`,
  `--variants`, `--cases`, `--programs`) to build a smaller set.
- Reduced set, the one listened to: work/agc-test/listen-small/
  (untracked), built with `--sources piano,interfm --programs
  tone,music --settings E36,am --variants v1,v2 --cases F1,F4,A2,A4
  --seed 1121`. 64 clips (5.3 minutes): FM `-E 36` F1 (1b +40, +60,
  +80 dB) and F4 (impulse trains of +40 and +60 dB), AM A2 (steps of
  +20 to +60 dB) and A4 (impulse trains of +20 to +60 dB), and the
  matching reference clips, each for V1 and V2. 24 clips were scaled
  to a −1 dBFS peak (the AM step clips, by 6.5 to 11.3 dB); none was
  marked loud. The seed differs from the full set, so names cannot be
  looked up in the full set's key.

### 8.2 Results

The user listened once to all 64 clips, with the file names
randomized and without the key, and filled in
`listen-small/questions.csv`. The answers, matched with the key:

| Group | V1 | V2 | Same answer for V1 and V2 |
|---|---|---|---|
| Reference clips (8) | no click, no dip, no distortion | the same | yes |
| AM A2, steps +20 to +60 dB (10 pairs) | click in all | click in all | 10 of 10 |
| AM A4, impulses +20 to +60 dB (8 pairs) | click in all | click in all | 8 of 8 |
| FM F1, steps, piano (3 pairs) | click | click | 3 of 3 |
| FM F1, steps, interfm (3 pairs) | nothing heard | nothing heard | 3 of 3 |
| FM F4, impulses, interfm +40 and +60 dB (2 pairs) | click | nothing heard | 0 of 2 |
| FM F4, impulses, piano +40 and +60 dB (2 pairs) | click, and multipath distortion for 0.1 s | click | 0 of 2 |

No dip or pumping was reported in any clip.

### 8.3 Evaluation

- The controls were clean: no answer for the 8 reference clips
  reported anything, so the clicks heard come from the events.
- Level steps (FM F1, AM A2) and AM impulses (A4): V1 and V2 got the
  same answers in all 24 pairs. This agrees with the measurements, in
  which V1 and V2 give the same output for steps with `-E 36` and
  nearly the same AF level in AM. For level steps, the 20 dB per
  sample step is not audibly worse than the 0.5 limit.
- FM `-E 36` impulse trains (F4): V1 was judged worse in all 4 pairs,
  either with a click that V2 did not have (interfm) or with a period
  of multipath distortion that V2 did not have (piano). In these clips
  the measured rms of the V1 difference from the reference is 2.7 to
  4.6 dB higher than for V2 (section 3.4). The measured difference is
  audible.
- The AM clicks at steps and impulses occur in both variants, so they
  do not come from the limit. The measured AF level dips after AM
  steps (up to −7 to −9 dB, 20 to 40 ms) were not reported as dips;
  they are probably too short to be heard apart from the click.
- No measurement or answer showed 0.5 worse than 0.1. Both limits keep
  the gain from changing sign. With 0.5 the limit acts on about three
  times as many samples after a large step, with no measurable effect
  on the audio at `-E 0`, the same multipath filter reset count at
  `-E 36`, and the same AM output. On this evidence, raising
  `min_ratio` to 0.5, at least in `IfSimpleAgc`, is justified. The AF
  AGC limit almost never acts, so its value matters little. The
  choice is the user's.
- Limits of this evidence: one listener, one pass, no repeated or ABX
  trials; only 4 pairs show the V1/V2 difference; the impulses are
  synthetic single samples at 384 kHz, not recorded impulse noise; no
  limit between 0.1 and 0.5 was tested; V0 and VF were not in the
  listening set.

## 9. Not done and limitations

- Listening to the full set of 736 clips (V0, VF, `-E 0`, the
  airspy897 source, cases F2, F3, F5, A1, A3, A5, A6, A2m30).
- A test with recorded impulse noise and `-E 36`, and a limit between
  0.1 and 0.5.
- Stereo pilot loss (`-v` events).
- Real hardware (plan section 6).
- Steps between +11 and +20 dB with `-E 36`.
- The cause given in section 6.2 is not confirmed in the code.
- CW (optional in the plan) was not tested.

## 10. Files

All untracked, in work/agc-test/:

- `agc-test-variants-trace.diff`: the variant and trace code (applies
  to `f012f95`); `TRACE_FORMAT.md`: macros, build commands, trace
  format.
- `gen_signals.py`: the signal generator.
- `run_decodes.sh`: the decode runner.
- `analyze.py`: the analysis; output in `results/metrics.csv` (one row
  per decode) and `results/summary.md` (tables and the automated
  checks).
- `make_listening.py`, `listen/`, and `listen-small/`: the listening
  packages; `listen-small/questions.csv` holds the user's answers.

The builds, the 190 signal clips (4.4 GB), and the decodes are in the
session scratchpad, which is not kept. They can be rebuilt from the
files above.
