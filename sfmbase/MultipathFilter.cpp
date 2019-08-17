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

// Multipath adaptive filter construction method reference in English:
// [1] J. Treichler and B. Agee, "A new approach to multipath correction of
// constant modulus signals," in IEEE Transactions on Acoustics, Speech, and
// Signal Processing, vol. 31, no. 2, pp. 459-472, April 1983.
// doi: 10.1109/TASSP.1983.1164062

// Multipath adaptive filter construction method reference in Japanese:
// [2] Takashi Mochizuki, and Mitsutoshi Hatori, "Automatic Cancelling of FM
// Multipath Distortion Using and Adaptive Digital Filter", The Journal of the
// Institute of Television Engineers of Japan, Vol. 39, No. 3, pp. 228-234
// (1985). https://doi.org/10.3169/itej1978.39.228

#include <cassert>
#include <cmath>
#include <complex>

#include "MultipathFilter.h"

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
  // Set the initial coefficient
  // of the unity gain at the middle of the coefficient sequence
  // where [0 ... m_filter_order] stands for [-stages ... 0 ... +stages]
  m_coeff[m_stages] = MfCoeff(1, 0);
}

// Apply a simple FIR filter for each input.
inline IQSample MultipathFilter::single_process(const IQSample filter_input) {
  // Remove the element number zero (oldest one)
  // and add the input as the newest element at the end of the buffer
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
  // TODO: reevaluate this value (might be able to set larger)
  //       according to NLMS algorithm

  // Assume estimated norm of n-dimension coefficient vector: 2 * n
  // (where n = m_filter_order)
  // (the absolute values of the real/imaginary part of each coefficient
  //  would not exceed 1.0)
  // then the update factor alpha should be proportional to 1/(2 * n),
  // and alpha must not exceed 1/(2 * n), which is actually too large.
  // In paper [2], the old constant alpha = 0.00002 was safe
  // when n = 401 (200 stages) when the IF S/N was 40dB.

  const double alpha = 0.008 / m_filter_order;
  // Input instant envelope
  const double env = std::norm(result);
  // error = [desired signal] - [filter output]
  const double error = m_reference_level - env;
  const MfCoeff factor = MfCoeff(alpha * error, 0);
  // Recalculate all coefficients
  for (unsigned int i = 0; i < m_filter_order; i++) {
    m_coeff[i] += factor * result * std::conj(m_state[i]);
  }
  // Set the imaginary part of the middle (position 0) coefficient to zero
  m_coeff[m_stages] = MfCoeff(m_coeff[m_stages].real(), 0);
  m_error = error;
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
