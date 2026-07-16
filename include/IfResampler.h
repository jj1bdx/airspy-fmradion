// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
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

#ifndef INCLUDE_IFRESAMPLER_H
#define INCLUDE_IFRESAMPLER_H

#include "SoftFM.h"

#include "CDSPResampler.h"

// class IfResampler

class IfResampler {
public:
  // maximum input buffer size
  static constexpr int max_input_length = 65536;
  // r8brain filter design parameters.
  // ReqTransBand is in percent of the output Nyquist frequency when
  // downsampling: 10% of the 192 kHz Nyquist at the 384 kHz IF rate
  // places the passband edge at 172.8 kHz, far beyond the FM broadcast
  // channel bandwidth, and 140 dB stop-band attenuation exceeds any
  // SDR frontend's dynamic range. The previous CDSPResampler24 preset
  // (2.0%, 180.15 dB) cost 6.2 ms of group delay at
  // 1152 kHz -> 384 kHz; these values cost 0.7 ms (as-built figures,
  // measured at the executable level).
  // See doc/LATENCY_PLAN_20260713.md sections 7-9.
  static constexpr double req_trans_band = 10.0;
  static constexpr double req_atten = 140.0;
  // Construct IF IQ resampler.
  // input_rate : input sampling rate.
  // output_rate: input sampling rate.
  IfResampler(const double input_rate, const double output_rate);
  // Process IQ samples.
  // converting input_rate to output_rate.
  void process(const IQSampleVector &samples_in, IQSampleVector &samples_out);

private:
  std::unique_ptr<r8b::CDSPResampler> m_cdspr_re;
  std::unique_ptr<r8b::CDSPResampler> m_cdspr_im;
};

#endif
