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

#include "MultipathFilter.h"

// Class MultipathFilter
// Complex adaptive filter for reducing FM multipath.

MultipathFilter::MultipathFilter(unsigned int stages)
    : // Filter stages.
      m_stages(stages)

      // Filter reference point position in the vector.
      ,
      m_index_reference_point((m_stages * 3) + 1)

      // Filter order, equals to the size of the coefficient vector.
      ,
      m_filter_order((m_stages * 4) + 1)

      // mu = alpha / (sum of input signal state square norm).
      // When the reference level = 1.0,
      // estimated norm of the input state vector ~= m_filter_order
      // * norm(input_signal) ~= 1.0 since abs(input_signal) ~= 1.0
      // * measurement suggests that the error of mu to
      //   the actually measureed mu is nominally +-10% or less
      // Note: reference level is 1.0, therefore excluded from this expression.
      ,
      m_mu(alpha / m_filter_order)

      // Initialize coefficient and state vectors with the size.
      ,
      m_coeff(m_filter_order), m_state(m_filter_order) {

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
  // VOLK calculation, equivalent to:
  // for (unsigned int i = 0; i < m_filter_order; i++) {
  //   output += m_state[i] * m_coeff[i];
  // }
  volk_32fc_x2_dot_prod_32fc(&output, m_state.data(), m_coeff.data(),
                             m_filter_order);
  return output;
}

// Update coefficients by complex LMS/CMA method.
inline void MultipathFilter::update_coeff(const IQSample result) {

  volk::vector<float> state_mag_sq;
  state_mag_sq.resize(m_filter_order);
  float state_mag_sq_sum;

  // Input instant envelope
  const double env = std::norm(result);
  // error = [desired signal] - [filter output]
  const double error = if_target_level - env;

  // Normalized LMS (NLMS) processing
  // First calculate the square norm of input data (m_state) by
  // * Compute the square magnitude of each element of m_state
  // * Then add the square magnitude for all the elements
  volk_32fc_magnitude_squared_32f(state_mag_sq.data(), m_state.data(),
                                  m_filter_order);
  volk_32f_accumulator_s32f(&state_mag_sq_sum, state_mag_sq.data(),
                            m_filter_order);
  // fprintf(stderr, "state_mag_sq_sum = %.9g\n", state_mag_sq_sum);

  // Obtain the step size (dymanically computed)
  m_mu = alpha / state_mag_sq_sum;

  // Calculate correlation vector
  const float factor = error * m_mu;
  const MfCoeff factor_times_result =
      MfCoeff(factor * result.real(), factor * result.imag());
  // Recalculate all coefficients
  // VOLK calculation, equivalent to:
  // for (unsigned int i = 0; i < m_filter_order; i++) {
  //  m_coeff[i] += factor_times_result * std::conj(m_state[i]);
  // }
  // Note: always check if the result and the source vectors can overlap!
  // For volk_32fc_x2_s32fc_multiply_conjugate_add_32fc(),
  // the overlapping issue seems to be OK.
  volk_32fc_x2_s32fc_multiply_conjugate_add_32fc(
      m_coeff.data(), m_coeff.data(), m_state.data(), factor_times_result,
      m_filter_order);
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
  for (unsigned int i = 0; i < n; i++) {
    IQSample output = single_process(samples_in[i]);
    samples_out[i] = output;
    if ((i & filter_interval) == 0) {
      // Update filter coefficients here
      update_coeff(output);
    }
  }
  assert(n == samples_out.size());
}

// end
