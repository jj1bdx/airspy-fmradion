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

#include <cassert>
#include <cmath>

#include "AmDecode.h"

// class AmDecoder

// Static constants.

const double AmDecoder::sample_rate_pcm = 48000;
const double AmDecoder::internal_rate_pcm = 12000;
const double AmDecoder::cw_rate_pcm = 2000;
// Half bandwidth of audio signal in Hz (4.5kHz for AM)
const double AmDecoder::bandwidth_pcm = 4500;
// Deemphasis constant in microseconds.
const double AmDecoder::default_deemphasis = 100;

AmDecoder::AmDecoder(double sample_rate_demod, IQSampleCoeff &amfilter_coeff,
                     const ModType mode)
    // Initialize member fields
    : m_sample_rate_demod(sample_rate_demod), m_amfilter_coeff(amfilter_coeff),
      m_mode(mode), m_baseband_mean(0), m_baseband_level(0), m_if_rms(0.0)

      // Construct AudioResampler
      ,
      m_audioresampler(internal_rate_pcm, sample_rate_pcm)

      // Construct IfResampler to first convert to internal PCM rate
      ,
      m_ifresampler(m_sample_rate_demod, internal_rate_pcm)

      // Construct AM narrow filter
      ,
      m_amfilter(m_amfilter_coeff, 1)

      // IF down/upshifter
      ,
      m_upshifter(true), m_downshifter(false)

      // SSB shifted-audio filter from 3 to 6kHz
      ,
      m_ssbshiftfilter(FilterParameters::jj1bdx_ssb_3to6khz, 1)

      // Construct IfResampler to first convert to internal PCM rate
      ,
      m_cw_downsampler(internal_rate_pcm, cw_rate_pcm),
      m_cw_upsampler(cw_rate_pcm, internal_rate_pcm)

      // IF upshifter for CW
      ,
      m_upshifter_cw(true)

      // CW baseband filter for +- 250Hz
      ,
      m_cwshiftfilter(FilterParameters::jj1bdx_cw_250hz, 1)

      // Construct HighPassFilterIir
      // cutoff: 60Hz for 12kHz sampling rate
      ,
      m_dcblock(0.005)

      // Construct LowPassFilterRC
      ,
      m_deemph(default_deemphasis * sample_rate_pcm * 1.0e-6)

      // Construct AF AGC
      ,
      m_afagc(1.0, // initial_gain
              4.0, // max_gain
              // reference
              ((m_mode == ModType::USB) || (m_mode == ModType::LSB))
                  ? 0.125
                  : (m_mode == ModType::CW) ? 0.1
                                            // default value
                                            : 0.25,
              0.002 // rate
              )

      // Construct IF AGC
      ,
      m_ifagc(1.0,      // initial_gain
              100000.0, // max_gain
              ((m_mode == ModType::USB) || (m_mode == ModType::LSB))
                  ? 0.125
                  : (m_mode == ModType::CW) ? 0.1
                                            // default value
                                            : 0.35,
              0.005 // rate
      ) {
  // Do nothing
}

void AmDecoder::process(const IQSampleVector &samples_in, SampleVector &audio) {

  // Apply narrower filters
  m_amfilter.process(samples_in, m_buf_filtered);

  // Shift Fs/4 and filter Fs/4~Fs/2 frequency bandwidth only
  switch (m_mode) {
  case ModType::USB:
    m_upshifter.process(m_buf_filtered, m_buf_filtered2a);
    m_ssbshiftfilter.process(m_buf_filtered2a, m_buf_filtered2b);
    m_downshifter.process(m_buf_filtered2b, m_buf_filtered3);
    break;
  case ModType::LSB:
    m_downshifter.process(m_buf_filtered, m_buf_filtered2a);
    m_ssbshiftfilter.process(m_buf_filtered2a, m_buf_filtered2b);
    m_upshifter.process(m_buf_filtered2b, m_buf_filtered3);
    break;
  case ModType::CW:
    m_cw_downsampler.process(m_buf_filtered, m_buf_filtered2a);
    // If no downsampled signal comes out, terminate and wait for next block.
    if (m_buf_filtered2a.size() == 0) {
      audio.resize(0);
      return;
    }
    m_cwshiftfilter.process(m_buf_filtered2a, m_buf_filtered2b);
    // CW pitch: 500Hz
    m_upshifter_cw.process(m_buf_filtered2b, m_buf_filtered2c);
    m_cw_upsampler.process(m_buf_filtered2c, m_buf_filtered3);
    // If no upsampled signal comes out, terminate and wait for next block.
    if (m_buf_filtered3.size() == 0) {
      audio.resize(0);
      return;
    }
    break;
  default:
    m_buf_filtered3 = std::move(m_buf_filtered);
    break;
  }

  // Measure IF RMS level.
  m_if_rms = rms_level_approx(m_buf_filtered3);

  // If AGC
  m_ifagc.process(m_buf_filtered3, m_buf_filtered4);

  // Demodulate AM/DSB signal.
  switch (m_mode) {
  case ModType::FM:
    // Force error
    assert(m_mode != ModType::FM);
    break;
  case ModType::NBFM:
    // Force error
    assert(m_mode != ModType::NBFM);
    break;
  case ModType::AM:
    demodulate_am(m_buf_filtered4, m_buf_baseband_demod);
    break;
  case ModType::DSB:
  case ModType::USB:
  case ModType::LSB:
  case ModType::CW:
    demodulate_dsb(m_buf_filtered4, m_buf_baseband_demod);
    break;
  }

  // DC blocking.
  m_dcblock.process_inplace(m_buf_baseband_demod);

  // Upsample baseband signal.
  m_audioresampler.process(m_buf_baseband_demod, m_buf_baseband_preagc);

  // If no baseband audio signal comes out, terminate and wait for next block,
  if (m_buf_baseband_preagc.size() == 0) {
    audio.resize(0);
    return;
  }

  // Audio AGC
  m_afagc.process(m_buf_baseband_preagc, m_buf_baseband);

  // Measure baseband level after DC blocking.
  double baseband_mean, baseband_rms;
  samples_mean_rms(m_buf_baseband, baseband_mean, baseband_rms);
  m_baseband_mean = 0.95 * m_baseband_mean + 0.05 * baseband_mean;
  m_baseband_level = 0.95 * m_baseband_level + 0.05 * baseband_rms;

  // Deemphasis
  m_deemph.process_inplace(m_buf_baseband);

  // Return mono channel.
  audio = std::move(m_buf_baseband);
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

// end
