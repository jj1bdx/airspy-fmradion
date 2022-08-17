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

#define CHANNELS (2)

// class IfResampler

// This code uses libsamplerate aka Secret Rabbit Code as the resampler.
// See http://libsndfile.github.io/libsamplerate/ for the documentation.

IfResampler::IfResampler(const double input_rate, const double output_rate)
    : m_ratio(output_rate / input_rate) {
  int error;
  m_src = src_new(SRC_SINC_MEDIUM_QUALITY, CHANNELS, &error);
  if (error) {
    src_delete(m_src);
    fprintf(stderr, "IfResampler: unable to create m_src: %s\n",
            src_strerror(error));
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

  IQSampleCoeff samples_in_interleaved;
  IQSampleCoeff samples_out_interleaved;
  samples_in_interleaved.resize(input_size * CHANNELS);
  samples_out_interleaved.resize(output_size * CHANNELS);

  // Create real and imaginary part vectors from sample input.
  for (unsigned int i = 0, j = 0; i < input_size; i++, j += CHANNELS) {
    IQSample value = samples_in[i];
    samples_in_interleaved[j] = value.real();
    samples_in_interleaved[j + 1] = value.imag();
  }

  // Process the real and imaginary parts.
  SRC_DATA src_data;
  long output_frame_length = 0;

  src_data.data_in = samples_in_interleaved.data();
  src_data.data_out = samples_out_interleaved.data();
  // Frame sizes here!
  src_data.input_frames = input_size;
  src_data.output_frames = output_size;
  src_data.src_ratio = m_ratio;
  src_data.end_of_input = 0;

  // Repeat processing until all input frames are processed
  while (src_data.input_frames > 0) {
    if (int error = src_process(m_src, &src_data)) {
      src_delete(m_src);
      fprintf(stderr, "IfResampler: unable to process m_src: %s\n",
              src_strerror(error));
      exit(1);
    }

    // Recalculate pointers as the input data are used
    // and the output data are added.

    // Here samples (= frames * CHANNELS)
    src_data.data_out += src_data.output_frames_gen * CHANNELS;
    // Here frames
    output_frame_length += src_data.output_frames_gen;

    // Here samples (= frames * CHANNELS)
    src_data.data_in += src_data.input_frames_used * CHANNELS;
    // Here frames
    src_data.input_frames -= src_data.input_frames_used;
  }

  // Create complex sample output from the real and imaginary part vectors.
  for (unsigned int i = 0, j = 0; i < output_frame_length; i++, j += 2) {
    samples_out[i] =
        IQSample(samples_out_interleaved[j], samples_out_interleaved[j + 1]);
  }

  samples_out.resize(output_frame_length);
}

// end
