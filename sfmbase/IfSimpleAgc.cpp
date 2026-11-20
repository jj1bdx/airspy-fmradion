// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2022-2026 Kenji Rikitake, JJ1BDX
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

#include "IfSimpleAgc.h"

// class IfSimpleAgc

IfSimpleAgc::IfSimpleAgc(const float initial_gain, const float max_gain,
                         const float rate)
    // Initialize member fields
    : m_initial_gain(initial_gain), m_max_gain(max_gain),
      m_distortion_rate(rate) {
  // A positive gain stays positive only if the ratio floor is positive.
  assert(m_initial_gain > 0.0f);
  reset_gain();
}

// Reset AGC gain to the initial_gain.
void IfSimpleAgc::reset_gain() { m_current_gain = m_initial_gain; }

// IF AGC based on the Tisserand-Berviller algorithm
// Target level = 1.0

void IfSimpleAgc::process(const IQSampleVector &samples_in,
                          IQSampleVector &samples_out) {
  const unsigned int n = samples_in.size();
  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    IQSample x = samples_in[i];
    IQSample x2 = x * m_current_gain;
    samples_out[i] = x2;
    float z = 1.0f + (m_distortion_rate * (1.0f - std::norm(x2)));
    // Lower limit of z: the first-order Taylor approximation of
    // exp(d * (1 - |y|^2)) is only valid for small d * |1 - |y|^2|.
    // Without the limit, a large overshoot makes z <= 0.
    if (z < min_ratio) {
      z = min_ratio;
    }
    m_current_gain *= z;
    // Check if m_current_gain is finite
    if (!std::isfinite(m_current_gain)) {
      reset_gain();
    } else {
      if (m_current_gain > m_max_gain) {
        m_current_gain = m_max_gain;
      }
    }
  }
}

// end
