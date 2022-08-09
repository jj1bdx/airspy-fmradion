// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
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

#ifndef INCLUDE_SOFTFM_H
#define INCLUDE_SOFTFM_H

#include <cassert>
#include <cinttypes>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#include <volk/volk_alloc.hh>

using IQSample = std::complex<float>;
using IQSampleVector = std::vector<IQSample>;

using IQSampleDecoded = float;
using IQSampleDecodedVector = std::vector<float>;

using Sample = double;
using SampleVector = std::vector<Sample>;

using IQSampleCoeff = std::vector<IQSample::value_type>;
using SampleCoeff = std::vector<SampleVector::value_type>;

enum class FilterType { Default, Medium, Narrow, Wide };
enum class DevType { Airspy, AirspyHF, RTLSDR, FileSource };
enum class ModType { FM, NBFM, AM, DSB, USB, LSB, CW, WSPR };
enum class OutputMode {
  RAW_INT16,
  RAW_FLOAT32,
  WAV_INT16,
  WAV_FLOAT32,
  PORTAUDIO
};

#endif
