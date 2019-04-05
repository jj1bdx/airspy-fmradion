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

#ifndef SOFTFM_AMDECODE_H
#define SOFTFM_AMDECODE_H

#include <cstdint>
#include <vector>

#include "AudioResampler.h"
#include "EqParameters.h"
#include "Filter.h"
#include "FilterParameters.h"
#include "IfDownsampler.h"
#include "SoftFM.h"

/** Complete decoder for FM broadcast signal. */
class AmDecoder {
public:
  static const double sample_rate_pcm;
  static const double bandwidth_pcm;
  static const double default_deemphasis;

  /**
   * Construct AM decoder.
   *
   * sample_rate_demod :: Demodulator IQ sample rate.
   */
  AmDecoder(double sample_rate_demod);

  // Process IQ samples and return audio samples.
  void process(const IQSampleVector &samples_in, SampleVector &audio);

private:
  // Demodulate AM signal.
  void demodulate(const IQSampleVector &samples_in, SampleVector &samples_out);
  // Audio AGC function.
  void audio_agc(const SampleVector &samples_in, SampleVector &samples_out);
  // IF AGC function.
  void if_agc(const IQSampleVector &samples_in, IQSampleVector &samples_out);

  // Data members.
  const double m_sample_rate_demod;
  double m_baseband_mean;
  double m_baseband_level;

  SampleVector m_agc_buf1;
  SampleVector m_agc_buf2;
  double m_agc_last_gain;
  double m_agc_peak1;
  double m_agc_peak2;
  double m_agc_reference;

  double m_if_agc_current_gain;
  const double m_if_agc_rate;
  const double m_if_agc_reference;

  IQSampleVector m_buf_downsampled;
  IQSampleVector m_buf_downsampled2;
  SampleVector m_buf_baseband_demod;
  SampleVector m_buf_baseband;
  SampleVector m_buf_mono;

  AudioResampler m_audioresampler;
  IfDownsampler m_ifdownsampler;
  HighPassFilterIir m_dcblock;
  LowPassFilterRC m_deemph;
};

#endif
