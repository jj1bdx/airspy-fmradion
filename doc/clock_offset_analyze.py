#!/usr/bin/env python3
"""Estimators for audio-interface sample-clock offset measurement.

This module holds all of the numerics for `clock_offset_measure.py` and
nothing that touches hardware, so a capture saved as .npz can be
re-analysed later, on any machine, without the audio interfaces present.

Three estimators are computed per sample rate.

  E1  DAC rate vs the host clock, from a straight-line fit of PortAudio's
      per-callback `outputBufferDacTime` against the cumulative frame
      index.
  E2  ADC rate vs the host clock, likewise from `inputBufferAdcTime`.
  E3  the DAC/ADC frequency ratio, from the phase slope of the recorded
      tone.  This one never looks at the host clock at all.

E3 measures p_dac - p_adc: a tone synthesised as f0 relative to the DAC's
nominal rate comes out of the DAC at f0*(1+d), and measuring it against
the ADC's nominal rate divides by (1+a), so the observed offset is
d - a to first order.  Three measurements of two unknowns leaves one
degree of freedom, which is spent on a chi-square consistency check --
the strongest evidence available that a run is trustworthy, since E1/E2
and E3 share no noise source.

Standalone usage:
    clock_offset_analyze.py capture_48000.npz
"""

from __future__ import annotations

import math
import sys
from dataclasses import dataclass, field

import numpy as np

# A straight-line fit is only meaningful once the stream has settled, so
# this many seconds are dropped from each end of every timestamp series.
EDGE_TRIM_SECONDS = 1.0

# Huber tuning constant: 95% efficiency against Gaussian residuals.
HUBER_C = 1.345

# Largest offset the unwrap guard must survive.  Real interfaces sit
# within a few hundred ppm; 1000 ppm is a generous margin.
PPM_MAX = 1000.0


# ---------------------------------------------------------------------------
# Straight-line rate fit (E1, E2)
# ---------------------------------------------------------------------------


@dataclass
class RateFit:
    """Result of fitting a device's sample rate against the host clock."""

    rate_hz: float
    rate_se_hz: float
    nominal_hz: float
    ppm: float
    ppm_se: float
    n_used: int
    n_total: int
    resid_rms_frames: float
    resid_max_frames: float
    autocorr_flagged: bool
    hac_inflation: float
    asymmetry: float

    def __str__(self) -> str:
        return (
            "%12.4f Hz  %+9.3f +/- %.3f ppm   %5d blocks  "
            "resid %7.3f frames rms (max %.1f)"
            % (
                self.rate_hz,
                self.ppm,
                self.ppm_se,
                self.n_used,
                self.resid_rms_frames,
                self.resid_max_frames,
            )
        )


def _bartlett_hac_variance(x: np.ndarray, resid: np.ndarray, w: np.ndarray,
                           sxx: float, lags: int | None = None) -> float:
    """Newey-West variance of a weighted least-squares slope.

    PortAudio timestamps advance in DMA-buffer quanta, so the fit
    residuals are strongly serially correlated and the textbook OLS
    standard error understates the true uncertainty, sometimes by an
    order of magnitude.  Bartlett-weighted HAC repairs that.
    """
    n = len(x)
    if lags is None:
        lags = max(int(4.0 * (n / 100.0) ** (2.0 / 9.0)), 1)
    lags = min(lags, n - 1)
    g = w * x * resid
    s = float(np.sum(g**2))
    for j in range(1, lags + 1):
        s += 2.0 * (1.0 - j / (lags + 1.0)) * float(np.sum(g[j:] * g[:-j]))
    return max(s, 0.0) / sxx**2


def _autocorr_flagged(resid: np.ndarray, max_lag: int = 20,
                      n_sigma: float = 2.0) -> bool:
    """True if the residuals show significant serial correlation."""
    n = len(resid)
    if n < 4 * max_lag:
        return False
    r = resid - resid.mean()
    denom = float(np.sum(r**2))
    if denom <= 0.0:
        return False
    band = n_sigma / math.sqrt(n)
    for k in range(1, max_lag + 1):
        if abs(float(np.sum(r[k:] * r[:-k])) / denom) > band:
            return True
    return False


def fit_rate(frames: np.ndarray, times: np.ndarray, nominal_hz: float,
             n_iter: int = 8) -> RateFit:
    """Fit true sample rate from callback timestamps.

    The frame counter is exact and the timestamp is the noisy quantity,
    so the unbiased regression is time-on-frames; the rate is the
    reciprocal of the fitted slope.  Regressing the other way round --
    treating the noisy timestamp as the exact regressor -- attenuates the
    slope by roughly var(timestamp noise)/var(timestamp), which is small
    but not negligible at the 0.01 ppm level.
    """
    n = np.asarray(frames, dtype=np.float64)
    t = np.asarray(times, dtype=np.float64)
    n_total = len(n)
    if n_total < 16:
        raise ValueError("need at least 16 callbacks to fit a rate, got %d" % n_total)

    # Drop the startup and shutdown transients, where the buffer fill
    # level is not yet in steady state.
    keep = (t >= t[0] + EDGE_TRIM_SECONDS) & (t <= t[-1] - EDGE_TRIM_SECONDS)
    if keep.sum() < 16:
        keep = np.ones(n_total, dtype=bool)
        keep[:3] = keep[-3:] = False
    n, t = n[keep], t[keep]

    # Centre both axes: the timestamps are seconds since an arbitrary
    # epoch and can be 1e4 s or more, whose squares would lose about six
    # decimal digits to cancellation against a span of only tens of
    # seconds.  Centring makes every summed product O(span^2).
    n0, t0 = n.mean(), t.mean()
    x, y = n - n0, t - t0

    weights = np.ones_like(x)
    slope = intercept = 0.0
    for _ in range(n_iter):
        sw = np.sqrt(weights)
        # lstsq is SVD-based; forming the normal equations here would
        # square an already large condition number for no benefit.
        design = np.vstack([x * sw, sw]).T
        slope, intercept = np.linalg.lstsq(design, y * sw, rcond=None)[0]
        resid = y - (slope * x + intercept)
        scale = 1.4826 * float(np.median(np.abs(resid - np.median(resid))))
        if scale <= 0.0:
            break
        u = np.abs(resid) / scale
        new_weights = np.where(u <= HUBER_C, 1.0, HUBER_C / np.maximum(u, 1e-12))
        if np.allclose(new_weights, weights, atol=1e-3):
            weights = new_weights
            break
        weights = new_weights

    resid = y - (slope * x + intercept)
    sxx = float(np.sum(weights * x**2))
    dof = max(float(weights.sum()) - 2.0, 1.0)
    var_ols = float(np.sum(weights * resid**2)) / dof / sxx
    var_hac = _bartlett_hac_variance(x, resid, weights, sxx)
    var_slope = max(var_ols, var_hac)

    rate = 1.0 / slope
    # Delta method: d(1/s)/ds = -1/s^2, so se(rate) = rate^2 * se(slope).
    rate_se = rate**2 * math.sqrt(var_slope)

    # Residuals are in seconds; frames are the natural unit to report.
    resid_frames = resid * rate
    # If glitches only ever delay a callback, Huber downweights one tail
    # and can bias the slope.  Report the imbalance so the caller can see
    # it rather than discovering it as a mystery ppm shift.
    downweighted = weights < 0.5
    asymmetry = (
        float(np.mean(np.sign(resid_frames[downweighted])))
        if downweighted.any()
        else 0.0
    )

    return RateFit(
        rate_hz=rate,
        rate_se_hz=rate_se,
        nominal_hz=nominal_hz,
        ppm=(rate / nominal_hz - 1.0) * 1e6,
        ppm_se=rate_se / nominal_hz * 1e6,
        n_used=int(np.sum(weights > 0.05)),
        n_total=n_total,
        resid_rms_frames=float(np.sqrt(np.mean(resid_frames**2))),
        resid_max_frames=float(np.max(np.abs(resid_frames))),
        autocorr_flagged=_autocorr_flagged(resid),
        hac_inflation=math.sqrt(var_hac / var_ols) if var_ols > 0 else 1.0,
        asymmetry=asymmetry,
    )


# ---------------------------------------------------------------------------
# Tone phase-slope fit (E3)
# ---------------------------------------------------------------------------


def choose_tone_frequency(rate: float, target: float = 3000.0) -> float:
    """Pick a test tone that divides the sample rate exactly.

    An exact submultiple is not required by the phase-slope estimator,
    but it keeps the demodulated image at -2*f0 landing on a null of the
    block-averaging filter (see `choose_decimation`), which removes the
    largest systematic term in the phase fit.
    """
    rate_i = int(round(rate))
    best = None
    for m in range(2, rate_i // 200):
        if rate_i % m:
            continue
        f0 = rate_i / m
        if best is None or abs(f0 - target) < abs(best - target):
            best = f0
    return float(best if best is not None else target)


def choose_decimation(rate: float, f0: float, block_seconds: float = 0.01) -> int:
    """Decimation factor for the demodulated tone.

    Two constraints.  The unwrap in the phase fit needs the per-block
    phase step to stay well inside pi, which caps D at

        D < rate / (4 * f0 * ppm_max * 1e-6)

    with a factor-of-two margin built in.  Separately, D is snapped to a
    multiple of rate/f0 so that the block average nulls the -2*f0 image
    exactly.
    """
    m = int(round(rate / f0))
    d = m * max(1, int(round(rate * block_seconds / m)))
    d_max = rate / (4.0 * f0 * PPM_MAX * 1e-6)
    while d > d_max and d > m:
        d -= m
    return max(int(d), 1)


@dataclass
class ToneFit:
    """Result of measuring the recorded tone's frequency offset."""

    f0_hz: float
    f_measured_hz: float
    ppm: float
    ppm_se: float
    n_blocks: int
    decimation: int
    resid_rms_rad: float
    snr_db: float
    level_dbfs: float

    def __str__(self) -> str:
        return (
            "%12.6f Hz (nominal %.1f)  %+9.3f +/- %.3f ppm   "
            "%5d blocks  phase resid %.5f rad rms"
            % (
                self.f_measured_hz,
                self.f0_hz,
                self.ppm,
                self.ppm_se,
                self.n_blocks,
                self.resid_rms_rad,
            )
        )


def fit_tone(x: np.ndarray, rate: float, f0: float,
             decimation: int | None = None,
             guard_seconds: float = 0.5) -> ToneFit:
    """Measure a recorded tone's frequency by unwrapped-phase regression.

    Complex-demodulate at the nominal tone frequency, average into
    blocks, and fit a straight line to the unwrapped phase.  The slope is
    the residual frequency, whose ratio to f0 is the DAC/ADC offset.

    Blocks are weighted by |z|^2, which is the Fisher information of a
    block's phase estimate; this is the maximum-likelihood combination
    and degrades gracefully through dropouts, where a hard amplitude
    threshold would either keep a corrupted block or discard a usable
    one.
    """
    x = np.asarray(x, dtype=np.float64)
    if decimation is None:
        decimation = choose_decimation(rate, f0)

    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    rms = float(np.sqrt(np.mean(x**2))) if len(x) else 0.0
    level_dbfs = 20.0 * math.log10(rms) if rms > 0 else -np.inf
    if peak >= 0.999:
        raise ValueError(
            "recording is clipped (peak %.4f full scale); reduce --amplitude "
            "or the interface input gain" % peak
        )
    if rms < 1e-5:
        raise ValueError(
            "recording is silent (%.1f dBFS rms); check the cable and that the "
            "output device is actually connected to the input device" % level_dbfs
        )

    n = np.arange(len(x), dtype=np.float64)
    z = x * np.exp(-2j * math.pi * f0 * n / rate)
    usable = (len(z) // decimation) * decimation
    blocks = z[:usable].reshape(-1, decimation).mean(axis=1)
    t = (np.arange(len(blocks)) * decimation + decimation / 2.0) / rate

    # The recording is deliberately longer than the tone, so it opens and
    # closes with silence.  Those blocks carry uniformly random phase, and
    # because unwrapping is sequential a single random excursion there
    # shifts every later sample by a whole cycle -- a bias, not just
    # noise, which amplitude weighting alone cannot undo.  Restrict to
    # the longest contiguous run where the tone is actually present
    # before unwrapping anything.
    magnitude = np.abs(blocks)
    threshold = 0.3 * float(np.median(np.sort(magnitude)[-max(len(magnitude) // 10, 1):]))
    present = magnitude > threshold
    start, best_start, best_len = 0, 0, 0
    for i in range(len(present) + 1):
        if i == len(present) or not present[i]:
            if i - start > best_len:
                best_start, best_len = start, i - start
            start = i + 1
    if best_len < 16:
        raise ValueError(
            "no continuous stretch of tone found in the recording; check that "
            "the output device is cabled to the input device"
        )
    blocks = blocks[best_start : best_start + best_len]
    t = t[best_start : best_start + best_len]

    # Discard the edges of that run, where the tone is ramping in or out.
    keep = (t > t[0] + guard_seconds) & (t < t[-1] - guard_seconds)
    if keep.sum() < 16:
        raise ValueError("recorded tone too short for a phase fit")
    blocks, t = blocks[keep], t[keep]

    phase = np.unwrap(np.angle(blocks))
    weights = np.abs(blocks) ** 2
    t0 = t.mean()
    xc = t - t0
    sw = np.sqrt(weights)
    design = np.vstack([xc * sw, sw]).T
    slope, intercept = np.linalg.lstsq(design, phase * sw, rcond=None)[0]
    resid = phase - (slope * xc + intercept)

    dof = max(len(t) - 2, 1)
    sigma2 = float(np.sum(weights * resid**2)) / dof
    sxx = float(np.sum(weights * xc**2))
    var_ols = sigma2 / sxx
    # Phase residuals are as serially correlated as the timestamp ones --
    # room reflections and interface filtering are both slowly varying --
    # so the same HAC correction applies here.
    var_hac = _bartlett_hac_variance(xc, resid, weights, sxx)
    slope_se = math.sqrt(max(var_ols, var_hac))

    df = slope / (2.0 * math.pi)
    df_se = slope_se / (2.0 * math.pi)

    # Signal-to-noise from the phase scatter rather than from a power
    # subtraction: the demodulated tone carries essentially all of the
    # signal power, so subtracting it from the total is a difference of
    # two nearly equal numbers and collapses into the rounding error.
    # A phasor with per-block SNR s has phase noise 1/sqrt(2s), and block
    # averaging by D has already bought a factor of D of noise rejection.
    if resid.size and np.std(resid) > 0:
        snr_block = 1.0 / (2.0 * float(np.mean(resid**2)))
        snr_db = 10.0 * math.log10(max(snr_block / decimation, 1e-30))
    else:
        snr_db = float("inf")

    return ToneFit(
        f0_hz=f0,
        f_measured_hz=f0 + df,
        ppm=df / f0 * 1e6,
        ppm_se=df_se / f0 * 1e6,
        n_blocks=len(t),
        decimation=decimation,
        resid_rms_rad=float(np.sqrt(np.mean(resid**2))),
        snr_db=snr_db,
        level_dbfs=level_dbfs,
    )


# ---------------------------------------------------------------------------
# Host clock (mach_absolute_time) vs NTP-disciplined time
# ---------------------------------------------------------------------------


def fit_host_clock(monotonic: np.ndarray, realtime: np.ndarray) -> tuple[float, float]:
    """Offset of the Mac's monotonic clock against NTP-disciplined time.

    PortAudio's CoreAudio timestamps live in the mach_absolute_time
    domain, which free-runs on the Mac's own crystal and is never steered
    towards UTC; only CLOCK_REALTIME is.  E1 and E2 are therefore offsets
    against that crystal, not against absolute time.

    Returns (ppm, se) where ppm is how slow the monotonic clock runs
    relative to UTC.  Both series carry comparable call-overhead jitter,
    so the slope is taken by total least squares rather than picking one
    side to call exact.
    """
    m = np.asarray(monotonic, dtype=np.float64)
    r = np.asarray(realtime, dtype=np.float64)
    if len(m) < 8:
        return float("nan"), float("nan")
    mc, rc = m - m.mean(), r - r.mean()
    _, _, vt = np.linalg.svd(np.column_stack([mc, rc]), full_matrices=False)
    vx, vy = vt[0]
    if vx == 0.0:
        return float("nan"), float("nan")
    slope = vy / vx  # d(realtime)/d(monotonic)

    resid = rc - slope * mc
    dof = max(len(m) - 2, 1)
    slope_se = math.sqrt(float(np.sum(resid**2)) / dof / float(np.sum(mc**2)))
    return (slope - 1.0) * 1e6, slope_se * 1e6


def apply_host_correction(ppm_vs_host: float, se_vs_host: float,
                          mono_slow_ppm: float, mono_slow_se: float
                          ) -> tuple[float, float]:
    """Convert an offset measured against the host clock into one against UTC.

    If one monotonic second is (1 + c*1e-6) real seconds, the monotonic
    clock under-reports elapsed time, so a rate computed per monotonic
    second is overstated by c ppm.  The correction therefore subtracts.
    """
    if not math.isfinite(mono_slow_ppm):
        return float("nan"), float("nan")
    return (
        ppm_vs_host - mono_slow_ppm,
        math.hypot(se_vs_host, mono_slow_se),
    )


# ---------------------------------------------------------------------------
# Weighted combination and consistency check
# ---------------------------------------------------------------------------


@dataclass
class Combined:
    """Weighted least-squares reconciliation of E1, E2 and E3."""

    dac_ppm: float
    dac_se: float
    adc_ppm: float
    adc_se: float
    dac_se_internal: float
    adc_se_internal: float
    birge: float
    correlation: float
    chi2: float
    p_value: float
    consistent: bool
    residuals: np.ndarray = field(default_factory=lambda: np.zeros(3))


def combine(dac: RateFit, adc: RateFit, tone: ToneFit,
            tolerance_ppm: float = 1.0) -> Combined:
    """Reconcile three measurements of two unknowns.

    E1 gives p_dac, E2 gives p_adc, E3 gives p_dac - p_adc.  The single
    spare degree of freedom becomes a chi-square test: E1/E2 come from
    PortAudio's callback timestamps and E3 from the recorded waveform's
    own sample-indexed timeline, so a disagreement beyond the error bars
    means something is wrong with the run -- a dropout, a rate that was
    silently resampled, or a clock that moved mid-measurement.
    """
    design = np.array([[1.0, 0.0], [0.0, 1.0], [1.0, -1.0]])
    measured = np.array([dac.ppm, adc.ppm, tone.ppm])
    sigmas = np.array([dac.ppm_se, adc.ppm_se, tone.ppm_se])
    # Guard against a degenerate zero error bar from a pathological fit.
    sigmas = np.maximum(sigmas, 1e-9)
    weight = np.diag(1.0 / sigmas**2)

    cov = np.linalg.inv(design.T @ weight @ design)
    theta = cov @ design.T @ weight @ measured
    resid = measured - design @ theta
    chi2 = float(resid @ weight @ resid)
    # One degree of freedom, so the survival function is erfc(sqrt(chi2/2)).
    p_value = math.erfc(math.sqrt(max(chi2, 0.0) / 2.0))

    se_dac_internal = float(math.sqrt(cov[0, 0]))
    se_adc_internal = float(math.sqrt(cov[1, 1]))

    # The internal error bars describe the scatter within each estimator,
    # and on a good run they come out near 0.005 ppm.  The estimators
    # nevertheless disagree with each other by a few tenths of a ppm,
    # because each carries systematics -- timestamp granularity on one
    # side, the analog path on the other -- that its own residuals cannot
    # see.  Rather than quote a precision the methods do not reproduce,
    # scale the uncertainty by the Birge ratio, the usual metrology
    # treatment of discrepant data.  The ratio is never allowed below 1:
    # agreeing better than chance is not evidence of extra precision.
    birge = math.sqrt(max(chi2, 1.0))  # 1 degree of freedom

    return Combined(
        dac_ppm=float(theta[0]),
        dac_se=se_dac_internal * birge,
        adc_ppm=float(theta[1]),
        adc_se=se_adc_internal * birge,
        dac_se_internal=se_dac_internal,
        adc_se_internal=se_adc_internal,
        birge=birge,
        correlation=float(cov[0, 1] / math.sqrt(cov[0, 0] * cov[1, 1])),
        chi2=chi2,
        p_value=p_value,
        # Judge the run on how far apart the estimators actually are, not
        # on the p-value: once a 60 s fit reaches 0.005 ppm of internal
        # scatter, any real systematic makes chi-square enormous and the
        # test would condemn every run ever taken. A few tenths of a ppm
        # is the method's floor; whole ppm means something broke.
        consistent=bool(np.max(np.abs(resid)) < tolerance_ppm),
        residuals=resid,
    )


def expected_ppm_se(resid_rms_frames: float, rate: float,
                    callback_period: float, duration: float) -> float:
    """Predicted 1-sigma ppm for a rate fit, for run-length planning.

    A slope estimate over a span T accumulates Fisher information as T^3,
    so the standard error falls as T^-1.5: doubling the run buys a factor
    of 2.8, not 1.4.
    """
    sigma_t = resid_rms_frames / rate
    return 1e6 * sigma_t * math.sqrt(12.0 * callback_period) / duration**1.5


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------


def analyze_capture(data) -> dict:
    """Run every estimator over one saved capture (an .npz mapping)."""
    rate = float(data["rate"])
    out_nominal = float(data.get("out_nominal_rate", rate))
    in_nominal = float(data.get("in_nominal_rate", rate))
    f0 = float(data["tone_hz"])

    dac = fit_rate(data["out_frames"], data["out_times"], out_nominal)
    adc = fit_rate(data["in_frames"], data["in_times"], in_nominal)
    tone = fit_tone(np.asarray(data["recording"], dtype=np.float64), in_nominal, f0)
    result = combine(dac, adc, tone)

    mono_ppm, mono_se = fit_host_clock(data["mono_times"], data["real_times"])
    dac_utc = apply_host_correction(result.dac_ppm, result.dac_se, mono_ppm, mono_se)
    adc_utc = apply_host_correction(result.adc_ppm, result.adc_se, mono_ppm, mono_se)

    return {
        "rate": rate,
        "dac_fit": dac,
        "adc_fit": adc,
        "tone_fit": tone,
        "combined": result,
        "mono_ppm": mono_ppm,
        "mono_se": mono_se,
        "dac_utc": dac_utc,
        "adc_utc": adc_utc,
        "resampled": bool(data.get("resampled", False)),
        "xruns": int(data.get("xruns", 0)),
    }


def format_report(res: dict, out_name: str = "DAC", in_name: str = "ADC") -> str:
    """Human-readable report for one sample rate."""
    lines = []
    rate = res["rate"]
    lines.append("=" * 78)
    lines.append("  %g Hz" % rate)
    lines.append("=" * 78)
    if res["resampled"]:
        lines.append(
            "  *** WARNING: one or both devices were NOT running at this rate;"
        )
        lines.append(
            "  *** CoreAudio resampled, and these figures describe the resampler."
        )
    if res["xruns"]:
        lines.append("  note: %d callbacks reported an over/underflow" % res["xruns"])

    lines.append("")
    lines.append("  Individual estimators")
    lines.append("    E1 DAC vs host   %s" % res["dac_fit"])
    lines.append("    E2 ADC vs host   %s" % res["adc_fit"])
    lines.append("    E3 DAC/ADC tone  %s" % res["tone_fit"])
    lines.append(
        "       recorded tone: %.1f dBFS, effective SNR %.1f dB%s"
        % (
            res["tone_fit"].level_dbfs,
            res["tone_fit"].snr_db,
            "  <- low, E3 may be unreliable" if res["tone_fit"].snr_db < 10.0 else "",
        )
    )
    if res["dac_fit"].autocorr_flagged or res["adc_fit"].autocorr_flagged:
        lines.append(
            "    (residuals are serially correlated; error bars are HAC-inflated"
            " by up to %.1fx)"
            % max(res["dac_fit"].hac_inflation, res["adc_fit"].hac_inflation)
        )

    comb = res["combined"]
    lines.append("")
    lines.append("  Combined (weighted least squares, 3 measurements / 2 unknowns)")
    lines.append(
        "    %-22s %+9.3f +/- %.3f ppm  (%.4f Hz)"
        % (out_name + " output", comb.dac_ppm, comb.dac_se, rate * (1 + comb.dac_ppm * 1e-6))
    )
    lines.append(
        "    %-22s %+9.3f +/- %.3f ppm  (%.4f Hz)"
        % (in_name + " input", comb.adc_ppm, comb.adc_se, rate * (1 + comb.adc_ppm * 1e-6))
    )
    lines.append(
        "    chi2 = %.2f (1 dof), p = %.3f, Birge ratio %.1f  -> %s"
        % (comb.chi2, comb.p_value, comb.birge,
           "CONSISTENT" if comb.consistent else "DISCREPANT")
    )
    lines.append(
        "    quoted error is the internal %.4f ppm scaled by the Birge ratio;"
        % comb.dac_se_internal
    )
    lines.append(
        "    estimator residuals %s ppm"
        % np.array2string(comb.residuals, precision=3)
    )
    if not comb.consistent:
        lines.append(
            "    *** the estimators disagree by more than 1 ppm: distrust this rate"
        )

    if math.isfinite(res["mono_ppm"]):
        lines.append("")
        lines.append(
            "  Host clock: monotonic runs %+.3f +/- %.3f ppm vs NTP-disciplined time"
            % (res["mono_ppm"], res["mono_se"])
        )
        lines.append(
            "    corrected to UTC: output %+.3f +/- %.3f ppm, input %+.3f +/- %.3f ppm"
            % (res["dac_utc"][0], res["dac_utc"][1],
               res["adc_utc"][0], res["adc_utc"][1])
        )
    return "\n".join(lines)


def _main(argv: list[str]) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("capture", nargs="+", help="one or more .npz captures")
    args = parser.parse_args(argv)

    for path in args.capture:
        with np.load(path, allow_pickle=False) as data:
            res = analyze_capture(data)
        print(format_report(res))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
