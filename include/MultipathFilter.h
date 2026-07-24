// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
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

#ifndef INCLUDE_MULTIPATHFILTER_H
#define INCLUDE_MULTIPATHFILTER_H

#include "SoftFM.h"

// MfCoeff = IQSample
using MfCoeff = std::complex<float>;
// MfCoeffVector = IQSampleVector with VOLK aligned allocator
using MfCoeffVector = volk::vector<MfCoeff>;

// Multipath equalizer FIR filter.

class MultipathFilter {
public:
  // IF AGC target level is 1.0
  // Note: this constant is for memorandum use only,
  // and the actual code of IfSimpleAGC
  // ASSUMES the reference level is 1.0.
  static constexpr double if_target_level = 1.0;

  // NLMS step-size numerator, for the reference filter order below.
  // The effective step is mu = alpha_effective / sum(|state|^2); since the IF
  // AGC holds |state[i]| ~= 1, sum(|state|^2) ~= filter order, so the per-tap
  // adaptation rate is alpha_effective / filter_order. Raising the order at
  // fixed alpha therefore divides the adaptation rate, which measurably
  // degrades tracking on a time-varying channel; alpha is scaled with the
  // order to compensate. See doc/MULTIPATH_FILTER_DESIGN_20260724.md §15.
  // Note: this is normalized CMA, not unnormalized LMS, so the classic
  // 0 < alpha < 2 NLMS bound does NOT apply here -- the CMA error is
  // quadratic in the output. Divergence was measured at alpha = 1.0.
#ifndef MF_ALPHA
#define MF_ALPHA 0.1
#endif
  static constexpr double alpha = MF_ALPHA;

  // Filter order at which alpha was tuned (stages = 36).
  static constexpr unsigned int alpha_reference_order = 145;

  // Upper bound on the scaled alpha, kept well below the measured divergence
  // point of 1.0. Binds from stages ~= 181 upward.
#ifndef MF_ALPHA_MAX
#define MF_ALPHA_MAX 0.5
#endif
  static constexpr double alpha_maximum = MF_ALPHA_MAX;

  // Number of coefficient updates between exact recomputations of the
  // incrementally-maintained state power sum.
  static constexpr unsigned int resync_interval = 65536;

  // Divergence threshold for process(), applied to the filter output
  // components and to the error value. With the IF AGC holding the input at
  // unity, both quantities stay within a small neighbourhood of 1 and 0
  // respectively; 10 is roughly a decade of headroom over anything
  // legitimate. Bounding the magnitude rather than only testing isfinite()
  // catches a diverging loop about six orders of magnitude earlier.
  // See doc/MULTIPATH_FILTER_DESIGN_20260724.md §16.2.
  static constexpr float divergence_limit = 10.0f;

  // Construct multipath filter.
  // Note: the reference level is fixed to 1.0.
  // stages :: number of filter stages
  MultipathFilter(unsigned int stages);

  // Initialize filter coefficients.
  void initialize_coefficients();

  // Clear the delay line. Required after a divergence reset, otherwise the
  // non-finite sample which caused the reset stays in the delay line and
  // keeps poisoning the output until it ages out.
  void reset_state();

  // Process IQ samples and return filtered IQ samples.
  // Return true if the processing is successful
  // (or the input length is zero);
  // Return false if the processing fails
  // (i.e., internal values contains infinite values (NaN, inf, etc)).
  bool process(const IQSampleVector &samples_in, IQSampleVector &samples_out);

  // Obtain the latest error value.
  const double get_error() const { return m_error; }

  // Obtain the internal filter coefficient.
  const MfCoeffVector &get_coefficients() { return m_coeff; }

  // Obtain the referenct point level value.
  // Initial value is 1.0.
  const float get_reference_level() {
    return m_coeff[m_index_reference_point].real();
  }

private:
  // Process single value.
  inline IQSample single_process(const IQSample filter_input);

  // Update coefficient.
  inline void update_coeff(const IQSample result);

  // Recompute the state power sum from scratch, to bound the drift of the
  // incrementally-maintained m_state_power.
  void resync_state_power();

  // Data members.
  const unsigned int m_stages;
  const unsigned int m_index_reference_point;
  const unsigned int m_filter_order;
  const double m_alpha;
  float m_mu;
  MfCoeffVector m_coeff;
  // Ring buffer of 2 * m_filter_order elements holding each input sample
  // twice, so that the newest m_filter_order samples are always readable as
  // one contiguous span (m_window) without shifting the whole vector.
  MfCoeffVector m_state;
  const MfCoeff *m_window;
  unsigned int m_state_pos;
  // Running sum of |m_state[i]|^2 over the window, maintained incrementally.
  double m_state_power;
  unsigned int m_resync_cnt;
  double m_error;
};

#endif
