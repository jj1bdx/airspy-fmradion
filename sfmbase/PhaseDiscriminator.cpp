// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
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

#include "PhaseDiscriminator.h"
#include "Utility.h"

// class PhaseDiscriminator

// Construct phase discriminator.
// frequency scaling factor = 1.0 / (max_freq_dev * 2.0 * M_PI)
PhaseDiscriminator::PhaseDiscriminator(double max_freq_dev)
    : m_normalize_factor(max_freq_dev * 2.0 * M_PI),
      // boundary = M_PI / m_normalize_factor;
      m_boundary(1.0 / (max_freq_dev * 2.0)), m_save_value(0) {}

// Process samples.
void PhaseDiscriminator::process(IQSampleVector &samples_in,
                                 IQSampleDecodedVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);
  m_phase.resize(n);

  // If an input sample value is 0+0j,
  // it will generate NaN after processed by
  // volk_32fc_s32f_atan2_32f(), so
  // the value is set not to generate NaN but zero.
  // Scanning this here has the price to pay,
  // but it's much easier than finding out NaNs.
  for (size_t i = 0; i < n; i++) {
    IQSample v = samples_in[i];
    if ((v.real() == 0.0) && (v.imag() == 0.0)) {
      samples_in[i] = IQSample(1e-10, 0.0);
    }
  }

  // libvolk parallelism
  volk_32fc_s32f_atan2_32f(m_phase.data(), samples_in.data(),
                           m_normalize_factor, n);
  volk_32f_s32f_32f_fm_detect_32f(samples_out.data(), m_phase.data(),
                                  m_boundary, &m_save_value, n);
}

// end
