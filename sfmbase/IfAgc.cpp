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

#include <cassert>

#include "IfAgc.h"
#include "Utility.h"

// class IfAgc

IfAgc::IfAgc(const float initial_gain, const float max_gain,
             const float reference, const float rate)
    // Initialize member fields
    : m_log_current_gain(std::log(initial_gain)),
      m_log_max_gain(std::log(max_gain)), m_log_reference(std::log(reference)),
      m_rate(rate) {
  // Do nothing
}

// IF AGC.
// Algorithm shown in:
// https://www.mathworks.com/help/comm/ref/comm.agc-system-object.html
// https://www.mathworks.com/help/comm/ref/linear_loop_block_diagram.png
// Note: in the original algorithm,
// log_amplitude was
//    std::log(input_magnitude) + (m_log_current_gain * 2.0),
// but in this implementation the 2.0 was removed (and set to 1.0).

void IfAgc::process(const IQSampleVector &samples_in,
                    IQSampleVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  volk::vector<float> samples_in_mag, log_gain, gain;
  samples_in_mag.resize(n);
  log_gain.resize(n);
  gain.resize(n);

  // Compute magnitude of sample input.
  volk_32fc_magnitude_32f(samples_in_mag.data(), samples_in.data(), n);

  for (unsigned int i = 0; i < n; i++) {
    // Store logarithm of current gain.
    log_gain[i] = m_log_current_gain;
    // Update the current gain.
    // Note: the original algorithm multiplied the abs(input)
    //       with the current gain (exp(log_current_gain))
    //       then took the logarithm value, but the sequence can be
    //       realigned as taking the log value of the abs(input)
    //       then add the log_current_gain.
    float log_amplitude = std::log(samples_in_mag[i]) + m_log_current_gain;
    float error = (m_log_reference - log_amplitude) * m_rate;
    float new_log_current_gain = m_log_current_gain + error;
    if (new_log_current_gain > m_log_max_gain) {
      new_log_current_gain = m_log_max_gain;
    }
    m_log_current_gain = new_log_current_gain;
  }
  // Compute output based on the saved logarithm of current gain.
  // NOTE: DO NOT USE volk_32f_expfast_32f() here
  //       because the calculation error is audible on AM mode!
  volk_32f_exp_32f(gain.data(), log_gain.data(), n);
  volk_32fc_32f_multiply_32fc(samples_out.data(), samples_in.data(),
                              gain.data(), n);
}

// end
