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
#include <boost/circular_buffer.hpp>
#include <vector>

typedef std::complex<float> MfCoeff;
typedef std::vector<MfCoeff> MfCoeffVector;

// Multipath equalizer FIR filter.

class MultipathFilter {
public:
  // Construct multipath filter.
  // stages             :: number of filter stages
  // reference_level    :: reference envelope amplitude level
  MultipathFilter(unsigned int stages, double reference_level);

  // Process IQ samples and return filtered IQ samples.
  void process(const IQSampleVector &samples_in, IQSampleVector &samples_out);

  // Obtain the latest error value.
  double get_error() { return m_error; }

private:
  // Initialize filter coefficients.
  inline void initialize_coefficients();

  // Process single value.
  inline IQSample single_process(const IQSample filter_input);

  // Update coefficient.
  inline void update_coeff(const IQSample result);

  // Data members.
  unsigned int m_stages;
  unsigned int m_filter_order;
  MfCoeffVector m_coeff;
  boost::circular_buffer<IQSample> m_state;
  double m_reference_level;
  double m_error;
};

#endif
