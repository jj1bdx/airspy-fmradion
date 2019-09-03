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

#include "IfAgc.h"

// class IfAgc

IfAgc::IfAgc(const double initial_gain, const double max_gain,
             const double reference, const double rate)
    // Initialize member fields
    : m_log_current_gain(std::log(initial_gain)),
      m_log_max_gain(std::log(max_gain)), m_log_reference(std::log(reference)),
      m_rate(rate) {
  // Do nothing
}

// IF AGC.
// Algorithm shown in:
// https://mycourses.aalto.fi/pluginfile.php/119882/mod_page/content/13/AGC.pdf
void IfAgc::process(const IQSampleVector &samples_in,
                    IQSampleVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    // Compute output based on the current gain.
    double current_gain = std::exp(m_log_current_gain);
    IQSample output = IQSample(samples_in[i].real() * current_gain,
                               samples_in[i].imag() * current_gain);
    samples_out[i] = output;
    // Update the current gain.
    double log_amplitude = std::log(std::abs(output));
    double error = (m_log_reference - log_amplitude) * m_rate;
    double new_log_current_gain = m_log_current_gain + error;
    if (new_log_current_gain > m_log_max_gain) {
      new_log_current_gain = m_log_max_gain;
    }
    m_log_current_gain = new_log_current_gain;
    // fprintf(stderr, "amplitude = %.9g, error = %.9g, new = %.9g\n",
    //         log_amplitude, error, new_log_current_gain);
  }
}

// end
