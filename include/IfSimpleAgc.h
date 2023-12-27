// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2022-2024 Kenji Rikitake, JJ1BDX
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

#ifndef INCLUDE_IFSIMPLEAGC_H
#define INCLUDE_IFSIMPLEAGC_H

#include "SoftFM.h"

// Algorithm:
// Etienne Tisserand, Yves Berviller.
// Design and implementation of a new digital automatic gain control.
// Electronics Letters, IET, 2016, 52 (22), pp.1847 - 1849.
// ff10.1049/el.2016.1398ff. ffhal-01397371f
// https://hal.univ-lorraine.fr/hal-01397371/document

// Implementation reference:
// https://github.com/sile/dagc/

// Class IfSimpleAgc
// Using float for faster computation

class IfSimpleAgc {
public:
  // Construct IF AGC.
  // Target level = 1.0.
  // initial_gain :: Initial gain value.
  // max_gain     :: Maximum gain value.
  // rate         :: rate factor for changing the gain value.
  IfSimpleAgc(const float initial_gain, const float max_gain, const float rate);

  // Reset AGC gain to the initial_gain.
  void reset_gain();

  // Process IQ samples.
  void process(const IQSampleVector &samples_in, IQSampleVector &samples_out);

  // Return IF AGC current gain.
  float get_current_gain() const { return m_current_gain; }

private:
  float m_initial_gain;
  float m_current_gain;
  float m_max_gain;
  float m_distortion_rate;
};

#endif
