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

#ifndef SOFTFM_EQPARAMETERS_H
#define SOFTFM_EQPARAMETERS_H

#include "SoftFM.h"
#include <boost/math/interpolators/cubic_b_spline.hpp>

// Class for calculating DiscriminatorEqualizer parameters
// based on pre-calculated tables with Boost Cubic B Spline interpolation.
class EqParameters {

public:
  // Constructor
  EqParameters();

  // Compute staticgain from ifrate.
  const double compute_staticgain(double ifrate);
  // Compute fitlevel from ifrate.
  const double compute_fitlevel(double ifrate);

private:
  // Parameters for pre-calculated tables.
  const double m_freq_initial;
  const double m_freq_step;
  const std::vector<double> m_vector_staticgain;
  const std::vector<double> m_vector_fitlevel;
  const boost::math::cubic_b_spline<double> m_staticgain;
  const boost::math::cubic_b_spline<double> m_fitlevel;
  // Private function to decide whether to apply interpolation or not.
  const double fitting(double ifrate, double low_limit, double high_limit,
                       const boost::math::cubic_b_spline<double> &spline);
};

#endif // SOFTFM_EQPARAMETERS_H
