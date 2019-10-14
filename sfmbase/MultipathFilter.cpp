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

#include <volk/volk.h>

#include "MultipathFilter.h"

// Class MultipathFilter
// Complex adaptive filter for reducing FM multipath.

MultipathFilter::MultipathFilter(unsigned int stages, double reference_level)
    : m_stages(stages), m_index_reference_point((m_stages * 3) + 1),
      m_filter_order((m_stages * 4) + 1), m_coeff(m_filter_order),
      m_state(m_filter_order), m_reference_level(reference_level) {
  assert(stages > 0);
  for (unsigned int i = 0; i < m_filter_order; i++) {
    m_state[i] = IQSample(0, 0);
  }
  initialize_coefficients();
}

void MultipathFilter::initialize_coefficients() {
  for (unsigned int i = 0; i < m_index_reference_point; i++) {
    m_coeff[i] = MfCoeff(0, 0);
  }
  // Set the initial coefficient
  // of the unity gain at the middle of the coefficient sequence
  // where [0 ... m_filter_order] stands for
  // [-(stages * 3) ... 0 ... +stages]
  m_coeff[m_index_reference_point] = MfCoeff(1, 0);
  for (unsigned int i = m_index_reference_point + 1; i < m_filter_order; i++) {
    m_coeff[i] = MfCoeff(0, 0);
  }
}

// Apply a simple FIR filter for each input.
inline IQSample MultipathFilter::single_process(const IQSample filter_input) {
  // Remove the element number zero (oldest one)
  // and add the input as the newest element at the end of the buffer
  m_state.emplace_back(filter_input);
  m_state.erase(m_state.begin());
  IQSample output = IQSample(0, 0);
#if 0
  for (unsigned int i = 0; i < m_filter_order; i++) {
    output += m_state[i] * m_coeff[i];
  }
#else
  volk_32fc_x2_dot_prod_32fc(&output, m_state.data(), m_coeff.data(), m_filter_order);
#endif
  return output;
}

// Update coefficients by complex LMS/CMA method.
inline void MultipathFilter::update_coeff(const IQSample result) {
  // TODO: reevaluate alpha value (might be able to set larger)
  //       according to NLMS algorithm
  // Alpha originally set to 0.002, increased to compensate
  // sparse recalculation (experimental)
  const double alpha = 0.004 / m_filter_order;
  // Input instant envelope
  const double env = std::norm(result);
  // error = [desired signal] - [filter output]
  const double error = m_reference_level - env;
  const double factor = alpha * error;
  const MfCoeff factor_times_result = MfCoeff(factor * result.real(),
		 factor * result.imag());

  // Recalculate all coefficients
  for (unsigned int i = 0; i < m_filter_order; i++) {
    m_coeff[i] += factor_times_result * std::conj(m_state[i]);
  }
  // Set the imaginary part of the middle (position 0) coefficient to zero
  m_coeff[m_index_reference_point] =
      MfCoeff(m_coeff[m_index_reference_point].real(), 0);
  // Set the latest error value for monitoring
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

  // Note: this is bitmask
  // Run the update every four (4) samples to reduce CPU load
  // This still maintain 384000/4 = 96000 times/sec update rate
  const unsigned int filter_interval = 0x03;
  unsigned int i = 0;
  for (; i < n; i++) {
    IQSample output = single_process(samples_in[i]);
    samples_out[i] = output;
    if ((i & filter_interval) == 0) {
      // Update filter coefficients here
      update_coeff(output);
    }
#if 0 // enable this for per-sample coefficient monitor
    //
    fprintf(stderr, "sample,%u,m_error,%.9f,m_coeff,", i, m_error);
    for (unsigned int i = 0; i < m_coeff.size(); i++) {
      MfCoeff val = m_coeff[i];
      fprintf(stderr, "%d,%.9f,%.9f,", i, val.real(),
                  val.imag());
      }
    fprintf(stderr, "\n");
#endif
  }
  assert(i == samples_out.size());
}

// end
