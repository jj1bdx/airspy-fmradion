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
#include <complex>

#include "MultipathFilter.h"

// Multipath adaptive filter construction method reference in Japanese:
// Takashi Mochizuki, and Mitsutoshi Hatori, "Automatic Cancelling of FM
// Multipath Distortion Using and Adaptive Digital Filter", The Journal of the
// Institute of Television Engineers of Japan, Vol. 39, No. 3, pp. 228-234
// (1985). https://doi.org/10.3169/itej1978.39.228

// Class MultipathFilter
// Complex adaptive filter for reducing FM multipath.

MultipathFilter::MultipathFilter(unsigned int stages, double reference_level)
    : m_stages(stages), m_filter_order((m_stages * 2) + 1),
      m_coeff(m_filter_order), m_state(m_filter_order),
      m_reference_level(reference_level) {
  assert(stages > 0);
  for (unsigned int i = 0; i < m_filter_order; i++) {
    m_coeff[i] = MfCoeff(0, 0);
    m_state[i] = IQSample(0, 0);
  }
  m_coeff[m_stages] = MfCoeff(1, 0);
}

// Apply a simple FIR filter for each input.
inline IQSample MultipathFilter::single_process(const IQSample filter_input) {
  m_state.push_back(filter_input);
  IQSample output = IQSample(0, 0);
  for (unsigned int i = 0; i < m_filter_order; i++) {
    output += m_state[i] * m_coeff[i];
  }
  return output;
}

// Update coefficients by complex LMS/CMA method.
inline void MultipathFilter::update_coeff(const IQSample result) {
  // This value should be kept the same
  const double alpha = 0.00002;
  // Input instant envelope
  const double env = std::norm(result);
  // error = [desired signal] - [filter output]
  const double error = m_reference_level - env;
  const MfCoeff factor = MfCoeff(alpha * error, 0);

  for (unsigned int i = 0; i < m_filter_order; i++) {
    m_coeff[i] += factor * result * std::conj(m_state[i]);
  }
  m_coeff[m_stages] = MfCoeff(m_coeff[m_stages].real(), 0);
}

// Process block samples.
void MultipathFilter::process(const IQSampleVector &samples_in,
                              IQSampleVector &samples_out) {
  unsigned int n = samples_in.size();
  if (n == 0) {
    return;
  }
  samples_out.resize(n);

  unsigned int i = 0;
  for (; i < n; i++) {
    IQSample output = single_process(samples_in[i]);
    samples_out[i] = output;
    // Update filter coefficients here
    update_coeff(output);
  }
  assert(i == samples_out.size());
}

// end
