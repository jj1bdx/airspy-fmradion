// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2022 Kenji Rikitake, JJ1BDX
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
#include "Utility.h"

// class IfSimpleAgc

IfSimpleAgc::IfSimpleAgc(const float initial_gain, const float max_gain,
             const float reference, const float rate)
    // Initialize member fields
    : m_current_gain(initial_gain),
      m_max_gain(max_gain), m_reference(reference),
      m_distortion_rate(rate) {
  // Do nothing
}

// IF AGC based on the Tisserand-Berviller algorithm

void IfSimpleAgc::process(const IQSampleVector &samples_in,
                    IQSampleVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    IQSample x = samples_in[i];
    IQSample x2 = x * m_current_gain;
    samples_out[i] = x2;
    float mag = std::norm(x2);
    float y = mag / m_reference;
    float z = 1.0 + (m_distortion_rate * (1.0 - y));
    m_current_gain *= z;
    if (m_current_gain > m_max_gain) {
        m_current_gain = m_max_gain;
    }
  }
}

// end
