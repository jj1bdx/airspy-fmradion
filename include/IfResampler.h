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

#ifndef SOFTFM_IFRESAMPLER_H
#define SOFTFM_IFRESAMPLER_H

#include <cstdint>

#include "SoftFM.h"

#include "soxr.h"

// class IfResampler

class IfResampler {
public:
  // Construct IF IQ resampler.
  // input_rate : input sampling rate.
  // output_rate: input sampling rate.
  IfResampler(const double input_rate, const double output_rate);
  // Process IQ samples.
  // converting input_rate to output_rate.
  void process(const IQSampleVector &samples_in, IQSampleVector &samples_out);

private:
  const double m_irate;
  const double m_orate;
  const double m_ratio;
  soxr_t m_soxr;
};

#endif
