// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
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

#ifndef SOFTFM_MULTIPATHFILTER_H
#define SOFTFM_MULTIPATHFILTER_H

#include "SoftFM.h"
#include <deque>
#include <vector>

typedef std::complex<float> MfCoeff;
typedef std::vector<MfCoeff> MfCoeffVector;

// Multipath equalizer FIR filter.

class MultipathFilter {
public:
  // IF AGC target level is 1.0
  // Note: this constant is for memorandum use only,
  // and the actual code ASSUMES the reference level is 1.0.
  static constexpr double if_target_level = 1.0;

  // LMS algorithm stepsize.
  // Increasing this value makes the filter unstable.
  // maximum amplitude must be less than sqrt(2 / alpha)
  // to maintain the filter convergence.
  static constexpr double alpha = 0.1;

  // Construct multipath filter.
  // Note: the reference level is fixed to 1.0.
  // stages :: number of filter stages
  MultipathFilter(unsigned int stages);

  // Initialize filter coefficients.
  void initialize_coefficients();

  // Process IQ samples and return filtered IQ samples.
  void process(const IQSampleVector &samples_in, IQSampleVector &samples_out);

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

  // Data members.
  const unsigned int m_stages;
  const unsigned int m_index_reference_point;
  const unsigned int m_filter_order;
  const double m_mu;
  MfCoeffVector m_coeff;
  IQSampleVector m_state;
  double m_error;
};

#endif
