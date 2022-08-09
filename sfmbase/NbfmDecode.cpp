// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2019-2022 Kenji Rikitake, JJ1BDX
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

#include "NbfmDecode.h"
#include "Utility.h"

// class NbfmDecoder

NbfmDecoder::NbfmDecoder(IQSampleCoeff &nbfmfilter_coeff, const double freq_dev)
    // Initialize member fields
    : m_nbfmfilter_coeff(nbfmfilter_coeff), m_freq_dev(freq_dev),
      m_baseband_mean(0), m_baseband_level(0), m_if_rms(0.0)

      // Construct NBFM narrow filter
      ,
      m_nbfmfilter(m_nbfmfilter_coeff, 1)

      // Construct PhaseDiscriminator
      ,
      m_phasedisc(m_freq_dev / internal_rate_pcm)

      // Construct LowPassFilterFirAudio
      ,
      m_audiofilter(FilterParameters::jj1bdx_48khz_nbfmaudio)

      // Construct IF AGC
      // Reference level: 1.0
      ,
      m_ifagc(1.0, 100000.0, 1.0, 0.0001) {
  // Do nothing
}

void NbfmDecoder::process(const IQSampleVector &samples_in,
                          SampleVector &audio) {

  // Apply IF filter.
  m_nbfmfilter.process(samples_in, m_buf_filtered);

  // Measure IF RMS level.
  m_if_rms = Utility::rms_level_sample(m_buf_filtered);

  // Perform IF AGC.
  m_ifagc.process(m_buf_filtered, m_samples_in_after_agc);

  // Demodulate FM to audio signal.
  m_phasedisc.process(m_samples_in_after_agc, m_buf_decoded);
  size_t decoded_size = m_buf_decoded.size();
  // If no downsampled decoded signal comes out,
  // terminate and wait for next block,
  if (decoded_size == 0) {
    audio.resize(0);
    return;
  }
  // Convert decoded data to baseband data
  m_buf_baseband.resize(decoded_size);
  volk_32f_convert_64f(m_buf_baseband.data(), m_buf_decoded.data(),
                       decoded_size);

  // If no downsampled baseband signal comes out,
  // terminate and wait for next block,
  if (m_buf_baseband.size() == 0) {
    audio.resize(0);
    return;
  }

  // Measure baseband level.
  float baseband_mean, baseband_rms;
  Utility::samples_mean_rms(m_buf_decoded, baseband_mean, baseband_rms);
  m_baseband_mean = 0.95 * m_baseband_mean + 0.05 * baseband_mean;
  m_baseband_level = 0.95 * m_baseband_level + 0.05 * baseband_rms;

  // Filter out audio high frequency noise.
  m_audiofilter.process(m_buf_baseband, m_buf_baseband_filtered);

  // Adjust gain by -3dB (0.707)
  const double audio_gain = std::pow(10.0, (-3.0 / 20.0));
  Utility::adjust_gain(m_buf_baseband_filtered, audio_gain);

  // Just return mono channel.
  audio = std::move(m_buf_baseband_filtered);
}

//
