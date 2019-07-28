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

IfResampler::IfResampler(const double input_rate,
                               const double output_rate)
    : m_irate(input_rate), m_orate(output_rate),
      m_ratio(output_rate / input_rate) {
  soxr_error_t error;
  // Use float, see typedef of IQSample
  soxr_io_spec_t io_spec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
  soxr_quality_spec_t quality_spec =
      soxr_quality_spec(SOXR_HQ, SOXR_LINEAR_PHASE);

  // Create two resampler objects with the same characteristics.

  m_soxr_re =
      soxr_create(m_irate, m_orate, 1, &error, &io_spec, &quality_spec, NULL);
  if (error) {
    soxr_delete(m_soxr_re);
    fprintf(stderr, "IfResampler: unable to create soxr_re: %s\n",
            error);
    exit(1);
  }

  m_soxr_im =
      soxr_create(m_irate, m_orate, 1, &error, &io_spec, &quality_spec, NULL);
  if (error) {
    soxr_delete(m_soxr_im);
    fprintf(stderr, "IfResampler: unable to create soxr_im: %s\n",
            error);
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

  size_t output_length_re;
  size_t output_length_im;
  size_t output_length;
  soxr_error_t error;

  IQSampleCoeff samples_in_re;
  IQSampleCoeff samples_in_im;
  IQSampleCoeff samples_out_re;
  IQSampleCoeff samples_out_im;

  samples_in_re.resize(input_size);
  samples_in_im.resize(input_size);
  samples_out_re.resize(output_size);
  samples_out_im.resize(output_size);

  // Create real and imaginary part vectors from sample input.
  for (int i = 0; i < input_size; i++) {
    IQSample value = samples_in[i];
    samples_in_re[i] = value.real();
    samples_in_im[i] = value.imag();
  }

  // Process the real and imaginary parts.
  error = soxr_process(
      m_soxr_re, static_cast<soxr_in_t>(samples_in_re.data()), input_size, NULL,
      static_cast<soxr_out_t>(samples_out_re.data()), output_size, &output_length_re);
  if (error) {
    soxr_delete(m_soxr_re);
    fprintf(stderr, "IfResampler: soxr_process error of soxr_re: %s\n",
            error);
    exit(1);
  }

  error = soxr_process(
      m_soxr_im, static_cast<soxr_in_t>(samples_in_im.data()), input_size, NULL,
      static_cast<soxr_out_t>(samples_out_im.data()), output_size, &output_length_im);
  if (error) {
    soxr_delete(m_soxr_im);
    fprintf(stderr, "IfResampler: soxr_process error of soxr_im: %s\n",
            error);
    exit(1);
  }

  if (samples_out_re != samples_out_im) {
    soxr_delete(m_soxr_re);
    soxr_delete(m_soxr_im);
    fprintf(stderr, "IfResampler: size values of soxr_process real and imaginary output do not match\n");
    exit(1);
  }

  output_length = output_length_re;

  // Create complex sample output from the real and imaginary part vectors.
  for (int i = 0; i < output_length; i++) {
    IQSample value = IQSample(samples_out_re[i], samples_out_im[i]);
    samples_out[i] = value;
  }

  samples_out.resize(output_length);

}

// end
