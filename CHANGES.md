[//]: # (-*- coding: utf-8 -*-)

# changes and known issues of airspy-fmradion

## Git submodules required

The following submodule is required:

* [r8brain-free-src](https://github.com/avaneev/r8brain-free-src)

## External version control code required

The following Git repositories are required:

* [{fmt}](https://github.com/fmtlib/fmt)
* [jj1bdx's fork of cmake-git-version-tracking](https://github.com/jj1bdx/cmake-git-version-tracking)

## Platforms tested

* Mac mini 2023 Apple Silicon (M2 Pro), macOS 26.2, Apple clang version 17.0.0 (clang-1700.6.3.2)
* Ubuntu 24.04.3 LTS x86\_64, gcc 14.2.0
* Raspberry Pi 5 with Raspberry Pi OS 64bit Lite (Debian Trixie 13.1), gcc 14.2.0

## Features under development

* FM Pilot PLL is under revision and reconstruction. Initial analysis result is available at doc/fm-pll-filtereval.py (requires Python 3, SciPy, matplotlib, and NumPy).
* C++23 `std::print()` functionality requirement will be mandated once it is available on all platforms of macOS, Ubuntu LTS, and Raspberry Pi OS.

## Known limitations

* The author observed output sound cracking on PortAudio on macOS, when computing-intensive jobs/actions are performed on the other jobs (such as Web browsers) of the same computer. This hasn't been observed on file recording, which uses a different output driver.
* libsndfile 1.1 or later must be installed to support MP3 file output.
* For Raspberry Pi 3 and 4, Airspy R2 10Msps and Airspy Mini 6Msps sampling rates are *not supported* due to the hardware limitation. Use in 2.5Msps for R2, 3Msps for Mini.
* Since 20231227-0, the buffer length option `-b` is no longer handled and will generate an error. The audio sample data sent to AudioOutput base classes are no longer pre-buffered.
* The author observed anomalies of being unable to run PortAudio with the `snd_aloop` loopback device while testing on Raspberry Pi OS 32bit Debian *Bullseye*. Portaudio anomaly support is out of our development scope.
* The author observed output latency of ~200 milliseconds between Sony ICF-M780N radio audio output of FM broadcast and airspy-fmradion FM `-E 100` stereo reception using USB DAC FiiO K7, macOS 26.1 on Mac mini 2023. Note well that the latency between PortAudio and USB DAC itself is dominant.

### Intel Mac support is dropped

Intel Mac hardware is no longer supported by airspy-fmradion, although the author makes the best effort to prevent introducing anything against the compilation on the Intel Macs. Please open an issue on the GitHub repository if you find anything incompatible on Intel Macs.

## Changes (including requirement changes)

* dev-lpf-volk 20260725: Rewrote the FIR low-pass filters `LowPassFilterFirIQ` and `LowPassFilterFirAudio` (`sfmbase/Filter.cpp`) to compute each output as a single VOLK dot product, replacing the hand-rolled warm-up and symmetry-folded steady-state loops; see doc/LPF_VOLK_20260725.md for the design and the measured difference against the previous code.
  * Each `process()` now builds one contiguous history buffer `[state | input]` per block and emits every output with one dot product over `order + 1` taps: `volk_32fc_32f_dot_prod_32fc` for the IQ path (complex samples, real taps) and `volk_32f_x2_dot_prod_32f` for the audio path. The coefficients are stored reversed once at construction so the dot product is a true convolution. The two-region warm-up/steady-state structure and the linear-phase symmetry fold are both gone, and the `n < order` / `n >= order` state-update branches collapse to one `std::copy`; net -11 lines.
  * Fixed a latent warm-up bug in the process. The old warm-up loops summed taps `j = 1 .. order` and dropped the `j = 0` tap (`coeff[0] * input[p]`) on the first `order` outputs of every block. It was nearly harmless only because every shipped coefficient table is a heavily-tapered windowed sinc whose edge tap is <= -67 dB, exactly 0 for the 3-tap IF filter used by the default and wide FM presets. The dot product always covers all `order + 1` taps, so the omission is closed.
  * Verified against the `dev` branch by decoding three recorded IQ files: the maximum per-sample audio difference is 3-5e-7 (one to two float32 ULP) and the signal-to-difference ratio is ~134 dB on all three, i.e. the floating-point re-association floor, ~36 dB below the 16-bit audio floor and benign on a real multipath signal. Whole-pipeline user CPU dropped about 8% on the 20-second piano decode; the filter stage's own speedup is larger, since VOLK vectorizes the full `order + 1` taps where the old symmetry fold, which the compiler did not vectorize, did about half the multiplies.
* dev-multipath-exp 20260724: Reviewed, measured, and revised the FM multipath filter (`MultipathFilter`, CMA/Godard blind equalizer). Four source changes, all verified against recorded IQ and against a synthesized two-ray channel with known ground truth; see doc/MULTIPATH_FILTER_DESIGN_20260724.md for the full evaluation (section 0 lists the diff, section 17 has the ground-truth results).
  * Removed six of the eleven O(N) passes per four input samples. The delay line is now a ring buffer holding each sample twice, so the newest N samples stay contiguous for VOLK without the `m_state.erase(m_state.begin())` shift that ran on every input sample; and the NLMS power sum is maintained incrementally (`|x_new|^2 - |x_leaving|^2`) instead of being recomputed by two VOLK passes into a `volk::vector<float>` scratch buffer that was heap-allocated on every coefficient update. Exactness is preserved by recomputing the sum every 65536 updates. Measured 1.6x faster on the filter itself for `-E36` through `-E200` (the gain washes out at `-E400`, where the doubled delay-line footprint costs more than the removed memmove saves). Decoded audio matches the previous code to -104 dB and convergence statistics to five significant figures. VOLK's unsuffixed `volk_32fc_x2_dot_prod_32fc` was verified to dispatch correctly at every byte offset the ring buffer can produce.
  * Scaled the NLMS `alpha` with the filter order, clamped at 0.5, so the per-tap adaptation rate `mu = alpha / sum(|state|^2) ~= alpha / filter_order` stays at its `-E36` value instead of falling as 1/N. Raising `-E` at fixed `alpha` was silently dividing the adaptation rate, which degrades tracking on a time-varying channel. `-E36` is unchanged by construction. Worth +6.1 dB of post-discriminator SNR at `-E100` against ground truth. The clamp exists because the stability limit of this loop is on `alpha`, not on `mu`: `alpha = 1.0` diverges at every filter order tested, while `alpha = 0.5` is stable at N = 145, 801, and 1601.
  * `FmDecoder` now clears the multipath filter delay line as well as the coefficients when `process()` reports divergence. Resetting the coefficients alone left the non-finite sample that triggered the reset sitting in the delay line, where it kept the output non-finite for up to N more samples and produced a burst of resets instead of one.
  * The divergence guard in `process()` now bounds the magnitude of the output and of the error value at 10, instead of only rejecting non-finite values. A diverging loop previously reached `|mf_error|` = 8.7e37 before anything noticed; it now trips at 92. The comparisons are written as "not within the limit" so that NaN is still rejected, preserving the reason `-ffast-math` must never be enabled. This is defensive hygiene only: it makes no audible difference, because the phase discriminator takes `atan2()` of consecutive samples and is blind to amplitude.
  * Added the `-E` sizing advice to the help text and to the `MultipathFilter` constructor. **The stage count must be sized to the echo delay spread; more stages is not safer.** Measured against ground truth on a 3 microsecond echo, `-E200` decodes 6.9 dB *worse* than switching the filter off, and the optimum for that channel is around `-E12`; on an 8 microsecond echo the optimum moves up to `-E36`. On a shallow (amplitude 0.5) echo the filter is a net loss at every setting, FM being robust to shallow echoes on its own. This is the missing mechanism behind the long-standing "For stable reception only: turn off if reception becomes unstable" caution.
  * Removed `MultipathFilter::get_reference_level()`. The reference tap is pinned to 1+0j at the end of every coefficient update, so the accessor could only ever return 1.0; it had no callers. Also dropped the meaningless return-type `const` on `get_error()`, made `get_coefficients()` and the two `FmDecoder` forwarding accessors `const` members, and replaced the stale "maximum amplitude must be less than sqrt(2 / alpha)" comment, which described unnormalized LMS while the code implements normalized CMA (for which that bound does not hold; divergence was measured at `alpha = 1.0`, not at the classic NLMS bound of 2).
  * Added doc/make_two_ray_channel.py (synthesizes `y[n] = x[n] + a*exp(j*theta)*x[n-tau]` on a clean IQ recording, with windowed-sinc fractional delay for sub-sample echoes) and doc/eval_two_ray_snr.py (scores a decode against the decode of the clean file by fitting a least-squares FIR, which absorbs the filter's fractional-sample group delay). These provide the first ground truth for this filter. The generated channel files are written to test-files/ and are not committed.
  * doc/make_two_ray_channel.py also generates *time-varying* channels: `--fade-depth` swings the echo amplitude sinusoidally in dB, `--fade-rate` sets the rate, and `--doppler` rotates the echo phase linearly (the mechanism a moving receiver actually produces, which fades the composite envelope without the echo amplitude changing). This makes it possible to build a channel that crosses the non-minimum-phase boundary during the run. `--fade-depth 0` leaves the amplitude exactly constant, so static invocations are bit-identical to before.
  * Rejected on measurement, with no source change: holding the coefficients in `std::complex<double>` (the NLMS step clears the float32 ULP by 86-450x in every configuration measured, so quantization is not the limiter); raising `alpha` from 0.1 (it lowers the filter's own cost function by 38 percent but measures 0.2-3.2 dB *worse* against ground truth, so `alpha` stays 0.1); adding a coefficient leakage term (no tap-norm drift observed on any of the three off-air recordings); and softening the hard `w[ref] = 1+0j` reference-tap constraint. The last of these was tested on nine synthesized channels crossing the non-minimum-phase boundary while fading: the long-suspected thrashing does not occur (no divergence resets, tap-vector norm bounded throughout), and while the unconstrained solution genuinely does want `|w[ref]| < 1` there (it settles at 0.31-0.88 when the pin is replaced by a phase-only constraint), softening measures worse in 8 of 9 cells. The pin also suppresses the equalizer's global-delay ambiguity, which a phase-only constraint leaves free to drift while the channel moves.
  * Measured, not fixed: on a fading channel the filter is a net loss at `-E36` in 7 of 9 cells, by up to 10.2 dB, whichever reference-tap constraint is used. It is not instability and not tracking lag (a larger `alpha` is worse at every fade rate tested); a CMA equalizer adapting on a moving channel injects more misadjustment noise than the multipath distortion it removes. With the stage-count result above, this gives the "turn off if reception becomes unstable" caution a measured mechanism: the multipath filter is a tool for stationary reception. Note for future work: the CMA cost function `mf_error` proved to be a poor proxy for audio quality in both directions, and no conclusion should rest on it alone.

* dev-pll-fix 20260723: Changed the lock-decision time from 0.5 second to 0.2 second. Changed the type of private class objects in PilotPhaseLock from int to unsigned int for clarity.
* dev-std-atan2-pilot 20260723: Replaced the `fast_atan2f()` float table-lookup phase detector in `PilotPhaseLock` with the standard double `std::atan2()`. Both phasor inputs are already double, so this drops a redundant double-to-float narrowing and yields a correctly-rounded phase error; per-call speed is platform dependent (faster on arm64/macOS, ~2x slower on x86_64/glibc) but a negligible fraction of one core, and the decoded output is unchanged. Include swap `Utility.h` -> `<algorithm>`/`<cmath>`. Removed the now-unused `fast_atan2f()` (GNU Radio 257-entry atan table plus linear interpolation, ~152 lines) from `include/Utility.h`, and removed the two unused second-stage phasor biquads (`m_biquad_phasor_i2`/`_q2`) from the `PilotPhaseLock` header. See doc/CORE_MATH_ATAN2F_20260722.md and doc/STD_ATAN2_X86_64_20260722.md for the accuracy/speed evaluation. (This branch also carries the dev-pll-zeta-redesign 20260723 loop retune below.)
* dev-pll-zeta-redesign 20260723: Retuned the FM stereo pilot PLL (`PilotPhaseLock`) so its dominant closed-loop pole pair is damped at zeta = 0.71 (was ~0.57, mildly under-damped), inside the conventional 0.70-0.73 band, without changing the loop natural frequency (fn held at 22.3 Hz) or the loop structure. Widened the in-loop phasor biquad LPF (all-pole, real corners ~34/160 -> ~40/188 Hz) and rescaled the FIR PI gains by x0.889 to hold fn constant. Verified against an exact 5-state linear loop model (zeta = 0.7101, fn = 22.34 Hz, stable) and by recompiling the binary on test-files/piano_iqtest.wav: steady-state tracking, jitter, and pilot level are unchanged (19000.012 Hz, std 0.045 Hz), the acquisition first swing shrinks from -2.66 to -1.52 Hz (less ringing), and the decoded audio is identical to -90 dBFS. See doc/PLL_REDESIGN_20260723.md and the prior analysis doc/PLL_ANALYSIS_20260722.md.
* 20260716-0:
  * Changed the copyright year of the source code.
  * Changed the audio-path sample type `Sample` in SoftFM.h from `double` to `float`, halving audio buffer memory bandwidth and reducing decode CPU time by about 16 percent. The `PilotPhaseLock` recursive state and loop computation are kept in explicit `double` (float rounding there becomes 38 kHz subcarrier phase noise); all other recursive IIR filters already keep hardcoded `double` state independent of `Sample`. Audio output boundaries were adapted accordingly (`sf_write_float`, float VOLK kernels, float-to-double conversion for the double-only r8brain resampler). Measured against the double pipeline with a 20-second off-the-air FM stereo broadcast recording: output difference is -140 dBFS RMS (the float32 file quantization floor), audible-band power identical; see doc/FLOAT_OUTPUT_20260716.md for the full evaluation.
* dev-portaudio-latency-option 20260715: Added the new `-L`/`--portaudio-latency` option to set the PortAudio output suggested latency in milliseconds (valid range: 1 to 40, default: floored at 40 milliseconds). When given, the value is passed verbatim to PortAudio as `suggestedLatency`, bypassing the 40-millisecond minimum-latency floor. `-L` is ignored unless PortAudio output (`-P`) is used.
* dev-resampler-lowlatency 20260714: Made the following changes for output latency reduction:
  * IfResampler and AudioResampler now pass explicit r8brain design parameters (`ReqTransBand`/`ReqAtten`) chosen for the actual FM/AM broadcast signal requirements, instead of the library defaults tuned for mastering-grade conversion. Measured steady-state resampler latency: FM 40.6 -> 2.9 ms; AM/CW/NBFM IF resampling 49.6 -> 6.0 ms. Output timeline and decoded audio content are unchanged.
  * On macOS, PortAudioOutput temporarily requested `defaultLowOutputLatency` with a 25 ms minimum (previously `defaultHighOutputLatency` with a 40 ms minimum). Measured PortAudio-granted output latency on USB DAC FiiO K7: 210.7 -> 110.3 ms, entirely attributable to the smaller latency request; CoreAudio grants roughly 5x the requested latency on this device, so the old 40 ms request was the dominant term of the previously observed ~200 ms latency. This macOS-specific code path was removed on 20260715 and superseded by the portable `-L`/`--portaudio-latency` option (see the dev-portaudio-latency-option entry above); without `-L`, the default request is again floored at 40 ms on all platforms.
  * Net measured steady-state latency reduction at the time of this branch: ~138 ms for FM, ~144 ms for AM/CW/NBFM. Of this, the resampler change alone accounts for ~38 ms (FM) / ~44 ms (AM/CW/NBFM); the remaining ~100 ms came from the since-removed macOS latency request and is now obtainable on any platform with `-L 25` or lower.
  * Removed the ineffective `r8b` CMake library target and its `R8B_*` compile definitions: they never propagated to the sfmbase sources, the pffft object was never linked into the executable, and main.cpp saw differently configured r8brain headers than sfmbase (an ODR hazard). r8brain-free-src is now used header-only. No DSP behavior change (verified bit-identical decode).
  * Added measured latency analyses: doc/LATENCY_PLAN_20260713.md (FM, including executable-level measurements and an executive summary), doc/AM_LATENCY_PLAN_20260714.md, doc/CW_LATENCY_PLAN_20260714.md, and doc/NBFM_LATENCY_PLAN_20260714.md.
* 20260713-0: Made the following changes:
  * RtlSdrSource now streams with `rtlsdr_read_async()` instead of the blocking `rtlsdr_read_sync()` loop. This gives ~15 USB buffers of in-flight tolerance against scheduling jitter (previously fatal "short read, samples lost"), and adds proper cleanup on SIGINT/SIGQUIT/SIGTERM. CPU usage and throughput are unchanged. See doc/RTL_READ_ASYNC_20260713.md for details.
  * Fixed vulnerabilities W1-W7 and X1-X8; see doc/FIXES_CLAUDE_20260610.md for the summary.
  * DataBuffer prints the queue overflow warning on every dropped block.
  * Updated the comment style to C++ single-line comments. See doc/COMMENT_STYLE_20260711.md for the summary.
  * Now building {fmt} 12.2.0.
* 20260505-0: Massive bugfix and vulnerability workarounds were introduced, under the analysis of Claude Code.
  * See doc/FIXES_CLAUDE_20260502.md and doc/FIXES_CLAUDE_20260504.md for the summary of the fixes. 
  * The `-E` option value range is explicitly limited from 1 to 1024.
* 20260211-0: Made the following changes:
  * {fmt} is now a required repository fetched from CMake. The static library will be automatically built and linked.
  * Updated r8brain-free-src to Version 7.1; CMakefile.txt was updated as well.
  * [Changed CW/USB/LSB FIR filters not to down/up-convert between 48kHz and 12kHz sampling rates, and to use the 2049-tap filters of 48ksamples/sec](https://github.com/jj1bdx/airspy-fmradion/issues/48). This significantly reduced output latency, tolerable for interactive use (such as FT8 reception). The CPU usage increased \~4 times than before, but was still in acceptable range (\~60% of FM `-E 100` setting).
  * File permission mode for the sound file output and the ppsfile is explicitly set to 0600 instead of 0666.
  * Refactored non-owning raw pointers to unique pointers.
* 20250929-0: Made the following changes:
  * Changed PortAudioOutput to use the own callback code instead of the stock blocking stream, for shorter output latency. See 20230923 in this changelog.
  * PortAudio minimum latency is no longer explicitly set, and is now set to defaultHighOutputLatency. Note: this behavior can be changed by setting compilation flag `PA_LOW_LATENCY` to defaultLowOutputLatency. Practically in many cases the low output latency setting works fine, but is not the default value, for safety.
  * Minimal `pa_ringbuffer` library code for handling the PortAudio callback was duplicated and included from PortAudio under PortAudio V19 License. Note: the ringbuffer code is *not* a Git submodule.
  * CMake minimum version is now 3.25.
* 20250714-0: no major functionality changes from 20241208-0.
* 20241208-0: [Use {fmt} as the output library.](https://github.com/jj1bdx/airspy-fmradion/pull/83)
  * {fmt} 11.0.2 or later is required.
* 20240424-0: Made the following changes:
  * [Add libairspyhf latest version document.](https://github.com/jj1bdx/airspy-fmradion/pull/80)
    * Airspy HF+ Firmware R3.0.7 and R4.0.8 both work OK on libairspyhf 1.6.8.
    * For the Firmware R4.0.8, use libairspy 1.8 to have full compatibility.
  * [Use shared libraries for airspy, airspyhf, and rtl-sdr.](https://github.com/jj1bdx/airspy-fmradion/pull/79)
  * Tested `airspy_set_packing()` for Airspy R2, but this increased CPU usage on Apple Silicon M2 Pro, so the change was not incorporated.
* 20240316-0: Made the following changes:
  * Raspberry Pi 4 with Raspberry Pi OS 64bit lite is now officially tested.
  * *Note well: Raspberry Pi OS 32bit is not supported*.
  * [`-A` AFC option is removed.](https://github.com/jj1bdx/airspy-fmradion/pull/70)
  * [Change VOLK version display format.](https://github.com/jj1bdx/airspy-fmradion/pull/71)
  * [Documentation update](https://github.com/jj1bdx/airspy-fmradion/pull/72):
    * Reduce text length of README.md.
    * Old README.md is now located at [`doc/old-README-until-2023.md`](doc/old-README-until-2023.md).
  * [For PortAudio, the minimum output latency is explicitly set to 40 milliseconds.](https://github.com/jj1bdx/airspy-fmradion/pull/73)
  * [Use libsndfile MP3 output capability to generate the MP3 file directly as the audio output, when supported.](https://github.com/jj1bdx/airspy-fmradion/pull/74)
    * libsndfile 1.1 or later is required for the MP3 support.
    * A conditional compilation flag `LIBSNDFILE_MP3_ENABLED`, set by cmake, is introduced.
    * See [`libsndfile.md`](libsndfile.md) for how to installing the latest libsndfile, suggested for Ubuntu 22.04.4 LTS.
    * [See also the related GitHub issue.](https://github.com/jj1bdx/airspy-fmradion/issues/47)
  * Apply [cmake-format](https://github.com/cheshirekow/cmake_format) for `CMakeLists.txt`.
    * Default style: `.cmake-format.py`
* 20240107-0: Made the following changes:
  * For broadcasting FM, show stereo 19kHz pilot signal level when detected.
  * Remove displaying whether FM stereo pilot signal level is stable or unstable.
  * Add Git info into the binary program built, with [cmake-git-version-tracking](https://github.com/andrew-hardin/cmake-git-version-tracking.git) (using jj1bdx's fork).
  * Add compile command database support on CMakeLists.txt.
  * Cleaned up old documents.
  * Fixed the following bugs detected by clang-tidy:
    * [ERR34-C. Detect errors when converting a string to a number](https://wiki.sei.cmu.edu/confluence/display/c/ERR34-C.+Detect+errors+when+converting+a+string+to+a+number)
      * Use `Utility::parse_int()` instead of raw `atoi()`
    * [DCL51-CPP. Do not declare or define a reserved identifier](https://wiki.sei.cmu.edu/confluence/display/cplusplus/DCL51-CPP.+Do+not+declare+or+define+a+reserved+identifier)
      * Remove unused `_FILE_OFFSET_BITS`
  * Fixed the bug of FileSource playback: the code did not terminate after the end of playback.
    * main.cpp: add checking pull_end_reached() in the main loop.
  * Set RtlSdrSource's default_block_length from 65536 to 16384, to prevent popping cracking sound (observed on Mac mini 2023).
  * stat_rate calculation is redesigned by observation of actual SDR units (:i.e., Airspy HF+, Airspy R2, and RTL-SDR).
* 20231227-0: Made the following changes:
  * Split class PilotPhaseLock from FmDecode.
  * Removed submodule readerwriterqueue.
  * Re-introduced DataBuffer from commit <https://github.com/jj1bdx/airspy-fmradion/commit/49faddbae1354bcb7bfcd2b24db458b770273cb5>.
  * PhaseDiscriminator now contains NaN-removal code.
  * Introduced accurate m_pilot_level computation for PilotPhaseLock.
  * Introduced enum PilotState and the state machine for more precisely showing stereo pilot signal detection and the signal levels.
  * Removed buffer option `-b` and `--buffer` finally.
* 20231216-0: Removed recording buffer thread. This will simplify the audio output operation. Also, lowered the output level of AM/CW/USB/LSB/WSPR decoder to prevent audio clipping, and changed the IF AGC constants for longer transition timing.
* 20231215-0: Fix the following known bugs and refactor the code to streamline the functioning:
  * Bug: a hung process during the startup period before valid audio signals are coming out
  * Bug: displaying `-nan` in the output level meter in broadcast FM and NBFM
    * The NaN is presumably generated by volk_32fc_s32f_atan2_32f() in PhaseDiscriminator::process()
    * This NaN issue was presumably the root cause of the multipath filter anomaly first fixed in 20231213-1
  * Enhancement: streamlining processing flow in the main for loop of `main()`
  * Enhancement: removing the initial waiting period for startup; the output is now activated from the block number 1
  * Utility addition: adding `Utility::remove_nans()`, a function to check and substitute NaNs and infinity values in IQSamplesDecodedVector
* 20231213-1: Fixed a NaN issue caused by 0+0j (true zero) output of the multipath filter; the true zero output now causes resetting the filter. This is presumably also one of the reasons that caused the audio disruption issue in 20231212-1 and 20231213-0.
* 20231213-0: Fixed an uninitialized variable `m_save_phase` in PhaseDiscriminator as in [the pull request](https://github.com/jj1bdx/airspy-fmradion/pull/43) by Clayton Smith.
* 20231212-1: FAILED: tried to make API compatible with [VOLK 3.1.0 change for s32fc functions](https://github.com/gnuradio/volk/pull/695), for `volk_32fc_x2_s32fc_multiply_conjugate_add_32fc()`, but this didn't work on Ubuntu 22.04.3.
* 20231212-0: Updated r8-brain-free-src to Version 6.5.
* 20230923: failed changes: low latency setting for buffering-based PortAudio didn't work well. Discarded changes of 20230910-1 to 20230910-4 from the dev branch.
* 20230910-0: Updated r8brain-free-src to Version 6.4.
* 20230528-2: DataBuffer class is reimplemented as a wrapper of `moodycamel::BlockReaderWriterQueue`, which allows efficient blocking operation and removes the requirements of busy waiting by using `moodycamel::BlockReaderWriterQueue::wait_dequeue()`.
* 20230528-1: DataBuffer class is now implemented as a wrapper of `moodycamel::ReaderWriterQueue` class in <https://github.com/cameron314/readerwriterqueue>. All lock-based synchronization functions from DataBuffer class are removed because they are no longer necessary. The repository readerwriterqueue is added as a git submodule. Also, sample length count is removed from the DataBuffer class because of their rare usage.
* 20230528-1: All DataBuffer queue length measurement code in main.cpp are bundled under a compilation flag `DATABUFFER_QUEUE_MONITOR`, which is not necessary for the production code. The actual maximum queue length measured in Mac mini 2018 executions are less than 10, even when the output glitch occurs due to a higher-priority process invocation, such as a web browser. The new DataBuffer class sets the default allocated queue length to 128.
* 20230526-0: Explicitly skip IF Resampler in class FmDecoder to reduce CPU usage for typical settings (i.e., IF sample rate is set to 384 ksamples/sec for Airspy HF+).
* 20230430-0: Forcefully set the coefficient of the reference point of FM multipath filter to 1 + 0j (unity). This may change how the filter behaves. Field testing since 20230214-test shows no notable anomalies.
* 20221215-1: Updated r8brain-free-src to Version 6.2.
* 20221215-0: Fixed AF and IF AGC anomaly when the current gain becomes NaN/Inf. Set workaround by adding a small value (1e-9) for log10() calculation generating the output value.
* 20220911-0: Refactored status message calculation, tested with libvolk 2.5.2.
* 20220903-0: Refactored include/DataBuffer.h for streamlining handling locks and mutexes, using C++17 std::scoped\_lock.
* 20220819-1: Restricted RTL-SDR sampling rate to [900001, 3200000] [Hz]. Also the default IF sample rate of RTL-SDR is set to 1152000Hz. AudioResampler and IfResampler maximum input length check is implemented.
* 20220819-0: /4 downsampling above 3.1MHz/3100kHz in 20210702-0 has been removed. The new IF resampler based on r8brain-free-src works well without preresampling.
* 20220818-1: Added r8brain-free-src options for gaining performance.
* 20220818-0: Implemented r8brain-free-src also for IfResampler. libsamplerate is removed. [r8brain-free-src](https://github.com/avaneev/r8brain-free-src) is used instead of libsoxr. r8brain-free-src is a sample rate converter designed by Aleksey Vaneev of Voxengo.
* 20220817-1: Introduced r8brain-free-src for AudioResampler.
* 20220817-0: Introduced libsamplerate aka Secret Rabbit Code for IfResampler.
* 20220810-1: IF AGC max gain for FM is raised to 10^5, AM/DSB/USB/LSB/WSPR/CW is raised to 10^6.
* 20220810-0: AGC algorithms are refactored.
* 20220809-0: Source code comments and documentation changes only.
* 20220808-3: AF AGC is replaced by the Tisserand-Berviller AGC algorithm. This is still experimental and more evaluation is needed. Output level of the AM (including USB/LSB/DSB/CW/WSPR) modes may increase by 2dB to 3dB, due to the algorithm change.
* 20220808-1: commit 40e342b2cf0e6710800c578272caf515a8b83add: IF AGC distortion rate reduced to improve multipath filter result.
* 20220808-0: IF AGC is replaced by the Tisserand-Berviller AGC algorithm. This is still experimental and more evaluation is needed.
* 20220412-0: Re-enabled experimental FM AFC code after the continuous-phase frequency shifting was implemented in the commit 37742981c34e53eb8083af07c0bc518491dc18ee.
* 20220313-1: Removed experimental FM AFC code due to periodical noise generation. `-A` option is removed as well.
* 20220313-0: Moved FineTuner object into independent files. Added experimental 10Hz-step IF AFC for FM broadcast (use `-A` option to enable). Simplified INSTALL-latest-libvolk.md.
* 20220221-0: Shortened polling periods for Airspy R2/Mini and Airspy HF+ from 1 second to 100 milliseconds. Also reduced AGC output levels for CW and WSPR to prevent output overdrive.
* 20220206-0: Rolled back the workaround of exit(0) in 20220205-1, because this is no longer necessary when a proper fix is done on Airspy HF+ driver.
* 20220205-1: Rolled back Airspy HF+ source driver stop/close semantics. Add exit(0) at the end of program to force-exit the code to avoid causing segfault.
* 20220205-0: Signal handling is now performed on a dedicated thread. SIGQUIT is also captured and will terminate the program gracefully as SIGINT and SIGTERM does. Redundant initialization sequences removed from Airspy HF+ source driver.
* 20220203-0: Explicitly state that pipe is not supported for `-W` and `-G` RIFF/WAV file output options.
* 20211209-0: Support for Apple Silicon M1: Add more default dirctories to CMakeLists.txt, add VOLK 2.5 installation instruction
* 20211101-0: `handle_sigterm()` now uses `psignal()` instead of `strsignal()` for the thread safety of Linux. Also fixed the bug of not saving `errno` in the signal handler. This bug was found by the ThreadSanitizer of macOS clang.
* 20211022-0: minor bugfix of COEFF\_MONITOR coefficient display code.
* Since 20210718-0, receiving block number is uint64\_t, and 12 digits are displayed.
* Since 20210709-0, all file output is controlled under libsndfile. Previous output formats are compatible with the older version of airspy-fmradion.
* Since 20210709-0, WAV file output is RF64 compatible, and automatically degraded to WAV if the output is less than WAV file length limit (4GB), controlled by libsndfile.
* Since 20210709-0, -G option is added for RF64/WAV FLOAT\_LE output.
* Obsoleted: Since 20210702-0, if IF rate is 3.1MHz (3100kHz) or larger, a decimation LPF of +-400kHz width by decimation ratio 4 is inserted after the Fs/4 shifter to reduce the ratio of fractional resampler for increasing the output stability to prevent FM stereo PLL unlocking. This function increases the CPU usage, so for a lower CPU usage use a lower sampling rate.
* Since 20210702-0, halfband filter kernel for Airspy R2/Mini is no longer used.
* FM Pilot PLL threshold level has been lowered from 0.01 to 0.001 since 20210607-0, for preventing unwanted unlocking.
* The 2nd-order LPF of FM Pilot PLL had been applied twice since 20210116-0 to 20210427-0, but rolled back to once (as in original SoftFM) since 20210607-0.
* Since 20210427-0, C++17 is required (instead of previous C++11). Modern compilers of Raspberry Pi OS, Ubuntu, and macOS all do support C++17 extensions.
* FM ppm display shows ppb (0.001ppm) digits since 20210206-0.
* Timestamp file format has been changed since 20201204-0.
* PortAudio is required since Version 20201023-0. Use PortAudio v19. Former ALSA output driver is replaced by more versatile PortAudio driver, which is compatible both for Linux and macOS.
* libvolk is required since v0.8.0. If you don't want to install libvolk, use v0.7.8 instead. Use the latest master branch of libvolk. Configure the `volk_config` file with `volk_profile -b` for the maximum performance. See [INSTALL-latest-libvolk.md](INSTALL-latest-libvolk.md) for the details.
* v0.8.5 and the earlier versions set the compilation flag of `-ffast-math`, which disabled the processing of NaN. This will cause a latch-up bug when the multipath filter coefficients diverge. Removed `-ffast-math` for the stable operation.
* v0.9.0-test1 to v0.9.5 had calculation error due to `volk_32f_expfast_32f()` in `IfAgc::process()` method. Fixed this by replacing to the more accurate calculation code of `volk_32f_exp_32f()`.

## No more semantic versioning

* Current version number scheme: YYYYMMDD-N (N: subnumber, starting from 0, unsigned integer)
* The semantic versioning scheme of airspy-fmradion has utterly failed.

## FYI: libusb-1.0.25 glitch

* Note: This problem has been fixed by the latest implementation of Airspy HF+ driver after [this commit](https://github.com/airspy/airspyhf/commit/3b823ad8fa729358e0729e6c1ca60ac5dfcd656e).
* The author has noticed [libusb-1.0.25 on macOS 12.2 causes segfault when stopping the code with SIGINT or SIGTERM with Airspy HF+ Discovery](https://github.com/jj1bdx/airspy-fmradion/issues/35).
* A proper fix of this is to [fix the Airspy HF+ driver](https://github.com/airspy/airspyhf/pull/31).
* [A similar case of SDR++ with ArchLinux](https://github.com/libusb/libusb/issues/1059#issuecomment-1030638617) is also reported.
* Since Version 20220205-0, a workaround is implemented to prevent data loss for this bug: the main() loop closes the audio output before calling the function which might cause this segmentation fault (SIGSEGV), which is the stopping function of the SDR source driver.
* Airspy R2 and Mini are not affected. Use the latest driver with [this fix](https://github.com/airspy/airspyone_host/commit/41c439f16818d931c4d0f8a620413ea5131c0bd6).
* You can still use 20220205-1 if you need to; there is no functional difference between 20220205-1 and 20220206-0.

[End of CHANGES.md]
