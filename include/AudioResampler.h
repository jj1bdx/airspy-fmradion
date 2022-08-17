// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2019-2022 Kenji Rikitake, JJ1BDX
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

#ifndef INCLUDE_AUDIORESAMPLER_H
#define INCLUDE_AUDIORESAMPLER_H

#include "SoftFM.h"

#include "CDSPResampler.h"

// class AudioResampler

class AudioResampler {
public:
  // Construct audio resampler.
  // input_rate : input sampling rate.
  // output_rate: input sampling rate.
  AudioResampler(const double input_rate, const double output_rate);
  // Process monaural audio samples,
  // converting input_rate to output_rate.
  void process(const SampleVector &samples_in, SampleVector &samples_out);

private:
  const double m_ratio;
  r8b::CDSPResampler *m_cdspr;
};

#endif
