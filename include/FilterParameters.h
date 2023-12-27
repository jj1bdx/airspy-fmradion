// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2019-2024 Kenji Rikitake, JJ1BDX
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

#ifndef INCLUDE_FILTERPARAMETERS_H
#define INCLUDE_FILTERPARAMETERS_H

#include "SoftFM.h"

// Class for providing filter parameters
// based on pre-calculated tables

// Class declaration

class FilterParameters {
public:
  static const IQSampleCoeff delay_3taps_only_iq;

  static const SampleCoeff jj1bdx_48khz_fmaudio;
  static const SampleCoeff jj1bdx_48khz_nbfmaudio;
  static const SampleCoeff delay_3taps_only_audio;

  static const IQSampleCoeff jj1bdx_ssb_12khz_1500hz;
  static const IQSampleCoeff jj1bdx_am_48khz_narrow;
  static const IQSampleCoeff jj1bdx_am_48khz_medium;
  static const IQSampleCoeff jj1bdx_am_48khz_default;
  static const IQSampleCoeff jj1bdx_am_48khz_wide;
  static const IQSampleCoeff jj1bdx_cw_12khz_500hz;
  static const IQSampleCoeff jj1bdx_nbfm_48khz_default;
  static const IQSampleCoeff jj1bdx_nbfm_48khz_narrow;
  static const IQSampleCoeff jj1bdx_nbfm_48khz_medium;
  static const IQSampleCoeff jj1bdx_nbfm_48khz_wide;
  static const IQSampleCoeff jj1bdx_fm_384kHz_narrow;
  static const IQSampleCoeff jj1bdx_fm_384kHz_medium;

  // TODO: Hilbert filter coefficients are ASYMMETRIC,
  // so they should not be treated the same as
  // LPF/BPF with symmetric coefficients
};

#endif // INCLUDE_FILTERPARAMETERS_H
