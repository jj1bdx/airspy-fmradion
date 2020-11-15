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
#include "Utility.h"

// class FineTuner

// Construct finetuner.
FineTuner::FineTuner(unsigned const int table_size, const int freq_shift)
    : m_index(0), m_table(table_size) {
  double phase_step = 2.0 * M_PI / double(table_size);
  for (unsigned int i = 0; i < table_size; i++) {
    double phi = (((int64_t)freq_shift * i) % table_size) * phase_step;
    double pcos = cos(phi);
    double psin = sin(phi);
    m_table[i] = IQSample(pcos, psin);
  }
}

// Process samples.
void FineTuner::process(const IQSampleVector &samples_in,
                        IQSampleVector &samples_out) {
  unsigned int tblidx = m_index;
  unsigned int tblsiz = m_table.size();
  unsigned int n = samples_in.size();

  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    samples_out[i] = samples_in[i] * m_table[tblidx];
    tblidx++;
    if (tblidx == tblsiz)
      tblidx = 0;
  }

  m_index = tblidx;
}

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
      m_cwfilter(FilterParameters::jj1bdx_cw_12khz_500hz, 1)

      // Construct SSB filter (in sample rate 12kHz)
      ,
      m_ssbfilter(FilterParameters::jj1bdx_ssb_12khz_1500hz, 1)

      // Construct HighPassFilterIir
      // cutoff: 60Hz for 12kHz sampling rate
      ,
      m_dcblock(60 / internal_rate_pcm)

      // Construct LowPassFilterRC
      ,
      m_deemph(default_deemphasis * sample_rate_pcm * 1.0e-6)

      // Construct AF AGC
      // Use mostly as peak limiter
      ,
      m_afagc(0.0001, // initial_gain
              1.5,    // max_gain
              // reference
              ((m_mode == ModType::USB) || (m_mode == ModType::LSB)) ? 0.1
              : ((m_mode == ModType::CW) || (m_mode == ModType::WSPR))
                  ? 0.1
                  // default value
                  : 0.2,
              0.002 // rate
              )

      // Construct IF AGC
      // Use as AM level compressor, raise the level to one
      ,
      m_ifagc(1.0,      // initial_gain
              100000.0, // max_gain
              ((m_mode == ModType::USB) || (m_mode == ModType::LSB)) ? 0.25
              : ((m_mode == ModType::CW) || (m_mode == ModType::WSPR))
                  ? 0.25
                  // default value
                  : 0.7,
              0.001 // rate
              )

      // fine tuner for CW pitch shifting (shift up 500Hz)
      // sampling rate: 12kHz
      // table size: 120 = 12000 / 100
      ,
      m_cw_finetuner(low_rate_pcm / 100, 500 / 100)

      // fine tuner for WSPR/SSB pitch shifting (shift up and down 1500Hz)
      // sampling rate: 12kHz
      // table size: 120 = 12000 / 100
      ,
      m_wspr_ssb_up_finetuner(low_rate_pcm / 100, 1500 / 100),
      m_wspr_ssb_down_finetuner(low_rate_pcm / 100, -1500 / 100)

      // CW downsampler and upsampler
      ,
      m_rate_downsampler(internal_rate_pcm, low_rate_pcm),
      m_rate_upsampler(low_rate_pcm, internal_rate_pcm)

{
  // Do nothing
}

void AmDecoder::process(const IQSampleVector &samples_in, SampleVector &audio) {
  switch (m_mode) {
  case ModType::AM:
  case ModType::DSB:
    // Apply narrower filters
    m_amfilter.process(samples_in, m_buf_filtered3);
    break;
  case ModType::USB:
    // Downsanple first
    m_rate_downsampler.process(samples_in, m_buf_filtered1);
    // If no downsampled signal comes out, terminate and wait for next block.
    if (m_buf_filtered1.size() == 0) {
      audio.resize(0);
      return;
    }
    // Shift down 1500Hz first to filter
    m_wspr_ssb_down_finetuner.process(m_buf_filtered1, m_buf_filtered1a);
    // Apply SSB filter
    m_ssbfilter.process(m_buf_filtered1a, m_buf_filtered1b);
    // Shift up 1500Hz to make center frequency to 1500Hz
    m_wspr_ssb_up_finetuner.process(m_buf_filtered1b, m_buf_filtered2);
    // Upsample back
    m_rate_upsampler.process(m_buf_filtered2, m_buf_filtered3);
    // If no upsampled signal comes out, terminate and wait for next block.
    if (m_buf_filtered3.size() == 0) {
      audio.resize(0);
      return;
    }
    break;
  case ModType::LSB:
    // Downsanple first
    m_rate_downsampler.process(samples_in, m_buf_filtered1);
    // If no downsampled signal comes out, terminate and wait for next block.
    if (m_buf_filtered1.size() == 0) {
      audio.resize(0);
      return;
    }
    // Shift up 1500Hz first to filter
    m_wspr_ssb_up_finetuner.process(m_buf_filtered1, m_buf_filtered1a);
    // Apply SSB filter
    m_ssbfilter.process(m_buf_filtered1a, m_buf_filtered1b);
    // Shift down 1500Hz to make center frequency to 1500Hz
    m_wspr_ssb_down_finetuner.process(m_buf_filtered1b, m_buf_filtered2);
    // Upsample back
    m_rate_upsampler.process(m_buf_filtered2, m_buf_filtered3);
    // If no upsampled signal comes out, terminate and wait for next block.
    if (m_buf_filtered3.size() == 0) {
      audio.resize(0);
      return;
    }
    break;
  case ModType::CW:
    // Apply CW filter
    // Downsanple first
    m_rate_downsampler.process(samples_in, m_buf_filtered);
    // If no downsampled signal comes out, terminate and wait for next block.
    if (m_buf_filtered.size() == 0) {
      audio.resize(0);
      return;
    }
    // Apply CW LPF here
    m_cwfilter.process(m_buf_filtered, m_buf_filtered2a);
    // Shift up to an audio frequency (500Hz)
    m_cw_finetuner.process(m_buf_filtered2a, m_buf_filtered2b);
    // Upsample back
    m_rate_upsampler.process(m_buf_filtered2b, m_buf_filtered3);
    // If no upsampled signal comes out, terminate and wait for next block.
    if (m_buf_filtered3.size() == 0) {
      audio.resize(0);
      return;
    }
    break;
  case ModType::WSPR:
    // Apply WSPR filter
    // Downsanple first
    m_rate_downsampler.process(samples_in, m_buf_filtered1);
    // If no downsampled signal comes out, terminate and wait for next block.
    if (m_buf_filtered1.size() == 0) {
      audio.resize(0);
      return;
    }
    // Shift down 1500Hz first to filter
    m_wspr_ssb_down_finetuner.process(m_buf_filtered1, m_buf_filtered1a);
    // Apply CW LPF here
    m_cwfilter.process(m_buf_filtered1a, m_buf_filtered1b);
    // Shift up 1500Hz to make center frequency to 1500Hz
    m_wspr_ssb_up_finetuner.process(m_buf_filtered1b, m_buf_filtered2);
    // Upsample back
    m_rate_upsampler.process(m_buf_filtered2, m_buf_filtered3);
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
  m_if_rms = Utility::rms_level_approx(m_buf_filtered3);

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
    demodulate_am(m_buf_filtered4, m_buf_decoded);
    break;
  case ModType::DSB:
  case ModType::USB:
  case ModType::LSB:
  case ModType::CW:
  case ModType::WSPR:
    demodulate_dsb(m_buf_filtered4, m_buf_decoded);
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
