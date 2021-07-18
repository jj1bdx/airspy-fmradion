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

#include "AudioResampler.h"

// class AudioResampler

AudioResampler::AudioResampler(const double input_rate,
                               const double output_rate)
    : m_irate(input_rate), m_orate(output_rate),
      m_ratio(output_rate / input_rate) {
  // Use double
  soxr_io_spec_t io_spec = soxr_io_spec(SOXR_FLOAT64_I, SOXR_FLOAT64_I);
  // Not steep: passband_end = 0.91132832
  soxr_quality_spec_t quality_spec =
      soxr_quality_spec((SOXR_VHQ | SOXR_LINEAR_PHASE), 0);
  soxr_runtime_spec_t runtime_spec = soxr_runtime_spec(1);

  soxr_error_t error;
  m_soxr = soxr_create(m_irate, m_orate, 1, &error, &io_spec, &quality_spec,
                       &runtime_spec);
  if (error) {
    soxr_delete(m_soxr);
    fprintf(stderr, "AudioResampler: unable to create soxr: %s\n", error);
    exit(1);
  }
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
  if (soxr_error_t error = soxr_process(
          m_soxr, static_cast<soxr_in_t>(samples_in.data()), input_size,
          nullptr, static_cast<soxr_out_t>(samples_out.data()), output_size,
          &output_length);
      error) {
    soxr_delete(m_soxr);
    fprintf(stderr, "AudioResampler: soxr_process error: %s\n", error);
    exit(1);
  }

  samples_out.resize(output_length);
}

// end
