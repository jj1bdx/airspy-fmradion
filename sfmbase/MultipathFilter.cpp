// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2019-2026 Kenji Rikitake, JJ1BDX
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

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>

#include "MultipathFilter.h"

// Class MultipathFilter
// Complex adaptive filter for reducing FM multipath.

MultipathFilter::MultipathFilter(unsigned int stages)
    : // Filter stages.
      // Size this to the echo delay spread; more stages is
      // not safer. Each stage adds 4 taps spanning 10.4us,
      // 3:1 before/after the reference. Taps beyond the
      // delay spread carry no signal and add gradient noise,
      // which raises the residual error and costs CPU.
      m_stages(stages)

      // Filter reference point position in the vector.
      ,
      m_index_reference_point((m_stages * 3) + 1)

      // Filter order, equals to the size of the coefficient vector.
      ,
      m_filter_order((m_stages * 4) + 1)

      // Scale alpha with the filter order so that the per-tap adaptation
      // rate mu ~= alpha_effective / m_filter_order stays at the value it has
      // at the reference order, instead of falling as 1 / m_filter_order.
      // Clamped because the stability limit of this loop is on alpha itself,
      // not on mu: alpha = 1.0 diverges regardless of the order.
      // See doc/MULTIPATH_FILTER_DESIGN_20260724.md §15.
      ,
      m_alpha(std::min(alpha * static_cast<double>(m_filter_order) /
                           static_cast<double>(alpha_reference_order),
                       alpha_maximum))

      // mu = alpha_effective / (sum of input signal state square norm).
      // When the reference level = 1.0,
      // estimated norm of the input state vector ~= m_filter_order
      // * norm(input_signal) ~= 1.0 since abs(input_signal) ~= 1.0
      // * measurement suggests that the error of mu to
      //   the actually measureed mu is nominally +-10% or less
      // Note: reference level is 1.0, therefore excluded from this expression.
      ,
      m_mu(m_alpha / m_filter_order)

      // Initialize coefficient and state vectors with the size.
      // The state ring buffer holds each sample twice (see the header).
      ,
      m_coeff(m_filter_order), m_state(m_filter_order * 2), m_window(nullptr),
      m_state_pos(0), m_state_power(0), m_resync_cnt(0),
      // Initialize calculation error value.
      m_error(0) {

  assert(stages > 0);
  // Guard against constructor overflow on m_filter_order = stages*4 + 1
  // and m_index_reference_point = stages*3 + 1.
  assert(stages < (UINT_MAX / 4));
  reset_state();
  initialize_coefficients();
}

// Clear the delay line and every quantity derived from it.
void MultipathFilter::reset_state() {
  for (unsigned int i = 0; i < m_filter_order * 2; i++) {
    m_state[i] = IQSample(0, 0);
  }
  m_state_pos = 0;
  m_window = m_state.data() + 1;
  m_state_power = 0;
  m_resync_cnt = 0;
}

// Recompute the window power sum exactly.
void MultipathFilter::resync_state_power() {
  double sum = 0;
  for (unsigned int i = 0; i < m_filter_order; i++) {
    sum += std::norm(m_window[i]);
  }
  m_state_power = sum;
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
  // m_state[m_state_pos] still holds the sample written m_filter_order steps
  // ago, i.e. the one leaving the window; update the running power sum with
  // it before it is overwritten.
  m_state_power += std::norm(filter_input) - std::norm(m_state[m_state_pos]);
  // Store the newest sample twice so that the newest m_filter_order samples
  // stay contiguous: the window is [m_state_pos + 1, m_state_pos + 1 + N).
  m_state[m_state_pos] = filter_input;
  m_state[m_state_pos + m_filter_order] = filter_input;
  m_window = m_state.data() + m_state_pos + 1;
  m_state_pos++;
  if (m_state_pos == m_filter_order) {
    m_state_pos = 0;
  }
  IQSample output = IQSample(0, 0);
  // VOLK calculation, equivalent to:
  // for (unsigned int i = 0; i < m_filter_order; i++) {
  //   output += m_window[i] * m_coeff[i];
  // }
  volk_32fc_x2_dot_prod_32fc(&output, m_window, m_coeff.data(), m_filter_order);
  return output;
}

// Update coefficients by complex LMS/CMA method.
inline void MultipathFilter::update_coeff(const IQSample result) {

  // Input instant envelope
  const double env = std::norm(result);
  // error = [desired signal] - [filter output]
  const double error = if_target_level - env;

  // Normalized LMS (NLMS) processing
  // The square norm of the input data window is maintained incrementally in
  // single_process(); recompute it exactly every resync_interval updates to
  // bound the accumulated rounding drift of the running sum.
  if (++m_resync_cnt >= resync_interval) {
    m_resync_cnt = 0;
    resync_state_power();
  }
  const double state_mag_sq_sum = std::max(0.0, m_state_power);

  // Obtain the step size (dymanically computed)
  // Add offset to prevent division-by-zero error
  m_mu = m_alpha / (state_mag_sq_sum + 1e-10);

  // Calculate correlation vector
  const float factor = error * m_mu;
  const MfCoeff factor_times_result =
      MfCoeff(factor * result.real(), factor * result.imag());
// Recalculate all coefficients
// VOLK calculation, equivalent to:
// for (unsigned int i = 0; i < m_filter_order; i++) {
//  m_coeff[i] += factor_times_result * std::conj(m_window[i]);
// }
// Note: always check if the result and the source vectors can overlap!
// For volk_32fc_x2_s32fc_multiply_conjugate_add_32fc(),
// the overlapping issue seems to be OK.
// Note 2: from VOLK 3.1.0 the API parameter type has been changed,
// handled in the following if-else-endif clause.
#if VOLK_VERSION < 030100
  // Before 3.1.0
  volk_32fc_x2_s32fc_multiply_conjugate_add_32fc(m_coeff.data(), m_coeff.data(),
                                                 m_window, factor_times_result,
                                                 m_filter_order);
#else
  // 3.1.0 and later (version inclusive)
  volk_32fc_x2_s32fc_multiply_conjugate_add2_32fc(
      m_coeff.data(), m_coeff.data(), m_window, &factor_times_result,
      m_filter_order);
#endif // VOLK_VERSION
  // Set the middle (position 0) coefficient to 1+0j (unity)
  m_coeff[m_index_reference_point] = MfCoeff(1, 0);
  // Set the latest error value for monitoring
  m_error = error;
}

// Process block samples.
bool MultipathFilter::process(const IQSampleVector &samples_in,
                              IQSampleVector &samples_out) {
  unsigned int n = samples_in.size();
  if (n == 0) {
    // Do nothing, return as successful
    return true;
  }
  samples_out.resize(n);

  // Note: this is bitmask
  // Run the update every four (4) samples to reduce CPU load
  // This still maintain 384000/4 = 96000 times/sec update rate
  const unsigned int filter_interval = 0x03;
  for (unsigned int i = 0; i < n; i++) {
    IQSample output = single_process(samples_in[i]);
    // Note well: -ffast-math DISABLES NaN processing (-menable-no-nans)
    // so DO NOT specify -ffast-math!
    // Check the output magnitude against divergence_limit rather than
    // against infinity: a diverging loop passes through many orders of
    // magnitude of obviously-wrong output long before it overflows, and
    // every one of those samples reaches the discriminator.
    // The comparisons are written as "not within the limit" so that NaN,
    // which compares false against everything, is rejected as well.
    if (!(std::abs(output.real()) <= divergence_limit) ||
        !(std::abs(output.imag()) <= divergence_limit)) {
      return false;
    }
    samples_out[i] = output;
    if ((i & filter_interval) == 0) {
      // Update filter coefficients here
      update_coeff(output);
      // Check the error value the same way. With if_target_level = 1.0 the
      // error cannot legitimately leave a small neighbourhood of zero.
      if (!(std::abs(m_error) <= divergence_limit)) {
        return false;
      }
    }
  }
  assert(n == samples_out.size());
  return true;
}

// end
