// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
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

#include "AudioResampler.h"
#include "CDSPResampler.h"

#include <algorithm>
#include <fmt/format.h>

// class AudioResampler

AudioResampler::AudioResampler(const double input_rate,
                               const double output_rate)
    : m_cdspr(std::make_unique<r8b::CDSPResampler>(input_rate, output_rate,
                                                   max_input_length,
                                                   req_trans_band, req_atten)) {
#ifdef DEBUG_AUDIORESAMPLER
  int latency = m_cdspr->getInLenBeforeOutStart();
  fmt::println(stderr, "AudioResampler latency = {}", latency);
#endif // DEBUG_AUDIORESAMPLER
  // do nothing
}

void AudioResampler::process(const SampleVector &samples_in,
                             SampleVector &samples_out) {
  size_t input_size = samples_in.size();

  assert(input_size <= max_input_length);

  // r8brain CDSPResampler processes double buffers only;
  // convert the float input samples to double first.
  DoubleVector input_double;
  input_double.resize(input_size);
  std::copy_n(samples_in.data(), input_size, input_double.data());

  size_t output_length;
  double *output0;

  output_length = m_cdspr->process(input_double.data(), input_size, output0);

  // Copy CDSPReampler internal buffer to given system buffer
  // with type conversion.

  // Resize first to ensure the output data fits in samples_out
  samples_out.resize(output_length);
  for (size_t i = 0; i < output_length; i++) {
    samples_out[i] = static_cast<Sample>(output0[i]);
  }
#ifdef DEBUG_AUDIORESAMPLER
  fmt::println(stderr, "AudioResampler: input_size = {}, output_length = {}",
               input_size, output_length);
#endif // DEBUG_AUDIORESAMPLER
}

// end
