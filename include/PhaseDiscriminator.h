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

#ifndef SOFTFM_PHASEDISCRIMINATOR_H
#define SOFTFM_PHASEDISCRIMINATOR_H

#include <cstdint>
#include <vector>

#include "SoftFM.h"

#include <volk/volk_alloc.hh>

/* Detect frequency by phase discrimination between successive samples. */
class PhaseDiscriminator {
public:
  /**
   * Construct phase discriminator.
   *
   * max_freq_dev :: Full scale frequency deviation relative to the
   *                 full sample frequency.
   */
  PhaseDiscriminator(double max_freq_dev);

  /**
   * Process samples.
   * Output is a sequence of frequency estimates, scaled such that
   * output value +/- 1.0 represents the maximum frequency deviation.
   */
  void process(const IQSampleVector &samples_in,
		  IQSampleDecodedVector &samples_out);

private:
  const Sample m_normalize_factor;
  const float m_boundary;
  float m_save_value;
  volk::vector<float> m_phase;
};

#endif
