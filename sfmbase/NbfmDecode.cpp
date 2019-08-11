// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
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

#include "NbfmDecode.h"

// class NbfmDecoder

// Static constants.

const double NbfmDecoder::sample_rate_pcm = 48000;
// Full scale carrier frequency deviation (75 kHz for broadcast FM)
const double NbfmDecoder::freq_dev = 5000;

NbfmDecoder::NbfmDecoder(double sample_rate_demod)
    // Initialize member fields
    : m_sample_rate_fmdemod(sample_rate_demod), m_baseband_mean(0),
      m_baseband_level(0)

      // Construct AudioResampler
      ,
      m_audioresampler_raw(m_sample_rate_fmdemod, sample_rate_pcm)

      // Construct PhaseDiscriminator
      ,
      m_phasedisc(freq_dev / m_sample_rate_fmdemod)

      // Construct LowPassFilterFirAudio
      ,
      m_audiofilter(FilterParameters::jj1bdx_48khz_nbfmaudio)

{
  // Do nothing
}

void NbfmDecoder::process(const IQSampleVector &samples_in,
                          SampleVector &audio) {

  // Demodulate FM to MPX signal.
  m_phasedisc.process(samples_in, m_buf_baseband_raw);

  // Upsample decoded MPX signal to 48kHz.
  m_audioresampler_raw.process(m_buf_baseband_raw, m_buf_baseband);

  // If no downsampled baseband signal comes out,
  // terminate and wait for next block,
  if (m_buf_baseband.size() == 0) {
    audio.resize(0);
    return;
  }

  // Measure baseband level.
  double baseband_mean, baseband_rms;
  samples_mean_rms(m_buf_baseband, baseband_mean, baseband_rms);
  m_baseband_mean = 0.95 * m_baseband_mean + 0.05 * baseband_mean;
  m_baseband_level = 0.95 * m_baseband_level + 0.05 * baseband_rms;

  // Filter out audio high frequency noise.
  m_audiofilter.process(m_buf_baseband, m_buf_baseband_filtered);

  // Just return mono channel.
  audio = std::move(m_buf_baseband_filtered);
}

/* end */
