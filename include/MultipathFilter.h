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
  // IF AGC target level is 1.0. For memorandum only: IfSimpleAgc ASSUMES a
  // reference level of 1.0.
  static constexpr double if_target_level = 1.0;

  // NLMS step numerator at the reference order: mu = alpha / sum(|state|^2) ~
  // alpha / order (IF AGC keeps |state| ~ 1), so alpha scales with the order.
  // Normalized CMA: the NLMS 0 < alpha < 2 bound does NOT apply; alpha = 1.0
  // diverges. See doc/MULTIPATH_FILTER_DESIGN_20260724.md §15.
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

  // Divergence threshold for process() on the filter output components and the
  // error value (near 1 and 0 with the IF AGC); 10 trips ~6 orders of magnitude
  // before isfinite(). See doc/MULTIPATH_FILTER_DESIGN_20260724.md §16.2.
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

  // Process IQ samples and return filtered IQ samples. Return true on success
  // (or empty input), false if the output or error left divergence_limit
  // (including NaN and inf).
  bool process(const IQSampleVector &samples_in, IQSampleVector &samples_out);

  // Obtain the latest error value.
  double get_error() const { return m_error; }

  // Obtain the internal filter coefficient.
  const MfCoeffVector &get_coefficients() const { return m_coeff; }

  // Note: there is no get_reference_level(). The reference tap is pinned to
  // 1+0j at the end of every update, so such an accessor could only ever
  // return 1.0. If the constraint is ever softened, add it back.

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
