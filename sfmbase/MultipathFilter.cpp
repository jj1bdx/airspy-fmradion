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

// Class MultipathFilter

MultipathFilter::MultipathFilter(unsigned int stages, double reference_level)
    : m_filter_order((stages * 2) + 1), m_coeff(m_filter_order),
      m_state(m_filter_order) {
  fprintf(stderr, "m_filter_order = %d\n", m_filter_order);
  assert(stages > 0);
  for (unsigned int i = 0; i < m_filter_order; i++) {
    m_coeff[i] = MfCoeff(0, 0);
    m_state[i] = IQSample(0, 0);
  }
  m_coeff[stages] = MfCoeff(1, 0);
}

IQSample MultipathFilter::single_process(const IQSample filter_input) {
  // Add new input to the last element of m_state
  // and remove the top element
  m_state.emplace_back(filter_input);
  m_state.erase(m_state.begin());
  IQSample output = IQSample(0, 0);
  for (unsigned int i = 0; i < m_filter_order; i++) {
    output += m_state[i] * m_coeff[i];
  }
  return output;
}

// Process samples.
// Note: the coefficients are NOT NECESSARILY symmetric
void MultipathFilter::process(const IQSampleVector &samples_in,
                              IQSampleVector &samples_out) {
  unsigned int n = samples_in.size();
  if (n == 0) {
    return;
  }
  samples_out.resize(n);

  unsigned int i = 0;
  for (; i < n; i++) {
    samples_out[i] = single_process(samples_in[i]);
    // TODO: update filter coefficients here
  }

  assert(i == samples_out.size());
}

// end
