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

#ifndef SOFTFM_FILTERPARAMETERS_H
#define SOFTFM_FILTERPARAMETERS_H

#include "SoftFM.h"

// Class for providing filter parameters
// based on pre-calculated tables

// Class declaration

class FilterParameters {
public:
  static const std::vector<IQSample::value_type> lambdaprog_10000khz_div8;
  static const std::vector<IQSample::value_type> lambdaprog_1250khz_div4;
  static const std::vector<IQSample::value_type> jj1bdx_10000khz_div8;
  static const std::vector<IQSample::value_type> jj1bdx_1250khz_div4;
  static const std::vector<IQSample::value_type> jj1bdx_2500khz_div4;
  static const std::vector<IQSample::value_type> jj1bdx_600khz_625khz_div2;

  static const std::vector<IQSample::value_type> delay_3taps_only_iq;

  static const std::vector<IQSample::value_type> jj1bdx_768kHz_div2;

  static const std::vector<SampleVector::value_type> delay_3taps_only_audio;

  static const std::vector<SampleVector::value_type> jj1bdx_384k_div4;
  static const std::vector<SampleVector::value_type> jj1bdx_96k_div2;

  static const std::vector<IQSample::value_type> jj1bdx_900kHz_div3;

  static const std::vector<SampleVector::value_type> jj1bdx_312_5khz_div4;
  static const std::vector<SampleVector::value_type> jj1bdx_78_125khz_fmaudio;

  static const std::vector<SampleVector::value_type> jj1bdx_48khz_fmaudio;
};

#endif // SOFTFM_EQPARAMETERS_H
