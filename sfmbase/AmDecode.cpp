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

#include "AmDecode.h"
#include "Utility.h"

// class AmDecoder

AmDecoder::AmDecoder(IQSampleCoeff &amfilter_coeff, const ModType mode)
    // Initialize member fields
    : m_amfilter_coeff(amfilter_coeff), m_mode(mode), m_baseband_mean(0),
      m_baseband_level(0), m_if_rms(0.0)

      // Construct AM narrow filter
      ,
      m_amfilter(m_amfilter_coeff, 1)

      // Construct CW narrow filter (in sample rate 12kHz)
      ,
      m_cwfilter(FilterParameters::jj1bdx_cw_48khz_500hz, 1)

      // Construct SSB filter (in sample rate 12kHz)
      ,
      m_ssbfilter(FilterParameters::jj1bdx_ssb_48khz_1500hz, 1)

      // Construct HighPassFilterIir
      // cutoff: 60Hz for 12kHz sampling rate
      ,
      m_dcblock(60 / internal_rate_pcm)

      // Construct LowPassFilterRC
      ,
      m_deemph(deemphasis_time * sample_rate_pcm * 1.0e-6)

      // Construct AF AGC
      // Use mostly as peak limiter
      ,
      m_afagc(1.0, // initial_gain
              1.5, // max_gain
              // reference
              ((m_mode == ModType::USB) || (m_mode == ModType::LSB) ||
               (m_mode == ModType::CW) || (m_mode == ModType::WSPR))
                  ? 0.24
                  // default value
                  : 0.6,
              // rate
              ((m_mode == ModType::CW) || (m_mode == ModType::WSPR))
                  ? 0.00125
                  // default value
                  : 0.001)

      // Construct IF AGC
      // Use as AM level compressor, raise the level to one
      ,
      m_ifagc(1.0,       // initial_gain
              1000000.0, // max_gain
              // rate
              ((m_mode == ModType::CW) || (m_mode == ModType::WSPR))
                  ? 0.0006
                  // default value
                  : 0.0003)

      // fine tuner for CW pitch shifting (shift up 500Hz)
      // sampling rate: 48kHz
      // table size: 480 = 48000 / 100
      ,
      m_cw_finetuner(internal_rate_pcm / 100, 500 / 100)

      // fine tuner for WSPR/SSB pitch shifting (shift up and down 1500Hz)
      // sampling rate: 48kHz
      // table size: 480 = 48000 / 100
      ,
      m_wspr_ssb_up_finetuner(internal_rate_pcm / 100, 1500 / 100),
      m_wspr_ssb_down_finetuner(internal_rate_pcm / 100, -1500 / 100)

{
  // Do nothing
}

void AmDecoder::process(const IQSampleVector &samples_in, SampleVector &audio) {
  switch (m_mode) {
  case ModType::AM:
  case ModType::DSB:
    // Apply narrower filters
    m_amfilter.process(samples_in, m_buf_filtered2);
    break;
  case ModType::USB:
  case ModType::LSB:
  case ModType::CW:
  case ModType::WSPR:
    switch (m_mode) {
    case ModType::USB:
      // Shift down 1500Hz first to filter
      m_wspr_ssb_down_finetuner.process(samples_in, m_buf_filtered1a);
      // Apply SSB filter
      m_ssbfilter.process(m_buf_filtered1a, m_buf_filtered1b);
      // Shift up 1500Hz to make center frequency to 1500Hz
      m_wspr_ssb_up_finetuner.process(m_buf_filtered1b, m_buf_filtered2);
      break;
    case ModType::LSB:
      // Shift up 1500Hz first to filter
      m_wspr_ssb_up_finetuner.process(samples_in, m_buf_filtered1a);
      // Apply SSB filter
      m_ssbfilter.process(m_buf_filtered1a, m_buf_filtered1b);
      // Shift down 1500Hz to make center frequency to 1500Hz
      m_wspr_ssb_down_finetuner.process(m_buf_filtered1b, m_buf_filtered2);
      break;
    case ModType::CW:
      // Apply CW LPF here
      m_cwfilter.process(samples_in, m_buf_filtered1a);
      // Shift up to an audio frequency (500Hz)
      m_cw_finetuner.process(m_buf_filtered1a, m_buf_filtered2);
      break;
    case ModType::WSPR:
      // Shift down 1500Hz first to filter
      m_wspr_ssb_down_finetuner.process(samples_in, m_buf_filtered1a);
      // Apply CW LPF here
      m_cwfilter.process(m_buf_filtered1a, m_buf_filtered1b);
      // Shift up 1500Hz to make center frequency to 1500Hz
      m_wspr_ssb_up_finetuner.process(m_buf_filtered1b, m_buf_filtered2);
      break;
    default:
      m_buf_filtered2 = std::move(samples_in);
      break;
    }
    // If no upsampled signal comes out, terminate and wait for next block.
    if (m_buf_filtered2.size() == 0) {
      audio.resize(0);
      return;
    }
    break;
  default:
    m_buf_filtered2 = std::move(samples_in);
    break;
  }

  // Measure IF RMS level.
  m_if_rms = Utility::rms_level_sample(m_buf_filtered2);

  // If AGC
  m_ifagc.process(m_buf_filtered2, m_buf_filtered3);

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
    demodulate_am(m_buf_filtered3, m_buf_decoded);
    break;
  case ModType::DSB:
  case ModType::USB:
  case ModType::LSB:
  case ModType::CW:
  case ModType::WSPR:
    demodulate_dsb(m_buf_filtered3, m_buf_decoded);
    break;
  }

  // If no decoded signal comes out, terminate and wait for next block,
  size_t decoded_size = m_buf_decoded.size();
  if (decoded_size == 0) {
    audio.resize(0);
    return;
  }

  // Convert decoded data to baseband data
  m_buf_baseband_demod.resize(decoded_size);
  volk_32f_convert_64f(m_buf_baseband_demod.data(), m_buf_decoded.data(),
                       decoded_size);

  // DC blocking.
  m_dcblock.process_inplace(m_buf_baseband_demod);

  // If no baseband audio signal comes out, terminate and wait for next block,
  if (m_buf_baseband_demod.size() == 0) {
    audio.resize(0);
    return;
  }

  // Audio AGC
  m_afagc.process(m_buf_baseband_demod, m_buf_baseband);

  // Measure baseband level after DC blocking.
  float baseband_mean, baseband_rms;
  Utility::samples_mean_rms(m_buf_decoded, baseband_mean, baseband_rms);
  m_baseband_mean = 0.95 * m_baseband_mean + 0.05 * baseband_mean;
  m_baseband_level = 0.95 * m_baseband_level + 0.05 * baseband_rms;

  // Deemphasis
  m_deemph.process_inplace(m_buf_baseband);

  // Return mono channel.
  audio = std::move(m_buf_baseband);
}

// Demodulate AM signal.
inline void AmDecoder::demodulate_am(const IQSampleVector &samples_in,
                                     IQSampleDecodedVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);
  volk_32fc_magnitude_32f(samples_out.data(), samples_in.data(), n);
}

// Demodulate DSB signal.
inline void AmDecoder::demodulate_dsb(const IQSampleVector &samples_in,
                                      IQSampleDecodedVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);
  volk_32fc_deinterleave_real_32f(samples_out.data(), samples_in.data(), n);
}

// end
