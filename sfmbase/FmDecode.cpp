// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2019-2022 Kenji Rikitake, JJ1BDX
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "FmDecode.h"
#include "Utility.h"

// Define this to print PLL filter messages
// #define DEBUG_PLL_FILTER

// class PilotPhaseLock

// Construct phase-locked loop.
PilotPhaseLock::PilotPhaseLock(double freq)
    : // Set min/max locking frequencies.
      m_minfreq((freq - bandwidth) * 2.0 * M_PI),
      m_maxfreq((freq + bandwidth) * 2.0 * M_PI),
      // Set valid signal threshold.
      // Initialize frequency and phase.
      m_freq(freq * 2.0 * M_PI), m_phase(0),
      // Lock decision time: 0.5 second (for 30Hz bandwidth)
      m_pilot_level(0), m_lock_delay(int(15.0 / bandwidth)), m_lock_cnt(0),
      // Initialize PPS generator.
      m_pilot_periods(0), m_pps_cnt(0), m_sample_cnt(0), m_pps_events(0),
      // approx 30Hz LPF by 2nd-order biquad IIR Butterworth filter
      // Caution: use only once for stable PLL locking
      m_biquad_phasor_i1(1.46974784e-06, 0, 0, -1.99682419, 0.996825659),
      m_biquad_phasor_q1(1.46974784e-06, 0, 0, -1.99682419, 0.996825659),
      // differentiator-like 1st-order inverse LPF (not really an HPF)
      m_first_phase_err(0.000304341788, -0.000304324564, 0), m_freq_err(0) {
  // do nothing
}

// Process samples and generate the 38kHz locked tone.
void PilotPhaseLock::process(const SampleVector &samples_in,
                             SampleVector &samples_out, bool pilot_shift) {
  unsigned int n = samples_in.size();

  samples_out.resize(n);

  bool was_locked = (m_lock_cnt >= m_lock_delay);
  m_pps_events.clear();

  if (n > 0) {
    m_pilot_level = 1000.0;
  } else {
    // n == 0
    // Do nothing when the input size is 0
    return;
  }

  for (unsigned int i = 0; i < n; i++) {

    // Generate locked pilot tone.
    Sample psin = std::sin(m_phase);
    Sample pcos = std::cos(m_phase);

    // Generate double-frequency output.
    if (pilot_shift) {
      // Use cos(2*x) to shift phase for pi/4 (90 degrees)
      // cos(2*x) = 2 * cos(x) * cos(x) - 1
      samples_out[i] = 2 * pcos * pcos - 1;
    } else {
      // Proper phase: not shifted
      // sin(2*x) = 2 * sin(x) * cos(x)
      samples_out[i] = 2 * psin * pcos;
    }

    // Multiply locked tone with input.
    Sample x = samples_in[i];
    Sample phasor_i = psin * x;
    Sample phasor_q = pcos * x;

    // Run IQ phase error through biquad LPFs once.
    Sample new_phasor_i = m_biquad_phasor_i1.process(phasor_i);
    Sample new_phasor_q = m_biquad_phasor_q1.process(phasor_q);

    // Convert I/Q ratio to estimate of phase error.
    // Note: maximum phase error during the locked state is +- 0.02 radian.
    // Sample phase_err = atan2(phasor_q, phasor_i);
    Sample phase_err = Utility::fast_atan2f(new_phasor_q, new_phasor_i);

    // Detect pilot level (conservative).
    m_pilot_level = std::min(m_pilot_level, new_phasor_i);

    // Run phase error through loop filter and update frequency estimate.
    // After the loop filter, the phase error is integrated to produce
    // the frequency. Then the frequency is integrated to produce the phase.
    // These two integrators form the two remaining poles, both at z = 1.

    Sample new_phase_err = m_first_phase_err.process(phase_err);
    m_freq_err = new_phase_err;
    m_freq += m_freq_err;

    // Limit frequency to allowable range.
    m_freq = std::max(m_minfreq, std::min(m_maxfreq, m_freq));

#ifdef DEBUG_PLL_FILTER
    if (i == 0) {
      fprintf(stderr,
              "m_freq = %.9g, m_freq_err = %.9g, "
              "m_pilot_level = %.9g\n",
              m_freq * FmDecoder::sample_rate_if / 2 / M_PI,
              m_freq_err * FmDecoder::sample_rate_if / 2 / M_PI, m_pilot_level);
    }
#endif

    // Update locked phase.
    m_phase += m_freq;
    if (m_phase > 2.0 * M_PI) {
      m_phase -= 2.0 * M_PI;
      m_pilot_periods++;

      // Generate pulse-per-second.
      if (m_pilot_periods == pilot_frequency) {
        m_pilot_periods = 0;
        if (was_locked) {
          struct PpsEvent ev;
          ev.pps_index = m_pps_cnt;
          ev.sample_index = m_sample_cnt + i;
          ev.block_position = double(i) / double(n);
          m_pps_events.push_back(ev);
          m_pps_cnt++;
        }
      }
    }
  }

  // Update lock status.
  if (2 * m_pilot_level > minsignal) {
    if (m_lock_cnt < m_lock_delay) {
      m_lock_cnt += n;
    }
  } else {
    m_lock_cnt = 0;
  }

  // Drop PPS events when pilot not locked.
  if (m_lock_cnt < m_lock_delay) {
    m_pilot_periods = 0;
    m_pps_cnt = 0;
    m_pps_events.clear();
  }

  // Update sample counter.
  m_sample_cnt += n;
}

// class FmDecoder

FmDecoder::FmDecoder(bool fmfilter_enable, IQSampleCoeff &fmfilter_coeff,
                     bool stereo, double deemphasis, bool pilot_shift,
                     unsigned int multipath_stages)
    // Initialize member fields
    : m_fmfilter_enable(fmfilter_enable), m_fmfilter_coeff(fmfilter_coeff),
      m_pilot_shift(pilot_shift),
      m_enable_multipath_filter((multipath_stages > 0)),
      // Wait first 100 blocks to enable the multipath filter
      m_wait_multipath_blocks(100), m_multipath_stages(multipath_stages),
      m_stereo_enabled(stereo), m_stereo_detected(false), m_baseband_mean(0),
      m_baseband_level(0), m_if_rms(0.0)

      // Construct FM narrow filter
      ,
      m_fmfilter(m_fmfilter_coeff, 1)

      // Construct AudioResampler for mono and stereo channels
      ,
      m_audioresampler_mono(sample_rate_if, sample_rate_pcm),
      m_audioresampler_stereo(sample_rate_if, sample_rate_pcm)

      // Construct 19kHz pilot signal cut filter
      ,
      m_pilotcut_mono(FilterParameters::jj1bdx_48khz_fmaudio),
      m_pilotcut_stereo(FilterParameters::jj1bdx_48khz_fmaudio)

      // Construct PhaseDiscriminator
      ,
      m_phasedisc(freq_dev / sample_rate_if)

      // Construct PilotPhaseLock
      ,
      m_pilotpll(pilot_freq / sample_rate_if) // freq

      // Construct HighPassFilterIir
      // cutoff: 4.8Hz for 48kHz sampling rate
      ,
      m_dcblock_mono(0.0001), m_dcblock_stereo(0.0001)

      // Construct LowPassFilterRC for deemphasis
      // Note: sampling rate is of the FM demodulator
      ,
      m_deemph_mono((deemphasis == 0) ? 1.0
                                      : (deemphasis * sample_rate_if * 1.0e-6)),
      m_deemph_stereo(
          (deemphasis == 0) ? 1.0 : (deemphasis * sample_rate_if * 1.0e-6))

      // Construct IF AGC
      ,
      m_ifagc(1.0, 100000.0, 0.0001)

      // Construct multipath filter
      // for 384kHz IF: 288 -> 750 microseconds (288/384000 * 1000000)
      ,
      m_multipathfilter(m_enable_multipath_filter ? m_multipath_stages : 1)

{
  // Do nothing
}

void FmDecoder::process(const IQSampleVector &samples_in, SampleVector &audio) {

  // If no sampled baseband signal comes out,
  // terminate and wait for next block,
  if (samples_in.size() == 0) {
    audio.resize(0);
    return;
  }

  // Measure IF RMS level.
  m_if_rms = Utility::rms_level_sample(samples_in);

  // Apply IF filter if IF resampler is enabled
  if (m_fmfilter_enable) {
    m_fmfilter.process(samples_in, m_samples_in_iffiltered);
  } else {
    m_samples_in_iffiltered = std::move(samples_in);
  }

  // Perform IF AGC.
  m_ifagc.process(m_samples_in_iffiltered, m_samples_in_after_agc);

  if (m_wait_multipath_blocks > 0) {
    m_wait_multipath_blocks--;
    // No multipath filter applied.
    m_samples_in_multipathfiltered = std::move(m_samples_in_after_agc);
  } else {
    if (m_enable_multipath_filter) {
      // Apply multipath filter.
      bool done_ok = m_multipathfilter.process(m_samples_in_after_agc,
                                               m_samples_in_multipathfiltered);
      // Check if the error evaluation becomes invalid/infinite.
      if (!done_ok) {
        // Reset the filter coefficients.
        m_multipathfilter.initialize_coefficients();
        // fprintf(stderr, "Reset Multipath Filter coefficients\n");
        // Discard the invalid filter output, and
        // use the no-filter input after resetting the filter.
        m_samples_in_multipathfiltered = std::move(m_samples_in_after_agc);
      }
    } else {
      // No multipath filter applied.
      m_samples_in_multipathfiltered = std::move(m_samples_in_after_agc);
    }
  }

  // Demodulate FM to MPX signal.
  m_phasedisc.process(m_samples_in_multipathfiltered, m_buf_decoded);

  // If no downsampled baseband signal comes out,
  // terminate and wait for next block,
  size_t decoded_size = m_buf_decoded.size();
  if (decoded_size == 0) {
    audio.resize(0);
    return;
  }

  // Remove possible NaNs and irregular values
  Utility::remove_nans(m_buf_decoded);

  // Convert decoded data to baseband data
  m_buf_baseband.resize(decoded_size);
  volk_32f_convert_64f(m_buf_baseband.data(), m_buf_decoded.data(),
                       decoded_size);

  // Measure baseband level.
  float baseband_mean, baseband_rms;
  Utility::samples_mean_rms(m_buf_decoded, baseband_mean, baseband_rms);
  m_baseband_mean = 0.95 * m_baseband_mean + 0.05 * baseband_mean;
  m_baseband_level = 0.95 * m_baseband_level + 0.05 * baseband_rms;

  // The following function must be executed anyway
  // even if the mono audio resampler output does not come out.
  if (m_stereo_enabled) {
    // Lock on stereo pilot,
    // and remove locked 19kHz tone from the composite signal.
    m_pilotpll.process(m_buf_baseband, m_buf_rawstereo, m_pilot_shift);

    // Force-set this flag to true to measure stereo PLL phase noise
    // m_stereo_detected = true;
    // Use locked flag for the normal use
    m_stereo_detected = m_pilotpll.locked();

    // Demodulate stereo signal.
    demod_stereo(m_buf_baseband, m_buf_rawstereo);

    // Deemphasize the stereo (L-R) signal if not for QMM.
    if (!m_pilot_shift) {
      m_deemph_stereo.process_inplace(m_buf_rawstereo);
    }

    // Downsample.
    // NOTE: This MUST be done even if no stereo signal is detected yet,
    // because the downsamplers for mono and stereo signal must be
    // kept in sync.
    m_audioresampler_stereo.process(m_buf_rawstereo, m_buf_stereo_firstout);
  }

  // Deemphasize the mono audio signal.
  m_deemph_mono.process_inplace(m_buf_baseband);

  // Extract mono audio signal.
  m_audioresampler_mono.process(m_buf_baseband, m_buf_mono_firstout);
  // If no mono audio signal comes out, terminate and wait for next block,
  if (m_buf_mono_firstout.size() == 0) {
    audio.resize(0);
    return;
  }
  // Filter out mono 19kHz pilot signal.
  m_pilotcut_mono.process(m_buf_mono_firstout, m_buf_mono);
  // DC blocking
  m_dcblock_mono.process_inplace(m_buf_mono);

  if (m_stereo_enabled) {
    // Filter out mono 19kHz pilot signal.
    m_pilotcut_stereo.process(m_buf_stereo_firstout, m_buf_stereo);
    // DC blocking
    m_dcblock_stereo.process_inplace(m_buf_stereo);

    if (m_stereo_detected) {
      if (m_pilot_shift) {
        // Duplicate L-R shifted output in left/right channels.
        mono_to_left_right(m_buf_stereo, audio);
      } else {
        // Extract left/right channels from (L+R) / (L-R) signals.
        stereo_to_left_right(m_buf_mono, m_buf_stereo, audio);
      }
    } else {
      if (m_pilot_shift) {
        // Fill zero output in left/right channels.
        zero_to_left_right(m_buf_stereo, audio);
      } else {
        // Duplicate mono signal in left/right channels.
        mono_to_left_right(m_buf_mono, audio);
      }
    }
  } else {
    // Just return mono channel.
    audio = std::move(m_buf_mono);
  }
}

// Demodulate stereo L-R signal.
inline void FmDecoder::demod_stereo(const SampleVector &samples_baseband,
                                    SampleVector &samples_rawstereo) {
  // Multiply the baseband signal with the double-frequency pilot,
  // and multiply by 2.00 to get the full amplitude.

  unsigned int n = samples_baseband.size();
  assert(n == samples_rawstereo.size());

  // libvolk equivalent function of the following loop:
  // for (unsigned int i = 0; i < n; i++) {
  //  samples_rawstereo[i] *= 2.0 * samples_baseband[i];
  // }
  volk_64f_x2_multiply_64f(samples_rawstereo.data(), samples_rawstereo.data(),
                           samples_baseband.data(), n);
  Utility::adjust_gain(samples_rawstereo, 2.0);
}

// Duplicate mono signal in left/right channels.
inline void FmDecoder::mono_to_left_right(const SampleVector &samples_mono,
                                          SampleVector &audio) {
  unsigned int n = samples_mono.size();

  audio.resize(2 * n);
  for (unsigned int i = 0; i < n; i++) {
    Sample m = samples_mono[i];
    audio[2 * i] = m;
    audio[2 * i + 1] = m;
  }
}

// Extract left/right channels from (L+R) / (L-R) signals.
inline void FmDecoder::stereo_to_left_right(const SampleVector &samples_mono,
                                            const SampleVector &samples_stereo,
                                            SampleVector &audio) {
  unsigned int n = samples_mono.size();
  assert(n == samples_stereo.size());

  audio.resize(2 * n);
  for (unsigned int i = 0; i < n; i++) {
    Sample m = samples_mono[i];
    // L-R singal is boosted by 1.017
    // for better separation (suggested by Teruhiko Hayashi)
    Sample s = 1.017 * samples_stereo[i];
    audio[2 * i] = m + s;
    audio[2 * i + 1] = m - s;
  }
}

// Fill zero signal in left/right channels.
// (samples_mono used for the size determination only)
inline void FmDecoder::zero_to_left_right(const SampleVector &samples_mono,
                                          SampleVector &audio) {
  unsigned int n = samples_mono.size();

  audio.resize(2 * n);
  for (unsigned int i = 0; i < n; i++) {
    audio[2 * i] = 0.0;
    audio[2 * i + 1] = 0.0;
  }
}

// end
