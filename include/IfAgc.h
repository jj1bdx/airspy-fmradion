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

#ifndef SOFTFM_IFAGC_H
#define SOFTFM_IFAGC_H

#include <cmath>
#include <cstdint>

#include "SoftFM.h"

// Class IfAgc
// Using float for faster computation

class IfAgc {
public:
  // Construct IF AGC.
  // initial_gain :: Initial gain value.
  // max_gain     :: Maximum gain value.
  // reference    :: target output level.
  // rate         :: rate factor for changing the gain value.
  IfAgc(const float initial_gain, const float max_gain, const float reference,
        const float rate);

  // Process IQ samples.
  void process(const IQSampleVector &samples_in, IQSampleVector &samples_out);

  // Return IF AGC current gain.
  float get_current_gain() const { return std::exp(m_log_current_gain); }

private:
  float m_log_current_gain;
  float m_log_max_gain;
  float m_log_reference;
  float m_rate;
};

#endif
