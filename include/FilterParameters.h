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
};

// Values of vectors

const std::vector<IQSample::value_type>
    FilterParameters::lambdaprog_10000khz_div8 = {
        0.000163684682470875, 0.000772584837442848, 0.002021239686872703,
        0.004435627121249832, 0.008375401475660970, 0.014239715819450282,
        0.022188299353843657, 0.032132032467856633, 0.043618498598846187,
        0.055856224488393495, 0.067771123204540540, 0.078156099008024574,
        0.085853232693530845, 0.089952397742173520, 0.089952397742173520,
        0.085853232693530845, 0.078156099008024574, 0.067771123204540540,
        0.055856224488393495, 0.043618498598846187, 0.032132032467856633,
        0.022188299353843657, 0.014239715819450282, 0.008375401475660970,
        0.004435627121249832, 0.002021239686872703, 0.000772584837442848,
        0.000163684682470875};

const std::vector<IQSample::value_type>
    FilterParameters::lambdaprog_1250khz_div4 = {
        0.000167223634636264,  0.000027551101146017,  -0.001378303988768317,
        -0.005408740169442248, -0.012543460485155080, -0.020712088221511341,
        -0.024363124345677958, -0.015648206458168671, 0.011948528295791011,
        0.059000708872984725,  0.117327789318493250,  0.171381314496520470,
        0.204012335784630960,  0.204012335784630960,  0.171381314496520470,
        0.117327789318493250,  0.059000708872984725,  0.011948528295791011,
        -0.015648206458168671, -0.024363124345677958, -0.020712088221511341,
        -0.012543460485155080, -0.005408740169442248, -0.001378303988768317,
        0.000027551101146017,  0.000167223634636264};

#endif // SOFTFM_EQPARAMETERS_H
