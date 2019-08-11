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
#include <vector>

#include "AudioResampler.h"
#include "EqParameters.h"
#include "Filter.h"
#include "FilterParameters.h"
#include "PhaseDiscriminator.h"
#include "SoftFM.h"

// Complete decoder for Narrow Band FM broadcast signal.

class NbfmDecoder {
public:
  static const double sample_rate_pcm;
  static const double freq_dev;

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
  double get_tuning_offset() const { return m_baseband_mean * freq_dev; }

  /** Return RMS baseband signal level (where nominal level is 0.707). */
  double get_baseband_level() const { return m_baseband_level; }

private:
  // Data members.
  const double m_sample_rate_fmdemod;
  double m_baseband_mean;
  double m_baseband_level;

  SampleVector m_buf_baseband;
  SampleVector m_buf_baseband_raw;
  SampleVector m_buf_baseband_filtered;

  AudioResampler m_audioresampler_raw;
  PhaseDiscriminator m_phasedisc;
  LowPassFilterFirAudio m_audiofilter;
};

#endif
