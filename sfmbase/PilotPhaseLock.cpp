// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2019-2026 Kenji Rikitake, JJ1BDX
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

#include "PilotPhaseLock.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

// Define this to print PLL filter messages
// #undef DEBUG_PLL_FILTER

#ifdef DEBUG_PLL_FILTER
#include "FmDecode.h"
#endif // DEBUG_PLL_FILTER

// class PilotPhaseLock

// Construct phase-locked loop.
PilotPhaseLock::PilotPhaseLock(double freq)
    : // Set min/max locking frequencies.
      m_minfreq((freq - bandwidth) * 2.0 * M_PI),
      m_maxfreq((freq + bandwidth) * 2.0 * M_PI),
      // Set valid signal threshold.
      // Initialize frequency and phase.
      m_freq(freq * 2.0 * M_PI), m_phase(0),
      // Lock decision time: 0.5 second (for 30Hz bandwidth)
      m_pilot_level(0), m_lock_delay(int(15.0 / bandwidth)), m_lock_cnt(0),
      // Initialize PPS generator.
      m_pilot_periods(0), m_pps_cnt(0), m_sample_cnt(0), m_pps_events(0),
      // In-loop phasor LPF: 2nd-order all-pole IIR (real corners ~40/188 Hz),
      // widened from the original ~34/160 Hz so the dominant closed-loop pole
      // pair is damped at zeta ~= 0.71 (was ~0.57). Unity DC gain; 38 kHz image
      // still suppressed by ~105 dB. Caution: use only once for stable locking.
      // See doc/PLL_REDESIGN_20260723.md and doc/PLL_ANALYSIS_20260722.md.
      m_biquad_phasor_i1(2.037743564e-06, 0, 0, -1.996259818, 0.996261856),
      m_biquad_phasor_q1(2.037743564e-06, 0, 0, -1.996259818, 0.996261856),
      // PI-controller proportional term / stabilizing zero (with the m_freq
      // accumulator as the integrator). Gains rescaled x0.889 vs the original
      // to hold the loop natural frequency at ~22 Hz after widening the LPF.
      m_first_phase_err(2.705503620719e-04, -2.705350504729e-04, 0),
      m_freq_err(0) {
  // do nothing
}

// Process samples and generate the 38kHz locked tone.
void PilotPhaseLock::process(const SampleVector &samples_in,
                             SampleVector &samples_out, bool pilot_shift) {
  unsigned int n = samples_in.size();

  samples_out.resize(n);

  bool was_locked = (m_lock_cnt >= m_lock_delay);
  m_pps_events.clear();

  if (n > 0) {
    m_pilot_level = 1000.0;
  } else {
    // n == 0
    // Do nothing when the input size is 0
    return;
  }

  for (unsigned int i = 0; i < n; i++) {

    // Generate locked pilot tone.
    // The PLL recursion runs in double precision regardless of the
    // Sample type; only the samples_out[i] stores narrow to Sample.
    double psin = std::sin(m_phase);
    double pcos = std::cos(m_phase);

    // Generate double-frequency output.
    if (pilot_shift) {
      // Use cos(2*x) to shift phase for pi/4 (90 degrees)
      // cos(2*x) = 2 * cos(x) * cos(x) - 1
      samples_out[i] = 2 * pcos * pcos - 1;
    } else {
      // Proper phase: not shifted
      // sin(2*x) = 2 * sin(x) * cos(x)
      samples_out[i] = 2 * psin * pcos;
    }

    // Multiply locked tone with input.
    double x = samples_in[i];
    double phasor_i = psin * x;
    double phasor_q = pcos * x;

    // Run IQ phase error through biquad LPFs once.
    double new_phasor_i = m_biquad_phasor_i1.process(phasor_i);
    double new_phasor_q = m_biquad_phasor_q1.process(phasor_q);

    // Convert I/Q ratio to estimate of phase error.
    // Note: maximum phase error during the locked state is +- 0.02 radian.
    // Use double std::atan2 for the phase detector.
    // For the performance and accuracy analysis,
    // see doc/CORE_MATH_ATAN2F_20260722.md and
    // doc/STD_ATAN2_X86_64_20260722.md.
    double phase_err = std::atan2(new_phasor_q, new_phasor_i);

    // Calculate pilot level (accurate).
    m_pilot_level = std::sqrt((new_phasor_i * new_phasor_i) +
                              (new_phasor_q * new_phasor_q));

    // Run phase error through loop filter and update frequency estimate.
    // After the loop filter, the phase error is integrated to produce
    // the frequency. Then the frequency is integrated to produce the phase.
    // These two integrators form the two remaining poles, both at z = 1.

    double new_phase_err = m_first_phase_err.process(phase_err);
    m_freq_err = new_phase_err;
    m_freq += m_freq_err;

    // Limit frequency to allowable range.
    m_freq = std::max(m_minfreq, std::min(m_maxfreq, m_freq));

#ifdef DEBUG_PLL_FILTER
    if (i == 0) {
      fmt::println(stderr,
                   "m_freq = {:.9g}, m_freq_err = {:.9g}, "
                   "m_pilot_level = {:.9g}",
                   m_freq * FmDecoder::sample_rate_if / 2 / M_PI,
                   m_freq_err * FmDecoder::sample_rate_if / 2 / M_PI,
                   m_pilot_level);
    }
#endif

    // Update locked phase.
    m_phase += m_freq;
    if (m_phase > 2.0 * M_PI) {
      m_phase -= 2.0 * M_PI;
      m_pilot_periods++;

      // Generate pulse-per-second.
      if (m_pilot_periods == pilot_frequency) {
        m_pilot_periods = 0;
        if (was_locked) {
          struct PpsEvent ev;
          ev.pps_index = m_pps_cnt;
          ev.sample_index = m_sample_cnt + i;
          ev.block_position = double(i) / double(n);
          m_pps_events.push_back(ev);
          m_pps_cnt++;
        }
      }
    }
  }

  // Update lock status.
  if (2 * m_pilot_level > minsignal) {
    if (m_lock_cnt < m_lock_delay) {
      m_lock_cnt += n;
    }
  } else {
    m_lock_cnt = 0;
  }

  // Drop PPS events when pilot not locked.
  if (m_lock_cnt < m_lock_delay) {
    m_pilot_periods = 0;
    m_pps_cnt = 0;
    m_pps_events.clear();
  }

  // Update sample counter.
  m_sample_cnt += n;
}

// end
