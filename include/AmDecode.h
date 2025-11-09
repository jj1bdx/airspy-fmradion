// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2019-2024 Kenji Rikitake, JJ1BDX
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

#ifndef INCLUDE_AMDECODE_H
#define INCLUDE_AMDECODE_H

#include "AfSimpleAgc.h"
#include "Filter.h"
#include "FilterParameters.h"
#include "FineTuner.h"
#include "IfResampler.h"
#include "IfSimpleAgc.h"
#include "SoftFM.h"

/** Complete decoder for FM broadcast signal. */
class AmDecoder {
public:
  // Static constants.
  static constexpr double sample_rate_pcm = 48000;
  static constexpr double internal_rate_pcm = 48000;
  // Half bandwidth of audio signal in Hz (4.5kHz for AM)
  static constexpr double bandwidth_pcm = 4500;
  // Deemphasis constant in microseconds.
  static constexpr double deemphasis_time = 100;

  /**
   * Construct AM decoder.
   *
   * amfilter_coeff    :: IQSample Filter Coefficients.
   * mode              :: ModType for decoding mode.
   */
  AmDecoder(IQSampleCoeff &amfilter_coeff, const ModType mode);

  // Process IQ samples and return audio samples.
  void process(const IQSampleVector &samples_in, SampleVector &audio);

  // Return RMS baseband signal level (where nominal level is 0.707).
  double get_baseband_level() const { return m_baseband_level; }

  // Return AF AGC current gain.
  float get_af_agc_current_gain() const { return m_afagc.get_current_gain(); }

  // Return IF AGC current gain.
  float get_if_agc_current_gain() const { return m_ifagc.get_current_gain(); }

  // Return RMS IF level.
  float get_if_rms() const { return m_if_rms; }

private:
  // Demodulate AM signal.
  inline void demodulate_am(const IQSampleVector &samples_in,
                            IQSampleDecodedVector &samples_out);
  // Demodulate DSB signal.
  inline void demodulate_dsb(const IQSampleVector &samples_in,
                             IQSampleDecodedVector &samples_out);

  // Data members.
  const IQSampleCoeff &m_amfilter_coeff;
  const ModType m_mode;
  float m_baseband_mean;
  float m_baseband_level;
  float m_if_rms;

  IQSampleVector m_buf_filtered;
  IQSampleVector m_buf_filtered1a;
  IQSampleVector m_buf_filtered1b;
  IQSampleVector m_buf_filtered2;
  IQSampleVector m_buf_filtered3;
  IQSampleDecodedVector m_buf_decoded;
  SampleVector m_buf_baseband_demod;
  SampleVector m_buf_baseband_preagc;
  SampleVector m_buf_baseband;
  SampleVector m_buf_mono;

  LowPassFilterFirIQ m_amfilter;
  LowPassFilterFirIQ m_cwfilter;
  LowPassFilterFirIQ m_ssbfilter;
  HighPassFilterIir m_dcblock;
  LowPassFilterRC m_deemph;
  AfSimpleAgc m_afagc;
  IfSimpleAgc m_ifagc;
  FineTuner m_cw_finetuner;
  FineTuner m_wspr_ssb_up_finetuner;
  FineTuner m_wspr_ssb_down_finetuner;
};

#endif
