// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2019 Kenji Rikitake, JJ1BDX
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

#ifndef SOFTFM_FMDECODE_H
#define SOFTFM_FMDECODE_H

#include <cstdint>

#include "AudioResampler.h"
#include "Filter.h"
#include "FilterParameters.h"
#include "IfAgc.h"
#include "MultipathFilter.h"
#include "PhaseDiscriminator.h"
#include "SoftFM.h"

/** Phase-locked loop for stereo pilot. */
class PilotPhaseLock {
public:
  // Static constants.
  // Expected pilot frequency (used for PPS events).
  static constexpr int pilot_frequency = 19000;
  // IF sampling rate.
  static constexpr double sample_rate_if = 384000;
  // Bandwidth (30Hz) relative to sample frequency.
  static constexpr double bandwidth = 30 / sample_rate_if;
  // Minimum pilot amplitude
  static constexpr double minsignal = 0.01;

  /** Timestamp event produced once every 19000 pilot periods. */
  struct PpsEvent {
    std::uint64_t pps_index;
    std::uint64_t sample_index;
    double block_position;
  };

  /**
   * Construct phase-locked loop.
   *
   * freq        :: 19 kHz center frequency relative to sample freq
   *                (0.5 is Nyquist)
   */
  PilotPhaseLock(double freq);

  /**
   * Process samples and extract 19 kHz pilot tone.
   * Generate phase-locked 38 kHz tone with unit amplitude.
   * pilot_shift :: true to shift pilot phase by
   *             :: using cos(2*x) instead of sin (2*x)
   *             :: (for multipath distortion detection)
   */
  void process(const SampleVector &samples_in, SampleVector &samples_out,
               bool pilot_shift);

  /** Return true if the phase-locked loop is locked. */
  bool locked() const { return m_lock_cnt >= m_lock_delay; }

  /** Return detected amplitude of pilot signal. */
  double get_pilot_level() const { return 2 * m_pilot_level; }

  // Return calculated frequency error.
  double get_freq_err() const { return m_freq_err; }

  /** Return PPS events from the most recently processed block. */
  std::vector<PpsEvent> get_pps_events() const { return m_pps_events; }

  // Erase the first PPS event.
  void erase_first_pps_event() { m_pps_events.erase(m_pps_events.begin()); }

private:
  Sample m_minfreq, m_maxfreq;
  Sample m_freq, m_phase;
  Sample m_pilot_level;
  int m_lock_delay;
  int m_lock_cnt;
  int m_pilot_periods;
  std::uint64_t m_pps_cnt;
  std::uint64_t m_sample_cnt;
  std::vector<PpsEvent> m_pps_events;
  BiquadIirFilter m_biquad_phasor_i1, m_biquad_phasor_i2;
  BiquadIirFilter m_biquad_phasor_q1, m_biquad_phasor_q2;
  FirstOrderIirFilter m_first_phase_err;
  Sample m_freq_err;
};

/** Complete decoder for FM broadcast signal. */
class FmDecoder {
public:
  // Static constants.
  // IF sampling rate.
  static constexpr double sample_rate_if = 384000;
  // Output sampling rate.
  static constexpr double sample_rate_pcm = 48000;
  // Full scale carrier frequency deviation (75 kHz for broadcast FM)
  static constexpr double freq_dev = 75000;
  // Half bandwidth of audio signal in Hz (15 kHz for broadcast FM)
  static constexpr double bandwidth_pcm = 15000;
  static constexpr double pilot_freq = 19000;
  static constexpr double default_deemphasis = 50;
  static constexpr double default_deemphasis_eu = 50; // Europe and Japan
  static constexpr double default_deemphasis_na = 75; // USA/Canada

  /**
   * Construct FM decoder.
   *
   * fmfilter_coeff    :: IQSample filter coefficients.
   * stereo            :: True to enable stereo decoding.
   * deemphasis        :: Time constant of de-emphasis filter in microseconds
   *                      (50 us for broadcast FM, 0 to disable de-emphasis).
   * pilot_shift       :: True to shift pilot signal phase
   *                   :: (use cos(2*x) instead of sin (2*x))
   *                   :: (for multipath distortion detection)
   * multipath_stages  :: Set >0 to enable multipath filter
   *                   :: (LMS adaptive filter stage number)
   */
  FmDecoder(IQSampleCoeff &fmfilter_coeff, bool stereo, double deemphasis,
            bool pilot_shift, unsigned int multipath_stages);
  /**
   * Process IQ samples and return audio samples.
   *
   * If the decoder is set in stereo mode, samples for left and right
   * channels are interleaved in the output vector (even if no stereo
   * signal is detected). If the decoder is set in mono mode, the output
   * vector only contains samples for one channel.
   */
  void process(const IQSampleVector &samples_in, SampleVector &audio);

  /** Return true if a stereo signal is detected. */
  bool stereo_detected() const { return m_stereo_detected; }

  /** Return actual frequency offset in Hz with respect to receiver LO. */
  float get_tuning_offset() const { return m_baseband_mean * freq_dev; }

  /** Return RMS baseband signal level (where nominal level is 0.707). */
  float get_baseband_level() const { return m_baseband_level; }

  /** Return amplitude of stereo pilot (nominal level is 0.1). */
  double get_pilot_level() const { return m_pilotpll.get_pilot_level(); }

  // Return RMS IF level.
  float get_if_rms() const { return m_if_rms; }

  /** Return PPS events from the most recently processed block. */
  std::vector<PilotPhaseLock::PpsEvent> get_pps_events() const {
    return m_pilotpll.get_pps_events();
  }

  // Erase the first PPS event.
  void erase_first_pps_event() { m_pilotpll.erase_first_pps_event(); }

  // Get error value of the multipath filter.
  double get_multipath_error() { return m_multipathfilter.get_error(); }

  // Get multipath filter coefficients.
  const MfCoeffVector &get_multipath_coefficients() {
    return m_multipathfilter.get_coefficients();
  }

private:
  /** Demodulate stereo L-R signal. */
  inline void demod_stereo(const SampleVector &samples_baseband,
                           SampleVector &samples_stereo);

  /** Duplicate mono signal in left/right channels. */
  inline void mono_to_left_right(const SampleVector &samples_mono,
                                 SampleVector &audio);

  /** Extract left/right channels from mono/stereo signals. */
  inline void stereo_to_left_right(const SampleVector &samples_mono,
                                   const SampleVector &samples_stereo,
                                   SampleVector &audio);

  // Fill zero signal in left/right channels.
  // (samples_mono used for the size determination only)
  inline void zero_to_left_right(const SampleVector &samples_mono,
                                 SampleVector &audio);

  // Data members.
  const IQSampleCoeff &m_fmfilter_coeff;
  const bool m_pilot_shift;
  const bool m_enable_multipath_filter;
  unsigned int m_wait_multipath_blocks;
  const unsigned int m_multipath_stages;
  const bool m_stereo_enabled;
  bool m_stereo_detected;
  float m_baseband_mean;
  float m_baseband_level;
  float m_if_rms;

  IQSampleVector m_samples_in_iffiltered;
  IQSampleVector m_samples_in_after_agc;
  IQSampleVector m_samples_in_multipathfiltered;
  IQSampleDecodedVector m_buf_decoded;
  SampleVector m_buf_baseband;
  SampleVector m_buf_baseband_raw;
  SampleVector m_buf_mono_firstout;
  SampleVector m_buf_mono;
  SampleVector m_buf_rawstereo;
  SampleVector m_buf_stereo_firstout;
  SampleVector m_buf_stereo;

  LowPassFilterFirIQ m_fmfilter;
  AudioResampler m_audioresampler_mono;
  AudioResampler m_audioresampler_stereo;
  LowPassFilterFirAudio m_pilotcut_mono;
  LowPassFilterFirAudio m_pilotcut_stereo;
  PhaseDiscriminator m_phasedisc;
  PilotPhaseLock m_pilotpll;
  HighPassFilterIir m_dcblock_mono;
  HighPassFilterIir m_dcblock_stereo;
  LowPassFilterRC m_deemph_mono;
  LowPassFilterRC m_deemph_stereo;
  IfAgc m_ifagc;
  MultipathFilter m_multipathfilter;
};

#endif
