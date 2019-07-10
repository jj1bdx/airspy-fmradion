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

#ifndef SOFTFM_IFDOWNSAMPLER_H
#define SOFTFM_IFDOWNSAMPLER_H

#include <cstdint>
#include <vector>

#include "Filter.h"
#include "FilterParameters.h"
#include "SoftFM.h"

// IF single/2-stage Downsampler.
class IfDownsampler {
public:
  /**
   * Construct single/2-stage IF Downsampler.
   *
   * first_downsample          :: Integer first stage downsampling rate (>= 1)
   * first_coeff               :: First stage filter coefficients
   * enable_second_downsampler :: Enable second downsampler
   * second_downsample         :: Integer second stage downsampling rate (>= 1)
   * second_coeff              :: Second stage filter coefficients
   */
  IfDownsampler(unsigned int first_downsample, const IQSampleCoeff &first_coeff,
                bool enable_second_downsampler, unsigned int second_downsample,
                const IQSampleCoeff &second_coeff);

  // Process IQ samples and return downsampled output.
  void process(const IQSampleVector &samples_in, IQSampleVector &samples_out);

private:
  // Compute RMS over a small prefix of the specified sample vector.
  double rms_level_approx(const IQSampleVector &samples);

  // Data members.
  IQSampleVector m_buf_iffirstout;
  const unsigned int m_first_downsample;
  const unsigned int m_second_downsample;
  const bool m_enable_second_downsampler;

  LowPassFilterFirIQ m_iffilter_first;
  LowPassFilterFirIQ m_iffilter_second;
};

#endif
