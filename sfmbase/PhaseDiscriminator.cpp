// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
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

#include "PhaseDiscriminator.h"
#include "Utility.h"

/* ****************  class PhaseDiscriminator  **************** */

// Construct phase discriminator.
PhaseDiscriminator::PhaseDiscriminator(double max_freq_dev)
    : m_freq_scale_factor(1.0 / (max_freq_dev * 2.0 * M_PI)),
      m_normalize_factor(max_freq_dev * 2.0 * M_PI),
      // boundary = M_PI / m_normalize_factor;
      m_boundary(1.0 / (max_freq_dev * 2.0)) {}

// Process samples.
//
void PhaseDiscriminator::process(const IQSampleVector &samples_in,
                                 SampleVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);

#if 0
  m_phase.resize(n);
  m_detector.resize(n);

  // libvolk parallelism
  volk_32fc_s32f_atan2_32f(m_phase.data(), samples_in.data(),
		  m_normalize_factor, n);
  volk_32f_s32f_32f_fm_detect_32f(m_detector.data(), m_phase.data(),
		  m_boundary, &m_save_value, n);
  volk_32f_convert_64f(samples_out.data(), m_detector.data(), n);
#else
  // sequential calculation code
  IQSample s0 = m_last1_sample;

  for (unsigned int i = 0; i < n; i++) {
    IQSample s1(samples_in[i]);
    IQSample d(conj(s0) * s1);
    // Sample w = atan2(d.imag(), d.real());
    Sample w = Utility::fast_atan2f(d.imag(), d.real());
    samples_out[i] = w * m_freq_scale_factor;
    s0 = s1;
  }

  m_last1_sample = s0;
#endif

}

/* end */
