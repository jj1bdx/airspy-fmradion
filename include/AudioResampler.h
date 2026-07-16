// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
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

#ifndef INCLUDE_AUDIORESAMPLER_H
#define INCLUDE_AUDIORESAMPLER_H

#include "SoftFM.h"

#include "CDSPResampler.h"

// class AudioResampler

class AudioResampler {
public:
  // maximum input buffer size
  static constexpr int max_input_length = 32768;
  // r8brain filter design parameters.
  // ReqTransBand is in percent of the output Nyquist frequency when
  // downsampling: 15% of 24 kHz places the passband edge at 20.4 kHz,
  // still above the 19 kHz pilot (removed downstream by the pilot-cut
  // FIR), while 120 dB stop-band attenuation remains far above the FM
  // broadcast SNR. The r8brain defaults (2.0%, 206.91 dB) cost 34.4 ms
  // of group delay at 384 kHz -> 48 kHz; these values cost 2.1 ms
  // (as-built figures, measured at the executable level).
  // See doc/LATENCY_PLAN_20260713.md sections 7-9.
  static constexpr double req_trans_band = 15.0;
  static constexpr double req_atten = 120.0;
  // Construct audio resampler.
  // input_rate : input sampling rate.
  // output_rate: input sampling rate.
  AudioResampler(const double input_rate, const double output_rate);
  // Process monaural audio samples,
  // converting input_rate to output_rate.
  void process(const SampleVector &samples_in, SampleVector &samples_out);

private:
  std::unique_ptr<r8b::CDSPResampler> m_cdspr;
};

#endif
