// NGSoftFM - Software decoder for FM broadcast radio with RTL-SDR
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2018 Kenji Rikitake, JJ1BDX
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

#include <cassert>
#include <cmath>

#include "FmDecode.h"
#include "fastatan2.h"

// Compute RMS and peak levels
// over a small prefix of the specified sample vector.
void rms_peak_level_approx(const IQSampleVector &samples,
        double &rms, double &peak) {
  unsigned int n = samples.size();
  n = (n + 63) / 64;

  IQSample::value_type level = 0;
  IQSample::value_type peak_level = 0;
  for (unsigned int i = 0; i < n; i++) {
    const IQSample &s = samples[i];
    IQSample::value_type re = s.real(), im = s.imag();
    IQSample::value_type sqsum = re * re + im * im;
    level += sqsum;
    peak_level = std::max(peak_level, sqsum);
  }
  rms = sqrt(level / n);
  peak = sqrt(peak_level);
}

/* ****************  class PhaseDiscriminator  **************** */

// Construct phase discriminator.
PhaseDiscriminator::PhaseDiscriminator(double max_freq_dev)
    : m_freq_scale_factor(1.0 / (max_freq_dev * 2.0 * M_PI)) {}

// Process samples.
void PhaseDiscriminator::process(const IQSampleVector &samples_in,
                                 SampleVector &samples_out) {
  unsigned int n = samples_in.size();
  IQSample s0 = m_last1_sample;

  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    IQSample s1(samples_in[i]);
    IQSample d(conj(s0) * s1);
    Sample w = atan2(d.imag(), d.real());
    // Sample w = fastatan2(d.imag(), d.real()); // fast approximation of atan2
    samples_out[i] = w * m_freq_scale_factor;
    s0 = s1;
  }

  m_last2_sample = m_last1_sample;
  m_last1_sample = s0;
}

// class DiscriminatorEqualizer

// Construct equalizer for phase discriminator.
// TODO: value optimized for 960kHz sampling rate
DiscriminatorEqualizer::DiscriminatorEqualizer(double ifeq_static_gain,
                                               double ifeq_fit_factor)
    : m_static_gain(ifeq_static_gain), m_fit_factor(ifeq_fit_factor),
      m_last1_sample(0.0) {}

void DiscriminatorEqualizer::process(const SampleVector &samples_in,
                                     SampleVector &samples_out) {
  unsigned int n = samples_in.size();
  Sample s0 = m_last1_sample;
  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    Sample s1 = samples_in[i];
    Sample mov1 = (s0 + s1) / 2.0;
    samples_out[i] = m_static_gain * s1 - m_fit_factor * mov1;
    s0 = s1;
  }
  m_last1_sample = s0;
}

/* ****************  class PilotPhaseLock  **************** */

// Construct phase-locked loop.
PilotPhaseLock::PilotPhaseLock(double freq, double bandwidth,
                               double minsignal) {
  /*
   * This is a type-2, 4th order phase-locked loop.
   *
   * Open-loop transfer function:
   *   G(z) = K * (z - q1) / ((z - p1) * (z - p2) * (z - 1) * (z - 1))
   *   K  = 3.788 * (bandwidth * 2 * Pi)**3
   *   q1 = exp(-0.1153 * bandwidth * 2*Pi)
   *   p1 = exp(-1.146 * bandwidth * 2*Pi)
   *   p2 = exp(-5.331 * bandwidth * 2*Pi)
   *
   * I don't understand what I'm doing; hopefully it will work.
   */

  // Set min/max locking frequencies.
  m_minfreq = (freq - bandwidth) * 2.0 * M_PI;
  m_maxfreq = (freq + bandwidth) * 2.0 * M_PI;

  // Set valid signal threshold.
  m_minsignal = minsignal;
  m_lock_delay = int(20.0 / bandwidth);
  m_lock_cnt = 0;
  m_pilot_level = 0;

  // Create 2nd order filter for I/Q representation of phase error.
  // Filter has two poles, unit DC gain.
  double p1 = exp(-1.146 * bandwidth * 2.0 * M_PI);
  double p2 = exp(-5.331 * bandwidth * 2.0 * M_PI);
  m_phasor_a1 = -p1 - p2;
  m_phasor_a2 = p1 * p2;
  m_phasor_b0 = 1 + m_phasor_a1 + m_phasor_a2;

  // Create loop filter to stabilize the loop.
  double q1 = exp(-0.1153 * bandwidth * 2.0 * M_PI);
  m_loopfilter_b0 = 0.62 * bandwidth * 2.0 * M_PI;
  m_loopfilter_b1 = -m_loopfilter_b0 * q1;

  // After the loop filter, the phase error is integrated to produce
  // the frequency. Then the frequency is integrated to produce the phase.
  // These integrators form the two remaining poles, both at z = 1.

  // Initialize frequency and phase.
  m_freq = freq * 2.0 * M_PI;
  m_phase = 0;

  m_phasor_i1 = 0;
  m_phasor_i2 = 0;
  m_phasor_q1 = 0;
  m_phasor_q2 = 0;
  m_loopfilter_x1 = 0;

  // Initialize PPS generator.
  m_pilot_periods = 0;
  m_pps_cnt = 0;
  m_sample_cnt = 0;
}

// Process samples and generate the 38kHz locked tone;
// remove remained locked 19kHz tone from samples_in if locked.
void PilotPhaseLock::process(SampleVector &samples_in,
                             SampleVector &samples_out, bool pilot_shift) {
  unsigned int n = samples_in.size();

  samples_out.resize(n);

  bool was_locked = (m_lock_cnt >= m_lock_delay);
  m_pps_events.clear();

  if (n > 0) {
    m_pilot_level = 1000.0;
  }

  for (unsigned int i = 0; i < n; i++) {

    // Generate locked pilot tone.
    Sample psin = sin(m_phase);
    Sample pcos = cos(m_phase);

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

    // Run IQ phase error through low-pass filter.
    phasor_i = m_phasor_b0 * phasor_i - m_phasor_a1 * m_phasor_i1 -
               m_phasor_a2 * m_phasor_i2;
    phasor_q = m_phasor_b0 * phasor_q - m_phasor_a1 * m_phasor_q1 -
               m_phasor_a2 * m_phasor_q2;
    m_phasor_i2 = m_phasor_i1;
    m_phasor_i1 = phasor_i;
    m_phasor_q2 = m_phasor_q1;
    m_phasor_q1 = phasor_q;

    // Convert I/Q ratio to estimate of phase error.
    // float <-> double conversion error exists, but anyway...
    Sample phase_err = fastatan2(phasor_q, phasor_i);

    // Detect pilot level (conservative).
    m_pilot_level = std::min(m_pilot_level, phasor_i);

    // Run phase error through loop filter and update frequency estimate.
    m_freq += m_loopfilter_b0 * phase_err + m_loopfilter_b1 * m_loopfilter_x1;
    m_loopfilter_x1 = phase_err;

    // Limit frequency to allowable range.
    m_freq = std::max(m_minfreq, std::min(m_maxfreq, m_freq));

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

    // Remove detected 19kHz tone from samples_in if locked.
    if (was_locked) {
      samples_in[i] -= psin * m_pilot_level * 2.0;
    }
  }

  // Update lock status.
  if (2 * m_pilot_level > m_minsignal) {
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

/* ****************  class FmDecoder  **************** */

FmDecoder::FmDecoder(double sample_rate_if, double ifeq_static_gain,
                     double ifeq_fit_factor, double tuning_offset,
                     double sample_rate_pcm, bool stereo, double deemphasis,
                     double bandwidth_if, double freq_dev, double bandwidth_pcm,
                     unsigned int downsample, bool pilot_shift)

    // Initialize member fields
    : m_sample_rate_if(sample_rate_if),
      m_sample_rate_baseband(sample_rate_if / downsample),
      m_tuning_table_size(finetuner_table_size),
      m_tuning_shift(lrint(-double(finetuner_table_size) * tuning_offset /
                           sample_rate_if)),
      m_freq_dev(freq_dev), m_downsample(downsample),
      m_pilot_shift(pilot_shift), m_stereo_enabled(stereo),
      m_stereo_detected(false), m_if_level(0), m_baseband_mean(0),
      m_baseband_level(0)

      // Construct FineTuner
      ,
      m_finetuner(m_tuning_table_size, m_tuning_shift)

      // Construct LowPassFilterFirIQ
      ,
      m_iffilter(10, bandwidth_if / sample_rate_if)

      // Construct DiscriminatorEqualizer
      ,
      m_disceq(ifeq_static_gain, ifeq_fit_factor)

      // Construct PhaseDiscriminator
      ,
      m_phasedisc(freq_dev / sample_rate_if)

      // Construct DownsampleFilter for baseband
      ,
      m_resample_baseband(8 * downsample, 0.4 / downsample, downsample, true)

      // Construct PilotPhaseLock
      ,
      m_pilotpll(pilot_freq / m_sample_rate_baseband, // freq
                 50 / m_sample_rate_baseband,         // bandwidth
                 0.01)                                // minsignal (was 0.04)

      // Construct DownsampleFilter for mono channel
      ,
      m_resample_mono(int(m_sample_rate_baseband / 1000.0),     // filter_order
                      bandwidth_pcm / m_sample_rate_baseband,   // cutoff
                      m_sample_rate_baseband / sample_rate_pcm, // downsample
                      false) // integer_factor

      // Construct DownsampleFilter for stereo channel
      ,
      m_resample_stereo(int(m_sample_rate_baseband / 1000.0),   // filter_order
                        bandwidth_pcm / m_sample_rate_baseband, // cutoff
                        m_sample_rate_baseband / sample_rate_pcm, // downsample
                        false) // integer_factor

      // Construct HighPassFilterIir
      ,
      m_dcblock_mono(30.0 / sample_rate_pcm),
      m_dcblock_stereo(30.0 / sample_rate_pcm)

      // Construct LowPassFilterRC
      ,
      m_deemph_mono(
          (deemphasis == 0) ? 1.0 : (deemphasis * sample_rate_pcm * 1.0e-6)),
      m_deemph_stereo(
          (deemphasis == 0) ? 1.0 : (deemphasis * sample_rate_pcm * 1.0e-6))

{
  // nothing more to do
}

void FmDecoder::process(const IQSampleVector &samples_in, SampleVector &audio) {
  // Fine tuning.
  m_finetuner.process(samples_in, m_buf_iftuned);

  // Low pass filter to isolate station.
  m_iffilter.process(m_buf_iftuned, m_buf_iffiltered);

  // Measure IF level.
  double if_rms;
  rms_peak_level_approx(m_buf_iffiltered, if_rms, m_if_peak_level);
  m_if_level = 0.95 * m_if_level + 0.05 * (double)if_rms;

  // Extract carrier frequency.
  m_phasedisc.process(m_buf_iffiltered, m_buf_baseband_raw);

  // Compensate 0th-hold aperture effect
  // by applying the equalizer to the discriminator output.
  m_disceq.process(m_buf_baseband_raw, m_buf_baseband);

  // Downsample baseband signal to reduce processing.
  if (m_downsample > 1) {
    SampleVector tmp(move(m_buf_baseband));
    m_resample_baseband.process(tmp, m_buf_baseband);
  }

  // Measure baseband level.
  double baseband_mean, baseband_rms;
  samples_mean_rms(m_buf_baseband, baseband_mean, baseband_rms);
  m_baseband_mean = 0.95 * m_baseband_mean + 0.05 * baseband_mean;
  m_baseband_level = 0.95 * m_baseband_level + 0.05 * baseband_rms;

  // Lock on stereo pilot,
  // and remove locked 19kHz tone from the composite signal.
  m_pilotpll.process(m_buf_baseband, m_buf_rawstereo, m_pilot_shift);
  m_stereo_detected = m_pilotpll.locked();

  // Extract mono audio signal.
  m_resample_mono.process(m_buf_baseband, m_buf_mono);
  // DC blocking
  m_dcblock_mono.process_inplace(m_buf_mono);

  if (m_stereo_enabled) {

    // Demodulate stereo signal.
    demod_stereo(m_buf_baseband, m_buf_rawstereo);

    // Extract audio and downsample.
    // NOTE: This MUST be done even if no stereo signal is detected yet,
    // because the downsamplers for mono and stereo signal must be
    // kept in sync.
    m_resample_stereo.process(m_buf_rawstereo, m_buf_stereo);

    // DC blocking
    m_dcblock_stereo.process_inplace(m_buf_stereo);

    if (m_stereo_detected) {
      if (m_pilot_shift) {
        // Duplicate L-R shifted output in left/right channels.
        // No deemphasis
        mono_to_left_right(m_buf_stereo, audio);
      } else {
        // Extract left/right channels from (L+R) / (L-R) signals.
        stereo_to_left_right(m_buf_mono, m_buf_stereo, audio);
        // L and R de-emphasis.
        m_deemph_stereo.process_interleaved_inplace(audio);
      }
    } else {
      if (m_pilot_shift) {
        // Fill zero output in left/right channels.
        zero_to_left_right(m_buf_stereo, audio);
      } else {
        // De-emphasis.
        m_deemph_mono.process_inplace(m_buf_mono);
        // Duplicate mono signal in left/right channels.
        mono_to_left_right(m_buf_mono, audio);
      }
    }
  } else {
    m_deemph_mono.process_inplace(m_buf_mono); //  De-emphasis.
    // Just return mono channel.
    audio = move(m_buf_mono);
  }
}

// Demodulate stereo L-R signal.
void FmDecoder::demod_stereo(const SampleVector &samples_baseband,
                             SampleVector &samples_rawstereo) {
  // Just multiply the baseband signal with the double-frequency pilot.
  // And multiply by 2.00 to get the full amplitude.
  // That's all.

  unsigned int n = samples_baseband.size();
  assert(n == samples_rawstereo.size());

  for (unsigned int i = 0; i < n; i++) {
    samples_rawstereo[i] *= 2.00 * samples_baseband[i];
  }
}

// Duplicate mono signal in left/right channels.
void FmDecoder::mono_to_left_right(const SampleVector &samples_mono,
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
void FmDecoder::stereo_to_left_right(const SampleVector &samples_mono,
                                     const SampleVector &samples_stereo,
                                     SampleVector &audio) {
  unsigned int n = samples_mono.size();
  assert(n == samples_stereo.size());

  audio.resize(2 * n);
  for (unsigned int i = 0; i < n; i++) {
    Sample m = samples_mono[i];
    Sample s = samples_stereo[i];
    audio[2 * i] = m + s;
    audio[2 * i + 1] = m - s;
  }
}

// Fill zero signal in left/right channels.
// (samples_mono used for the size determination only)
void FmDecoder::zero_to_left_right(const SampleVector &samples_mono,
                                   SampleVector &audio) {
  unsigned int n = samples_mono.size();

  audio.resize(2 * n);
  for (unsigned int i = 0; i < n; i++) {
    audio[2 * i] = 0.0;
    audio[2 * i + 1] = 0.0;
  }
}

/* end */
