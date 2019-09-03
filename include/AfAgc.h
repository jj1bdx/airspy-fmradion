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

#ifndef SOFTFM_AFAGC_H
#define SOFTFM_AFAGC_H

#include <cstdint>
#include <cmath>
#include <vector>

#include "SoftFM.h"
#include "util.h"

// Class AfAgc

class AfAgc {
public:
  // Construct AF AGC.
  // initial_gain :: Initial gain value.
  // max_gain     :: Maximum gain value.
  // reference    :: target output level.
  // rate         :: rate factor for changing the gain value.
  AfAgc(const double initial_gain, const double max_gain,
        const double reference, const double rate);

  // Process audio samples.
  void process(const SampleVector &samples_in, SampleVector &samples_out);

  // Return AF AGC current gain.
  double get_current_gain() const { return std::exp(m_log_current_gain); }

private:
  double m_log_current_gain;
  double m_log_max_gain;
  double m_log_reference;
  double m_rate;
};

#endif
