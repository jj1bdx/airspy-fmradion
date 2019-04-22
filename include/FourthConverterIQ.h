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

#ifndef SOFTFM_FOURTHCONVERTERIQ_H
#define SOFTFM_FOURTHCONVERTERIQ_H

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

#include "SoftFM.h"

// Converting Fs/4 tuner.
class FourthConverterIQ {
public:
  // Construct Fs/4 downconverting tuner.
  // up : true if upconverting
  //    : false if downconverting 
  FourthConverterIQ(bool up) : m_index(0),
    m_tblidx0(up ? 3 : 1),
    m_tblidx1(up ? 0 : 2),
    m_tblidx2(up ? 1 : 3),
    m_tblidx3(up ? 2 : 0) {
    // do nothing
  }
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
      // downconvert: +1, +j, -1, -j
      // upconvert: +1, -j, -1, +j
      case 0:
        // multiply +1
        y = s;
        tblidx = m_tblidx0;
        break;
      case 1:
        // multiply +j 
        y = IQSample(im, -re);
        tblidx = m_tblidx1;
        break;
      case 2:
        // multiply -1
        y = IQSample(-re, -im);
        tblidx = m_tblidx2;
        break;
      case 3:
        // multiply -j
        y = IQSample(-im, re);
        tblidx = m_tblidx3;
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
  const unsigned int m_tblidx0;
  const unsigned int m_tblidx1;
  const unsigned int m_tblidx2;
  const unsigned int m_tblidx3;
};

#endif
