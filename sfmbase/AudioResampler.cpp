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

#include "AudioResampler.h"
#include "CDSPResampler.h"

#define MAXINLEN (65536)

// class AudioResampler

AudioResampler::AudioResampler(const double input_rate,
                               const double output_rate)
    : m_ratio(output_rate / input_rate),
      m_cdspr(new r8b::CDSPResampler(input_rate, output_rate, MAXINLEN)) {
#ifdef DEBUG_AUDIORESAMPLER
  int latency = m_cdspr->getInLenBeforeOutStart();
  fprintf(stderr, "m_ratio = %g, latency = %d\n", m_ratio, latency);
#endif // DEBUG_AUDIORESAMPLER
  // do nothing
}

void AudioResampler::process(const SampleVector &samples_in,
                             SampleVector &samples_out) {
  size_t input_size = samples_in.size();
  size_t output_size;
  if (m_ratio > 1) {
    output_size = (size_t)lrint((input_size * m_ratio) + 1);
  } else {
    output_size = input_size;
  }
  samples_out.resize(output_size);

  size_t output_length;

  double *input0 = const_cast<double *>(samples_in.data());
  double *output0;

  output_length = m_cdspr->process(input0, input_size, output0);

  // Copy CDSPReampler internal buffer to given system buffer

  if (output_length > 0) {
    samples_out.assign(output0, output0 + output_length);
  }
  samples_out.resize(output_length);
}

// end
