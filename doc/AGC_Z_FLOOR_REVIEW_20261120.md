# Review of AfSimpleAgc / IfSimpleAgc against the Tisserand–Berviller digital AGC

Date: 2026-11-20
Project: airspy-fmradion
Files reviewed: `AfSimpleAgc.h`, `AfSimpleAgc.cpp`, `IfSimpleAgc.h`, `IfSimpleAgc.cpp`, `SoftFM.h`
Reference: E. Tisserand, Y. Berviller, "Design and implementation of a new digital automatic gain control", Electronics Letters, 52 (22), pp. 1847–1849, 2016. hal-01397371.

## 1. Summary

- The per-sample recursion in both classes matches the software AGC in the paper (Fig. 4, eq. (5)), including the order in which output and gain update are computed.
- The paper's first-order Taylor approximation of the gain ratio stops being valid for large overshoots. With no lower limit on z, the gain can become zero or negative, or alternate in sign with growing magnitude. A float32 simulation reproduced a limit cycle that never ends.
- Fix: limit z to at least a fixed constant of 0.1 in both classes. Patches are in section 8.

## 2. The algorithm in the paper

The paper's analog derivation gives the instantaneous gain

g(t) = exp( K ∫ (Yref² − y²(t)) dt ),  y(t) = g(t)·x(t),  K = 1 / (2·τ·Yref²)

where τ is the time constant of the level-estimation filter (Fig. 2).

Digital version, sampling period Ts = 1/Fs:

- Eq. (4): g_k = g_{k−1} · exp( K·Ts·(Yref² − y²_{k−1}) )
- With Yref = 1 and τ ≫ Ts, the paper sets K·Ts = Ts/(2τ) = d ≪ 1 and uses the first-order Taylor expansion
- Eq. (5): g_k = g_{k−1} · (1 + d·(1 − y²_{k−1}))

Rise rate for a low-level input: 8.68·d·Fs dB/s (= 8.68/(2τ) dB/s).

### Definition of d

- Ts is the sampling period, Ts = 1/Fs.
- d = K·Ts holds only with Yref = 1, which the paper chooses. In general the exponent is (Ts/2τ)·(1 − y²/Yref²), so d = K·Ts·Yref² = Ts/(2τ) = 1/(2·τ·Fs).
- Check against the paper's numbers:
  - d = 1e-4 at 44.1 kHz gives τ = 0.113 s (paper: 0.11 s).
  - d = 1e-3 at 44.1 kHz gives a rise rate of 383 dB/s (paper: 380 dB/s).
- In the code, `rate` (`m_distortion_rate`) is d. It is set per sample, so the same value gives τ = 1/(2·d·Fs) at each sampling rate. For example, d = 1e-4 gives τ ≈ 104 ms at 48 kHz and ≈ 13 ms at 384 kHz.

## 3. Comparison with the implementation

| Item | Paper | Code | Result |
|---|---|---|---|
| Recursion | g_k = g_{k−1}(1 + d(1 − y²_{k−1})) | `y = x·g`; output y; `g *= 1 + d(1 − y²)` | Matches |
| Gain used for output | g_k, computed from y_{k−1} | output uses g before the update | Matches |
| Yref | eq. (4) general, Yref = 1 in practice | AF: loop at level 1, output × `m_reference` | Mathematically equivalent to eq. (4) |
| Complex samples (IF) | not covered | `std::norm(y)` = I² + Q² | Correct complex form; regulates mean \|y\|² to 1 |
| Steady state | mean y² = 1 | 1 kHz sine, d = 1e-4: output RMS 1.0002 | Matches |
| `max_gain` clamp | not in paper | present | Reasonable addition; without it the gain overflows float after tens of seconds of silence |
| Non-finite reset | not in paper | present | Reasonable addition |

Notes:
- `AfSimpleAgc::get_current_gain()` returns the loop gain without the `m_reference` factor.
- The Taylor form settles at E[log z] = 0, not E[1 − y²] = 0. That lowers the level by roughly d/2·E[(1 − y²)²], which is negligible for d around 1e-4.
- In `IfSimpleAgc` the original z line used double literals (`1.0`), so it was computed in double and narrowed to float. The patch changes these to `1.0f`.
- Float z has a step size of about 1.2e-7 above 1 and about 6e-8 below 1. That is fine for d ≥ 1e-5. For d ≲ 1e-6, rounding affects tracking and can shift the settled level.

## 4. The problem: z can become zero or negative

Let u = d·(y² − 1), so the linear ratio is z = 1 − u. The exact eq. (4) ratio is exp(−u), which is always positive.

| Output level | z | Behaviour |
|---|---|---|
| 1 < y² < 1 + 1/d | 0 < z < 1 | Normal decay, but near the upper end the gain can drop by up to ~80 dB in one sample (exp would give ~4 dB) |
| y² = 1 + 1/d | 0 | Gain becomes 0 and stays there (0·z = 0) |
| 1 + 1/d < y² < 1 + 2/d | −1 < z < 0 | Gain changes sign; the `> max_gain` check does not catch negative values |
| y² > 1 + 2/d | z < −1 | \|g\| grows with alternating sign until clamped, giving a limit cycle |

Thresholds for |y|:

| Condition | \|y\| | d = 1e-3 | d = 1e-4 | d = 1e-5 |
|---|---|---|---|---|
| z = 0.5 | √(1 + 0.5/d) | 22.4 | 70.7 | 224 |
| z = 0 (sign change) | √(1 + 1/d) | 31.6 | 100.0 | 316 |
| \|z\| = 1 (divergence) | √(1 + 2/d) | 44.7 | 141.4 | 447 |

Rule of thumb: the problem starts at |y| ≈ 1/√d, which is 10·log₁₀(1/d) dB above the target. For the input, this is a sudden increase of about 1/√d in amplitude relative to the level the gain had settled for. After silence, with the gain at `max_gain`, it starts when |x| > 1/(√d·max_gain).

Simulation (float32, d = 1e-4, max_gain = 1e5): 3 s of silence, then a DC input of 0.01. The gain alternated between −9.9e6 and +1e5 indefinitely, and the output between about +1000 and −99000. The non-finite reset never fired, because each large positive gain was cut back to `max_gain`. With DC 0.001 the gain collapsed by 80 dB in one sample and needed about 2 s to recover.

## 5. Definitions used

The target output level is 1 (Yref = 1). The test case is a sudden increase in input level while the gain is still set for a quieter signal.

- **Overshoot**: the output is above the target because the gain has not yet come down. The first sample after a jump always passes at full size, because the gain update is one sample late; no lower limit on z can change it. Measured as:
  - peak |y|
  - duration: number of samples with |y| > 10 (more than 20 dB above target)
  - excess energy: Σ max(y² − 2, 0) in section 7.2, Σ max(y² − 1, 0) in section 7.3
- **Undershoot**: while correcting an overshoot, the gain falls too far and the output goes below the target. It then recovers only at the rise rate, 8.68·d·Fs dB/s. For a constant input it happens on any step where z < 1/|y|. Measured as the lowest gain relative to the settled gain (negative dB = undershoot).
- In control-theory terms, a gain that goes past its final value on the way down would be called overshoot of the gain. In this document it is called undershoot of the output.

## 6. Lower limit on z

### Hard limit

z > 0. Any limit in (0, 1) keeps the loop stable: when the limit is active the gain stays positive and decreases.

### Practical limit for a DC step

The ideal one-sample correction is z = 1/|y|. The unclamped linear z stays above 1/|y| until |y| reaches y\*, where y·(1 − d(y² − 1)) = 1. Any limit z_min ≥ z\* = 1/y\* prevents undershoot for a constant input.

| d | y\* | z\* | √d |
|---|---|---|---|
| 1e-3 | 31.13 | 0.0321 | 0.0316 |
| 1e-4 | 99.50 | 0.01005 | 0.0100 |
| 1e-5 | 315.7 | 0.00317 | 0.00316 |

So z\* ≈ √d, slightly above it.

DC step check (d = 1e-4, gain at 1e5):

| Limit | Undershoot |
|---|---|
| 0.0101 | none (lowest output 1.009) |
| 0.005 | output 0.5 (−6 dB), about 0.16 s to recover |
| 0.001 | output 0.1 (−20 dB), about 0.5 s to recover |

This bound only ensures that the next gain is at least 1/|x_k|, i.e. correct for the current sample. For signals with peaks it does not prevent undershoot (section 7.3).

## 7. Simulations (float32, Fs = 48 kHz)

### 7.1 Where each limit takes effect

The limit only acts when |y| > √(1 + (1 − z_min)/d):

- z_min = 0.5: |y| > 70.7 (d = 1e-4), > 22.4 (d = 1e-3)
- z_min = 0.1: |y| > 94.9 (d = 1e-4), > 30.0 (d = 1e-3)

Normal operation, the steady-state level and the rise rate are unaffected. The rise rate is 41.7 dB/s for d = 1e-4 and 417 dB/s for d = 1e-3.

### 7.2 Limit 0.5 versus 0.1 (DC step)

| | 0.5 | 0.1 |
|---|---|---|
| Largest gain drop per sample | −6 dB | −20 dB |
| Samples with \|y\| > 10, d = 1e-4 (+40/+60/+100 dB) | 47 / 50 / 57 (≈1.0–1.2 ms) | 2 / 3 / 5 (0.04–0.10 ms) |
| Same, d = 1e-3 | 5 / 9 / 15 (0.10–0.31 ms) | 2 / 3 / 5 (0.04–0.10 ms) |
| Excess energy | reference | about 25–45% lower |
| Undershoot | none | none |
| Settling to within 1 dB | ≈164 ms (d = 1e-4), ≈16.4 ms (d = 1e-3) | same |

Settling time is set by the final approach near y ≈ 1, with a time constant of about 1/(2d) samples (≈104 ms at d = 1e-4). The lower limit has no effect on it.

### 7.3 Limit 1.1·√d

| d | Limit | Largest drop per sample |
|---|---|---|
| 1e-3 | 0.0348 | −29.2 dB |
| 1e-4 | 0.0110 | −39.2 dB |
| 1e-5 | 0.00348 | −49.2 dB |

DC step:
- Excess energy comes down almost to the first sample alone. For d = 1e-4 at +40 dB: 1.10e4, against 3.30e4 (0.1) and 4.84e4 (0.5).
- No undershoot. For comparison, 0.9·√d undershot by −0.92 dB.
- Burst length is usually 1–3 samples. It can be longer when the first step lands just above |y| = 10, where z is close to 1 (10–18 samples at d = 1e-4).

Signals with peaks (lowest gain relative to settled gain):

| Input, d | 0.5 | 0.1 | 1.1·√d |
|---|---|---|---|
| Sine sweep, d = 1e-3 | −0.31 dB | −0.31 dB | −2.13 dB |
| Sine sweep, d = 1e-4 | 0.00 dB | 0.00 dB | −0.42 dB |
| Sine sweep, d = 1e-5 | 0.00 dB | 0.00 dB | −2.14 dB |
| Gaussian noise, d = 1e-3 | −0.71 dB | −0.64 dB | −3.80 dB |

The sine sweep covered 100 Hz–15 kHz, 13 phases and steps of +30 to +100 dB. The −0.3 to −0.7 dB values for 0.5 and 0.1 at d = 1e-3 are normal gain ripple at that loop speed.

Cause: when the limited sample is a peak, the new gain is correct for that sample but too low for the signal's RMS. The undershoot can be as large as the crest factor: up to 3 dB for a sine and 12–20 dB for music. The worst cases came at 15 kHz, where one limited peak sample sets the gain.

### 7.4 Choice of limit

- **IF:** an FM IQ signal has a nearly constant envelope, and the demodulator ignores amplitude. Even 1.1·√d would work.
- **AF:** music has a high crest factor. 1.1·√d could reduce the level by several dB after a loud peak, with an audible recovery of 0.05–0.5 s. 0.1 showed no undershoot and makes the overshoot about 10× shorter than 0.5.
- **Decision:** use a fixed constant of 0.1 in both classes.

## 8. Patches

Both compile with `g++ -std=c++17 -Wall -Wextra` with no warnings.

Test: 3 s of silence (gain goes to `max_gain` = 1e5), then a DC (AF) or complex (IF) input of 0.01, d = 1e-4.

| Class | Output | Settles at | Gain |
|---|---|---|---|
| AfSimpleAgc | 1000 → 100 | 1.0000–1.00003 | 100 |
| IfSimpleAgc | 1000 → 100 | 1.00015 (+0.0013 dB, float rounding of z) | 100.015 |

Apply with `patch -p1` or `git apply` from the directory containing the files.

### 8.1 AfSimpleAgc

```diff
--- a/AfSimpleAgc.h
+++ b/AfSimpleAgc.h
@@ -36,10 +36,17 @@
 class AfSimpleAgc {
 public:
   // Construct AF AGC: initial_gain and max_gain are gain values, reference is
-  // the target output level, rate is the gain-change rate factor.
+  // the target output level, rate is the gain-change rate factor
+  // (d = Ts / (2 * tau) in the paper).
   AfSimpleAgc(const double initial_gain, const double max_gain,
               const double reference, const double rate);
 
+  // Lower limit of the per-sample gain ratio z = 1 + rate * (1 - y^2).
+  // Without this limit, z becomes zero or negative when
+  // y^2 >= 1 + 1 / rate, which makes the gain zero, negative, or divergent.
+  // 0.1 limits the gain drop to 20 dB per sample.
+  static constexpr double min_ratio = 0.1;
+
   // Reset AGC gain to the initial_gain.
   void reset_gain();
 
--- a/AfSimpleAgc.cpp
+++ b/AfSimpleAgc.cpp
@@ -25,6 +25,8 @@
     // Initialize member fields
     : m_initial_gain(initial_gain), m_max_gain(max_gain),
       m_reference(reference), m_distortion_rate(rate) {
+  // A positive gain stays positive only if the ratio floor is positive.
+  assert(m_initial_gain > 0.0);
   reset_gain();
 }
 
@@ -43,6 +45,12 @@
     Sample x2 = x * m_current_gain;
     samples_out[i] = x2 * m_reference;
     double z = 1.0 + (m_distortion_rate * (1.0 - (x2 * x2)));
+    // Lower limit of z: the first-order Taylor approximation of
+    // exp(d * (1 - y^2)) is only valid for small d * |1 - y^2|.
+    // Without the limit, a large overshoot makes z <= 0.
+    if (z < min_ratio) {
+      z = min_ratio;
+    }
     m_current_gain *= z;
     // Check if m_current_gain is finite
     if (!std::isfinite(m_current_gain)) {
```

### 8.2 IfSimpleAgc

```diff
--- a/IfSimpleAgc.h
+++ b/IfSimpleAgc.h
@@ -37,9 +37,15 @@
 class IfSimpleAgc {
 public:
   // Construct IF AGC (target level 1.0): initial_gain, max_gain, and rate, the
-  // gain-change rate factor.
+  // gain-change rate factor (d = Ts / (2 * tau) in the paper).
   IfSimpleAgc(const float initial_gain, const float max_gain, const float rate);
 
+  // Lower limit of the per-sample gain ratio z = 1 + rate * (1 - |y|^2).
+  // Without this limit, z becomes zero or negative when
+  // |y|^2 >= 1 + 1 / rate, which makes the gain zero, negative, or divergent.
+  // 0.1 limits the gain drop to 20 dB per sample.
+  static constexpr float min_ratio = 0.1f;
+
   // Reset AGC gain to the initial_gain.
   void reset_gain();
 
--- a/IfSimpleAgc.cpp
+++ b/IfSimpleAgc.cpp
@@ -25,6 +25,8 @@
     // Initialize member fields
     : m_initial_gain(initial_gain), m_max_gain(max_gain),
       m_distortion_rate(rate) {
+  // A positive gain stays positive only if the ratio floor is positive.
+  assert(m_initial_gain > 0.0f);
   reset_gain();
 }
 
@@ -43,7 +45,13 @@
     IQSample x = samples_in[i];
     IQSample x2 = x * m_current_gain;
     samples_out[i] = x2;
-    float z = 1.0 + (m_distortion_rate * (1.0 - std::norm(x2)));
+    float z = 1.0f + (m_distortion_rate * (1.0f - std::norm(x2)));
+    // Lower limit of z: the first-order Taylor approximation of
+    // exp(d * (1 - |y|^2)) is only valid for small d * |1 - |y|^2|.
+    // Without the limit, a large overshoot makes z <= 0.
+    if (z < min_ratio) {
+      z = min_ratio;
+    }
     m_current_gain *= z;
     // Check if m_current_gain is finite
     if (!std::isfinite(m_current_gain)) {
```

## 9. Open items

- The `rate` and `max_gain` values used at the call sites in airspy-fmradion were not part of this review. Whether the original failure could happen in practice depends on them (section 4).
- The effect of the 20 dB per sample gain step on AF has not been checked by listening tests.
