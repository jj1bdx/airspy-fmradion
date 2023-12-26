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

#include "PilotPhaseLock.h"
#include "Utility.h"

// Define this to print PLL filter messages
// #define DEBUG_PLL_FILTER

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
      // approx 30Hz LPF by 2nd-order biquad IIR Butterworth filter
      // Caution: use only once for stable PLL locking
      m_biquad_phasor_i1(1.46974784e-06, 0, 0, -1.99682419, 0.996825659),
      m_biquad_phasor_q1(1.46974784e-06, 0, 0, -1.99682419, 0.996825659),
      // differentiator-like 1st-order inverse LPF (not really an HPF)
      m_first_phase_err(0.000304341788, -0.000304324564, 0), m_freq_err(0) {
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
    Sample psin = std::sin(m_phase);
    Sample pcos = std::cos(m_phase);

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
    Sample x = samples_in[i];
    Sample phasor_i = psin * x;
    Sample phasor_q = pcos * x;

    // Run IQ phase error through biquad LPFs once.
    Sample new_phasor_i = m_biquad_phasor_i1.process(phasor_i);
    Sample new_phasor_q = m_biquad_phasor_q1.process(phasor_q);

    // Convert I/Q ratio to estimate of phase error.
    // Note: maximum phase error during the locked state is +- 0.02 radian.
    // Sample phase_err = atan2(phasor_q, phasor_i);
    Sample phase_err = Utility::fast_atan2f(new_phasor_q, new_phasor_i);

    // Detect pilot level (conservative).
    m_pilot_level = std::min(m_pilot_level, new_phasor_i);

    // Run phase error through loop filter and update frequency estimate.
    // After the loop filter, the phase error is integrated to produce
    // the frequency. Then the frequency is integrated to produce the phase.
    // These two integrators form the two remaining poles, both at z = 1.

    Sample new_phase_err = m_first_phase_err.process(phase_err);
    m_freq_err = new_phase_err;
    m_freq += m_freq_err;

    // Limit frequency to allowable range.
    m_freq = std::max(m_minfreq, std::min(m_maxfreq, m_freq));

#ifdef DEBUG_PLL_FILTER
    if (i == 0) {
      fprintf(stderr,
              "m_freq = %.9g, m_freq_err = %.9g, "
              "m_pilot_level = %.9g\n",
              m_freq * FmDecoder::sample_rate_if / 2 / M_PI,
              m_freq_err * FmDecoder::sample_rate_if / 2 / M_PI, m_pilot_level);
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
