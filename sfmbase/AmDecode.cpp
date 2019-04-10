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

// For the audio_agc and if_agc functions:
/*
This software is part of libcsdr, a set of simple DSP routines for
Software Defined Radio.
Copyright (c) 2014, Andras Retzler <randras@sdr.hu>
All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ANDRAS RETZLER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <cassert>
#include <cmath>

#include "AmDecode.h"

// class AmDecoder

// Static constants.

const double AmDecoder::sample_rate_pcm = 48000;
// Half bandwidth of audio signal in Hz (4.5kHz for AM)
const double AmDecoder::bandwidth_pcm = 4500;
// Deemphasis constant in microseconds.
const double AmDecoder::default_deemphasis = 100;

AmDecoder::AmDecoder(double sample_rate_demod,
                     std::vector<IQSample::value_type> &amfilter_coeff)
    // Initialize member fields
    : m_sample_rate_demod(sample_rate_demod), m_amfilter_coeff(amfilter_coeff),
      m_baseband_mean(0), m_baseband_level(0), m_agc_last_gain(1.0),
      m_agc_peak1(0), m_agc_peak2(0), m_agc_reference(0.95),
      m_if_agc_current_gain(10.0), m_if_agc_rate(0.0007), m_if_agc_reference(0.5)

      // Construct AudioResampler for mono and stereo channels
      ,
      m_audioresampler(m_sample_rate_demod / 4.0, sample_rate_pcm)

      // Construct IfDownsampler
      ,
      m_ifdownsampler(2, FilterParameters::jj1bdx_am_48khz_div2, true, 2,
                      FilterParameters::jj1bdx_am_24khz_div2)

      // Construct AM narrow filter
      ,
      m_amfilter(1, m_amfilter_coeff, false, 1,
                 FilterParameters::delay_3taps_only_iq)

      // Construct HighPassFilterIir
      // cutoff: 60Hz for 12kHz sampling rate
      ,
      m_dcblock(0.005)

      // Construct LowPassFilterRC
      ,
      m_deemph(default_deemphasis * sample_rate_pcm * 1.0e-6) {
  // do nothing
}

void AmDecoder::process(const IQSampleVector &samples_in, SampleVector &audio) {

  // Downsample input signal to /4
  m_ifdownsampler.process(samples_in, m_buf_downsampled);

  // Apply narrower filters
  m_amfilter.process(m_buf_downsampled, m_buf_downsampled2);

  // If AGC
  if_agc(m_buf_downsampled2, m_buf_downsampled3);

  // Demodulate AM signal.
  demodulate(m_buf_downsampled3, m_buf_baseband_demod);

  // DC blocking.
  m_dcblock.process_inplace(m_buf_baseband_demod);

  // Audio AGC
  audio_agc(m_buf_baseband_demod, m_buf_baseband);

  // Measure baseband level after DC blocking.
  double baseband_mean, baseband_rms;
  samples_mean_rms(m_buf_baseband, baseband_mean, baseband_rms);
  m_baseband_mean = 0.95 * m_baseband_mean + 0.05 * baseband_mean;
  m_baseband_level = 0.95 * m_baseband_level + 0.05 * baseband_rms;

  // Upsample baseband signal.
  m_audioresampler.process(m_buf_baseband, m_buf_mono);

  // If no mono audio signal comes out, terminate and wait for next block,
  if (m_buf_mono.size() == 0) {
    audio.resize(0);
    return;
  }

  // Deemphasis
  m_deemph.process_inplace(m_buf_mono);

  // Return mono channel.
  audio = std::move(m_buf_mono);
}

// Demodulate AM signal.
inline void AmDecoder::demodulate(const IQSampleVector &samples_in,
                                  SampleVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    samples_out[i] = std::abs(samples_in[i]);
  }
}

// Audio AGC.
// Algorithm: function fastagc_ff() in
// https://github.com/simonyiszk/csdr/blob/master/libcsdr.c
inline void AmDecoder::audio_agc(const SampleVector &samples_in,
                                 SampleVector &samples_out) {
  const double agc_max_gain = 4.0;
  unsigned int n = samples_in.size();
  samples_out.resize(n);
  m_agc_buf1.resize(n);
  m_agc_buf2.resize(n);

  double agc_peak = 0;
  for (unsigned int i = 0; i < n; i++) {
    double v = fabs(samples_in[i]);
    if (v > agc_peak) {
      agc_peak = v;
    }
  }

  double target_peak = agc_peak;
  if (target_peak < m_agc_peak2) {
    target_peak = m_agc_peak2;
  }
  if (target_peak < m_agc_peak1) {
    target_peak = m_agc_peak1;
  }

  double target_gain = m_agc_reference / target_peak;
  if (target_gain > agc_max_gain) {
    target_gain = agc_max_gain;
  }

  for (unsigned int i = 0; i < n; i++) {
    double rate = (double)i / (double)n;
    double gain = (m_agc_last_gain * (1.0 - rate)) + (target_gain * rate);
    samples_out[i] = m_agc_buf1[i] * gain;
  }

  m_agc_buf1 = m_agc_buf2;
  m_agc_peak1 = m_agc_peak2;
  m_agc_buf2 = samples_in;
  m_agc_peak2 = agc_peak;
  m_agc_last_gain = target_gain;

  // fprintf(stderr, "m_agc_last_gain= %f\n", m_agc_last_gain);
}

// IF AGC.
// Algorithm: function simple_agc_ff() in
// https://github.com/simonyiszk/csdr/blob/master/libcsdr.c
inline void AmDecoder::if_agc(const IQSampleVector &samples_in,
                              IQSampleVector &samples_out) {
  const double if_agc_max_gain = 10000.0; // 80dB
  double rate = m_if_agc_rate;
  double rate_1minus = 1 - rate;
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    double amplitude = std::abs(samples_in[i]);
    double ideal_gain = m_if_agc_reference / amplitude;
    if (ideal_gain > if_agc_max_gain) {
      ideal_gain = if_agc_max_gain;
    }
    if (ideal_gain <= 0) {
      ideal_gain = 0;
    }
    m_if_agc_current_gain = ((ideal_gain - m_if_agc_current_gain) * rate) +
                            (m_if_agc_current_gain * rate_1minus);
    // fprintf(stderr, "m_if_agc_current_gain = %f\n", m_if_agc_current_gain);
    samples_out[i] = IQSample(samples_in[i].real() * m_if_agc_current_gain,
                              samples_in[i].imag() * m_if_agc_current_gain);
  }
}

// end
