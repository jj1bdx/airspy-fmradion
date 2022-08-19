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

#include "IfResampler.h"

// class IfResampler

IfResampler::IfResampler(const double input_rate, const double output_rate)
    : m_ratio(output_rate / input_rate),
      m_cdspr_re(
          new r8b::CDSPResampler24(input_rate, output_rate, max_input_length)),
      m_cdspr_im(
          new r8b::CDSPResampler24(input_rate, output_rate, max_input_length)) {
#ifdef DEBUG_IFRESAMPLER
  int latency = m_cdspr_re->getInLenBeforeOutStart();
  fprintf(stderr, "m_ratio = %g, latency = %d\n", m_ratio, latency);
#endif // DEBUG_IFRESAMPLER
  // do nothing
}

void IfResampler::process(const IQSampleVector &samples_in,
                          IQSampleVector &samples_out) {
  size_t input_size = samples_in.size();
  size_t output_size;

  assert(input_size <= max_input_length);

  if (m_ratio > 1) {
    output_size = (size_t)lrint((input_size * m_ratio) + 1);
  } else {
    output_size = input_size;
  }
  samples_out.resize(output_size);

  // Use two independent sample rate converters in sync.
  DoubleVector samples_in_re;
  DoubleVector samples_in_im;
  samples_in_re.resize(input_size);
  samples_in_im.resize(input_size);

  // See lv_cmake() definition for VOLK complex processing.
  volk_32fc_deinterleave_64f_x2(samples_in_re.data(), samples_in_im.data(),
                                samples_in.data(), input_size);

  size_t output_length_re, output_length_im;
  double *output0_re, *output0_im;

  output_length_re =
      m_cdspr_re->process(samples_in_re.data(), input_size, output0_re);
  output_length_im =
      m_cdspr_im->process(samples_in_im.data(), input_size, output0_im);
  assert(output_length_re == output_length_im);

  // Copy CDSPReampler24 internal buffers to given output buffer
  // with type conversion.

  if (output_length_re > 0) {
    for (size_t i = 0; i < output_length_re; i++) {
      samples_out[i] = IQSample(static_cast<float>(output0_re[i]),
                                static_cast<float>(output0_im[i]));
    }
  }
  samples_out.resize(output_length_re);
#ifdef DEBUG_IFRESAMPLER
  fprintf(stderr, "IfResampler: input_size = %zu, output_length_re = %zu\n",
          input_size, output_length_re);
#endif // DEBUG_IFRESAMPLER
}

// end
