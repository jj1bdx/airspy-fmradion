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

#ifndef SOFTFM_FOURTHDOWNCONVERTERIQ_H
#define SOFTFM_FOURTHDOWNCONVERTERIQ_H

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "SoftFM.h"

// Downconverting Fs/4 tuner.
class FourthDownconverterIQ {
public:
  // Construct Fs/4 downconverting tuner.
  FourthDownconverterIQ() : m_index(0) {}
  // Process samples.
  // See Richard G. Lyons' explanation at
  // https://www.embedded.com/print/4007186
  inline void process(const IQSampleVector &samples_in,
                      IQSampleVector &samples_out) {
    unsigned int tblidx = m_index;
    unsigned int n = samples_in.size();

    samples_out.resize(n);

    for (unsigned int i = 0; i < n; i++) {
      IQSample y;
      const IQSample &s = samples_in[i];
      const IQSample::value_type re = s.real();
      const IQSample::value_type im = s.imag();
      switch (tblidx) {
      case 0:
        // multiply +1
        y = s;
        tblidx = 1;
        break;
      case 1:
        // multiply +j
        y = IQSample(im, -re);
        tblidx = 2;
        break;
      case 2:
        // multiply -1
        y = IQSample(-re, -im);
        tblidx = 3;
        break;
      case 3:
        // multiply -j
        y = IQSample(-im, re);
        tblidx = 0;
        break;
      default:
        // unreachable, error here;
        assert(tblidx < 4);
        break;
      }
      samples_out[i] = y;
    }

    m_index = tblidx;
  }

private:
  unsigned int m_index;
};

#endif
