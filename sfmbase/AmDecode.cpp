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

// For the af_agc and if_agc functions:
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

AmDecoder::AmDecoder(double sample_rate_demod, IQSampleCoeff &amfilter_coeff,
                     const ModType mode)
    // Initialize member fields
    : m_sample_rate_demod(sample_rate_demod), m_amfilter_coeff(amfilter_coeff),
      m_mode(mode), m_baseband_mean(0), m_baseband_level(0),
      m_af_agc_current_gain(1.0), m_af_agc_rate(0.0010),
      m_af_agc_reference(1.0), m_af_agc_max_gain(5.0),
      m_if_agc_current_gain(1.0), m_if_agc_rate(0.0007),
      m_if_agc_reference(0.7), m_if_agc_max_gain(100000.0)

      // Construct AudioResampler for mono and stereo channels
      ,
      m_audioresampler(sample_rate_pcm / 4.0, sample_rate_pcm)

      // Construct IfResampler to first convert to 48kHz
      ,
      m_ifresampler(m_sample_rate_demod, sample_rate_pcm / 4.0)

      // Construct AM narrow filter
      ,
      m_amfilter(m_amfilter_coeff, 1)

      // IF down/upshifter
      ,
      m_upshifter(true), m_downshifter(false)

      // SSB shifted-audio filter from 3 to 6kHz
      ,
      m_ssbshiftfilter(FilterParameters::jj1bdx_ssb_3to6khz, 1)

      // Construct HighPassFilterIir
      // cutoff: 60Hz for 12kHz sampling rate
      ,
      m_dcblock(0.005)

      // Construct LowPassFilterRC
      ,
      m_deemph(default_deemphasis * sample_rate_pcm * 1.0e-6)

{
  switch (m_mode) {
  // Reduce output level for SSB to prevent clipping
  case ModType::USB:
  case ModType::LSB:
    m_af_agc_reference = 0.25;
    m_if_agc_reference = 0.25;
    break;
  default:
    break;
  }
}

void AmDecoder::process(const IQSampleVector &samples_in, SampleVector &audio) {

  // Downsample input signal to /4
  m_ifresampler.process(samples_in, m_buf_downsampled);

  // If no downsampled signal comes out, terminate and wait for next block.
  if (m_buf_downsampled.size() == 0) {
    audio.resize(0);
    return;
  }

  // Apply narrower filters
  m_amfilter.process(m_buf_downsampled, m_buf_downsampled2);

  // Shift Fs/4 and filter Fs/4~Fs/2 frequency bandwidth only
  switch (m_mode) {
  case ModType::USB:
    m_upshifter.process(m_buf_downsampled2, m_buf_downsampled2a);
    m_ssbshiftfilter.process(m_buf_downsampled2a, m_buf_downsampled2b);
    m_downshifter.process(m_buf_downsampled2b, m_buf_downsampled2);
    break;
  case ModType::LSB:
    m_downshifter.process(m_buf_downsampled2, m_buf_downsampled2a);
    m_ssbshiftfilter.process(m_buf_downsampled2a, m_buf_downsampled2b);
    m_upshifter.process(m_buf_downsampled2b, m_buf_downsampled2);
    break;
  default:
    // Do nothing here
    break;
  }

  // If AGC
  if_agc(m_buf_downsampled2, m_buf_downsampled3);

  // Demodulate AM/DSB signal.
  switch (m_mode) {
  case ModType::FM:
    // Force error
    assert(m_mode != ModType::FM);
    break;
  case ModType::AM:
    demodulate_am(m_buf_downsampled3, m_buf_baseband_demod);
    break;
  case ModType::DSB:
  case ModType::USB:
  case ModType::LSB:
    demodulate_dsb(m_buf_downsampled3, m_buf_baseband_demod);
    break;
  }

  // DC blocking.
  m_dcblock.process_inplace(m_buf_baseband_demod);

  // Audio AGC
  af_agc(m_buf_baseband_demod, m_buf_baseband);

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
inline void AmDecoder::demodulate_am(const IQSampleVector &samples_in,
                                     SampleVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    samples_out[i] = std::abs(samples_in[i]);
  }
}

// Demodulate DSB signal.
inline void AmDecoder::demodulate_dsb(const IQSampleVector &samples_in,
                                      SampleVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    samples_out[i] = samples_in[i].real();
  }
}

// IF AGC.
// Algorithm: function simple_agc_ff() in
// https://github.com/simonyiszk/csdr/blob/master/libcsdr.c
inline void AmDecoder::if_agc(const IQSampleVector &samples_in,
                              IQSampleVector &samples_out) {
  double rate = m_if_agc_rate;
  double rate_1minus = 1 - rate;
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    double amplitude = std::abs(samples_in[i]);
    double ideal_gain = m_if_agc_reference / amplitude;
    if (ideal_gain > m_if_agc_max_gain) {
      ideal_gain = m_if_agc_max_gain;
    }
    if (ideal_gain <= 0) {
      ideal_gain = 0;
    }
    m_if_agc_current_gain = ((ideal_gain - m_if_agc_current_gain) * rate) +
                            (m_if_agc_current_gain * rate_1minus);
    samples_out[i] = IQSample(samples_in[i].real() * m_if_agc_current_gain,
                              samples_in[i].imag() * m_if_agc_current_gain);
  }
  // fprintf(stderr, "m_if_agc_current_gain = %f\n", m_if_agc_current_gain);
}

// Divided by 2 at output
// See adjust_gain() in main.cpp
#define LIMIT_LEVEL (1.7)

// AF AGC.
// Algorithm: function simple_agc_ff() in
// https://github.com/simonyiszk/csdr/blob/master/libcsdr.c
inline void AmDecoder::af_agc(const SampleVector &samples_in,
                              SampleVector &samples_out) {
  double rate = m_af_agc_rate;
  double rate_1minus = 1 - rate;
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    double amplitude = fabs(samples_in[i]);
    double ideal_gain = m_af_agc_reference / amplitude;
    if (ideal_gain > m_af_agc_max_gain) {
      ideal_gain = m_af_agc_max_gain;
    }
    if (ideal_gain <= 0) {
      ideal_gain = 0;
    }
    m_af_agc_current_gain = ((ideal_gain - m_af_agc_current_gain) * rate) +
                            (m_af_agc_current_gain * rate_1minus);
    double output_amplitude = samples_in[i] * m_af_agc_current_gain;
    // hard limiting
    if (output_amplitude > LIMIT_LEVEL) {
      output_amplitude = LIMIT_LEVEL;
      // fprintf(stderr, "af_agc: plus limit level\n");
    } else if (output_amplitude < -(LIMIT_LEVEL)) {
      output_amplitude = -(LIMIT_LEVEL);
      // fprintf(stderr, "af_agc: minus limit level\n");
    }
    samples_out[i] = output_amplitude;
  }
  // fprintf(stderr, "m_af_agc_current_gain = %f\n", m_af_agc_current_gain);
}

// end
