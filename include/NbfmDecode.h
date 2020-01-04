// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
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

#ifndef SOFTFM_NBFMDECODE_H
#define SOFTFM_NBFMDECODE_H

#include <cstdint>

#include "AudioResampler.h"
#include "Filter.h"
#include "FilterParameters.h"
#include "PhaseDiscriminator.h"
#include "SoftFM.h"

// Complete decoder for Narrow Band FM broadcast signal.

class NbfmDecoder {
public:
  // Static constants.
  static constexpr double sample_rate_pcm = 48000;
  // Full scale carrier frequency deviation (5kHz nominal)
  static constexpr double freq_dev = 8000;

  /**
   * Construct Narrow Band FM decoder.
   *
   * sample_rate_demod :: Demodulator IQ sample rate.
   */
  NbfmDecoder(double sample_rate_demod);

  /**
   * Process IQ samples and return audio samples.
   */
  void process(const IQSampleVector &samples_in, SampleVector &audio);

  /** Return actual frequency offset in Hz with respect to receiver LO. */
  float get_tuning_offset() const { return m_baseband_mean * freq_dev; }

  /** Return RMS baseband signal level (where nominal level is 0.707). */
  float get_baseband_level() const { return m_baseband_level; }

  // Return RMS IF level.
  float get_if_rms() const { return m_if_rms; }

private:
  // Data members.
  const double m_sample_rate_fmdemod;
  float m_baseband_mean;
  float m_baseband_level;
  float m_if_rms;

  IQSampleDecodedVector m_buf_decoded;
  SampleVector m_buf_baseband;
  SampleVector m_buf_baseband_raw;
  SampleVector m_buf_baseband_filtered;

  AudioResampler m_audioresampler_raw;
  PhaseDiscriminator m_phasedisc;
  LowPassFilterFirAudio m_audiofilter;
};

#endif
