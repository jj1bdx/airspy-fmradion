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
#include "FourthConverterIQ.h"
#include "IfResampler.h"
#include "SoftFM.h"
#include "util.h"

/** Complete decoder for FM broadcast signal. */
class AmDecoder {
public:
  static const double sample_rate_pcm;
  static const double internal_rate_pcm;
  static const double cw_rate_pcm;
  static const double bandwidth_pcm;
  static const double default_deemphasis;

  /**
   * Construct AM decoder.
   *
   * sample_rate_demod :: Demodulator IQ sample rate.
   * amfilter_coeff    :: IQSample Filter Coefficients.
   * mode              :: ModType for decoding mode.
   */
  AmDecoder(double sample_rate_demod, IQSampleCoeff &amfilter_coeff,
            const ModType mode);

  // Process IQ samples and return audio samples.
  void process(const IQSampleVector &samples_in, SampleVector &audio);

  // Return RMS baseband signal level (where nominal level is 0.707).
  double get_baseband_level() const { return m_baseband_level; }

  // Return Audio AGC last gain.
  double get_agc_last_gain() const { return m_af_agc_current_gain; }

  // Return IF AGC current gain.
  double get_if_agc_current_gain() const { return m_if_agc_current_gain; }

  // Return RMS IF level.
  double get_if_rms() const { return m_if_rms; }

private:
  // Demodulate AM signal.
  inline void demodulate_am(const IQSampleVector &samples_in,
                            SampleVector &samples_out);
  // Demodulate DSB signal.
  inline void demodulate_dsb(const IQSampleVector &samples_in,
                             SampleVector &samples_out);
  // Audio AGC function.
  inline void audio_agc(const SampleVector &samples_in,
                        SampleVector &samples_out);
  // IF AGC function.
  inline void if_agc(const IQSampleVector &samples_in,
                     IQSampleVector &samples_out);
  // AF AGC function.
  inline void af_agc(const SampleVector &samples_in, SampleVector &samples_out);

  // Data members.
  const double m_sample_rate_demod;
  const IQSampleCoeff &m_amfilter_coeff;
  const ModType m_mode;
  double m_baseband_mean;
  double m_baseband_level;
  double m_if_rms;

  SampleVector m_agc_buf1;
  SampleVector m_agc_buf2;
  double m_agc_last_gain;
  double m_agc_peak1;
  double m_agc_peak2;
  double m_agc_reference;

  double m_af_agc_current_gain;
  double m_af_agc_rate;
  double m_af_agc_reference;
  double m_af_agc_max_gain;

  double m_if_agc_current_gain;
  double m_if_agc_rate;
  double m_if_agc_reference;
  double m_if_agc_max_gain;

  IQSampleVector m_buf_downsampled;
  IQSampleVector m_buf_downsampled2;
  IQSampleVector m_buf_downsampled2a;
  IQSampleVector m_buf_downsampled2b;
  IQSampleVector m_buf_downsampled2c;
  IQSampleVector m_buf_downsampled3;
  SampleVector m_buf_baseband_demod;
  SampleVector m_buf_baseband;
  SampleVector m_buf_mono;

  AudioResampler m_audioresampler;
  IfResampler m_ifresampler;
  LowPassFilterFirIQ m_amfilter;
  FourthConverterIQ m_upshifter;
  FourthConverterIQ m_downshifter;
  LowPassFilterFirIQ m_ssbshiftfilter;
  IfResampler m_cw_downsampler;
  IfResampler m_cw_upsampler;
  FourthConverterIQ m_upshifter_cw;
  LowPassFilterFirIQ m_cwshiftfilter;
  HighPassFilterIir m_dcblock;
  LowPassFilterRC m_deemph;
};

#endif
