// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
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

#ifndef INCLUDE_PILOTPHASELOCK_H
#define INCLUDE_PILOTPHASELOCK_H

#include "AudioResampler.h"
#include "Filter.h"
#include "FilterParameters.h"
#include "IfSimpleAgc.h"
#include "MultipathFilter.h"
#include "PhaseDiscriminator.h"
#include "SoftFM.h"

// Phase-locked loop for stereo pilot.
class PilotPhaseLock {
public:
  // Static constants.
  // Expected pilot frequency (used for PPS events).
  static constexpr int pilot_frequency = 19000;
  // IF sampling rate.
  static constexpr double sample_rate_if = 384000;
  // Bandwidth (30Hz) relative to sample frequency.
  static constexpr double bandwidth = 30 / sample_rate_if;
  // Minimum pilot amplitude (lowered to prevent accidental unlocking)
  static constexpr double minsignal = 0.001;

  // Timestamp event produced once every 19000 pilot periods.
  struct PpsEvent {
    std::uint64_t pps_index;
    std::uint64_t sample_index;
    double block_position;
  };

  //
  // Construct phase-locked loop.
  //
  // freq        :: 19 kHz center frequency relative to sample freq
  //                (0.5 is Nyquist)
  PilotPhaseLock(double freq);

  //
  // Process samples and extract 19 kHz pilot tone.
  // Generate phase-locked 38 kHz tone with unit amplitude.
  // pilot_shift :: true to shift pilot phase by
  //             :: using cos(2*x) instead of sin (2*x)
  //             :: (for multipath distortion detection)
  void process(const SampleVector &samples_in, SampleVector &samples_out,
               bool pilot_shift);

  // Return true if the phase-locked loop is locked.
  bool locked() const { return m_lock_cnt >= m_lock_delay; }

  // Return detected amplitude of pilot signal.
  double get_pilot_level() const { return 2 * m_pilot_level; }

  // Return calculated frequency error.
  double get_freq_err() const { return m_freq_err; }

  // Return PPS events from the most recently processed block.
  std::vector<PpsEvent> get_pps_events() const { return m_pps_events; }

  // Erase the first PPS event.
  void erase_first_pps_event() { m_pps_events.erase(m_pps_events.begin()); }

private:
  Sample m_minfreq, m_maxfreq;
  Sample m_freq, m_phase;
  Sample m_pilot_level;
  int m_lock_delay;
  int m_lock_cnt;
  int m_pilot_periods;
  std::uint64_t m_pps_cnt;
  std::uint64_t m_sample_cnt;
  std::vector<PpsEvent> m_pps_events;
  BiquadIirFilter m_biquad_phasor_i1, m_biquad_phasor_i2;
  BiquadIirFilter m_biquad_phasor_q1, m_biquad_phasor_q2;
  FirstOrderIirFilter m_first_phase_err;
  Sample m_freq_err;
};

#endif
