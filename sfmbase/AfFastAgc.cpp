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

/*
This software is part of libcsdr, a set of simple DSP routines for
Software Defined Radio.
Copyright (c) 2014, Andras Retzler <randras@sdr.hu>
All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ANDRAS RETZLER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <cassert>
#include <cmath>

#include "AfFastAgc.h"

// class AfFastAgc

AfFastAgc::AfFastAgc(const double initial_gain, const double max_gain,
                     const double reference)
    // Initialize member fields
    : m_last_gain(initial_gain), m_max_gain(max_gain),
      m_peak1(0), m_peak2(0),
      m_reference(reference) {
  // Do nothing
}

// AF Fast AGC.
// Algorithm: function fastagc_ff() in
// https://github.com/simonyiszk/csdr/blob/master/libcsdr.c
void AfFastAgc::process(const SampleVector &samples_in,
		SampleVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);
  m_buf1.resize(n);
  m_buf2.resize(n);

  double peak = 0;
  for (unsigned int i = 0; i < n; i++) {
    double v = fabs(samples_in[i]);
    if (v > peak) {
      peak = v;
    }
  }

  double target_peak = peak;
  if (target_peak < m_peak2) {
    target_peak = m_peak2;
  }
  if (target_peak < m_peak1) {
    target_peak = m_peak1;
  }

  double target_gain = m_reference / target_peak;
  if (target_gain > m_max_gain) {
    target_gain = m_max_gain;
  }

  for (unsigned int i = 0; i < n; i++) {
    double rate = (double)i / (double)n;
    double gain = (m_last_gain * (1.0 - rate)) + (target_gain * rate);
    samples_out[i] = m_buf1[i] * gain;
  }

  m_buf1 = m_buf2;
  m_peak1 = m_peak2;
  m_buf2 = samples_in;
  m_peak2 = peak;
  m_last_gain = target_gain;

  // fprintf(stderr, "m_last_gain= %f\n", m_last_gain);
}

// end
