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

#include "IfResampler.h"

// class IfResampler

IfResampler::IfResampler(const double input_rate, const double output_rate)
    : m_irate(input_rate), m_orate(output_rate),
      m_ratio(output_rate / input_rate) {
  // Use float, see typedef of IQSample
  soxr_io_spec_t io_spec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
  // Steep: passband_end = 0.91132832
  soxr_quality_spec_t quality_spec =
      soxr_quality_spec((SOXR_VHQ | SOXR_LINEAR_PHASE), 0);
  soxr_runtime_spec_t runtime_spec = soxr_runtime_spec(1);

  // Create a resampler objects of two interleave channels.
  soxr_error_t error;
  m_soxr = soxr_create(m_irate, m_orate, 2, &error, &io_spec, &quality_spec,
                       &runtime_spec);
  if (error) {
    soxr_delete(m_soxr);
    fprintf(stderr, "IfResampler: unable to create m_soxr: %s\n", error);
    exit(1);
  }
}

void IfResampler::process(const IQSampleVector &samples_in,
                          IQSampleVector &samples_out) {
  size_t input_size = samples_in.size();
  size_t output_size;
  if (m_ratio > 1) {
    output_size = (size_t)lrint((input_size * m_ratio) + 1);
  } else {
    output_size = input_size;
  }
  samples_out.resize(output_size);

  size_t output_length;

  IQSampleCoeff samples_in_interleaved;
  IQSampleCoeff samples_out_interleaved;

  samples_in_interleaved.resize(input_size * 2);
  samples_out_interleaved.resize(output_size * 2);

  // Create real and imaginary part vectors from sample input.
  for (unsigned int i = 0, j = 0; i < input_size; i++, j += 2) {
    IQSample value = samples_in[i];
    samples_in_interleaved[j] = value.real();
    samples_in_interleaved[j + 1] = value.imag();
  }

  // Process the real and imaginary parts.
  if (soxr_error_t error = soxr_process(
          m_soxr, static_cast<soxr_in_t>(samples_in_interleaved.data()),
          input_size, nullptr,
          static_cast<soxr_out_t>(samples_out_interleaved.data()), output_size,
          &output_length);
      error) {
    soxr_delete(m_soxr);
    fprintf(stderr, "IfResampler: soxr_process error of m_soxr: %s\n", error);
    exit(1);
  }

  // Create complex sample output from the real and imaginary part vectors.
  for (unsigned int i = 0, j = 0; i < output_length; i++, j += 2) {
    samples_out[i] =
        IQSample(samples_out_interleaved[j], samples_out_interleaved[j + 1]);
  }

  samples_out.resize(output_length);
}

// end
