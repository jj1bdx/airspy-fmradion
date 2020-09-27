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

AmDecoder::AmDecoder(double sample_rate_demod, IQSampleCoeff &amfilter_coeff,
                     const ModType mode)
    // Initialize member fields
    : m_sample_rate_demod(sample_rate_demod), m_amfilter_coeff(amfilter_coeff),
      m_mode(mode), m_baseband_mean(0), m_baseband_level(0), m_if_rms(0.0)

      // Construct AudioResampler
      ,
      m_audioresampler(internal_rate_pcm, sample_rate_pcm)

      // Construct AM narrow filter
      ,
      m_amfilter(m_amfilter_coeff, 1)

      // Construct CW narrow filter
      ,
      m_cwfilter(FilterParameters::jj1bdx_cw_48khz_800hz, 1)

      // Construct SSB narrow filter
      ,
      m_ssbfilter(FilterParameters::jj1bdx_am_48khz_narrow, 1)

      // IF down/upshifter
      ,
      m_upshifter(true), m_downshifter(false)

      // SSB shifted-audio filter from 12 to 24kHz
      ,
      m_ssbshiftfilter(FilterParameters::jj1bdx_ssb_48khz_12to24khz, 1)

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
              ((m_mode == ModType::USB) || (m_mode == ModType::LSB))
                  ? 0.1
                  : (m_mode == ModType::CW) ? 0.1
                                            // default value
                                            : 0.2,
              0.002 // rate
              )

      // Construct IF AGC
      // Use as AM level compressor, raise the level to one
      ,
      m_ifagc(1.0,      // initial_gain
              100000.0, // max_gain
              ((m_mode == ModType::USB) || (m_mode == ModType::LSB))
                  ? 0.25
                  : (m_mode == ModType::CW) ? 0.25
                                            // default value
                                            : 0.7,
              0.001 // rate
              )

      // fine tuner for pitch shifting (shift up 500Hz)
      ,
      m_finetuner(internal_rate_pcm / 100, 500 / 100) {
  // Do nothing
}

void AmDecoder::process(const IQSampleVector &samples_in, SampleVector &audio) {
  switch (m_mode) {
  case ModType::AM:
    // Apply narrower filters
    m_amfilter.process(samples_in, m_buf_filtered3);
    break;
  case ModType::USB:
    // Apply SSB filters
    m_ssbfilter.process(samples_in, m_buf_filtered);
    // Shift Fs/2 and eliminate the lower sideband
    m_upshifter.process(m_buf_filtered, m_buf_filtered2a);
    m_ssbshiftfilter.process(m_buf_filtered2a, m_buf_filtered2b);
    m_downshifter.process(m_buf_filtered2b, m_buf_filtered3);
    break;
  case ModType::LSB:
    // Apply SSB filters
    m_ssbfilter.process(samples_in, m_buf_filtered);
    // Shift Fs/2 and eliminate the upper sideband
    m_downshifter.process(m_buf_filtered, m_buf_filtered2a);
    m_ssbshiftfilter.process(m_buf_filtered2a, m_buf_filtered2b);
    m_upshifter.process(m_buf_filtered2b, m_buf_filtered3);
    break;
  case ModType::CW:
    // Apply CW filter
    m_cwfilter.process(samples_in, m_buf_filtered);
    m_finetuner.process(m_buf_filtered, m_buf_filtered3);
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
