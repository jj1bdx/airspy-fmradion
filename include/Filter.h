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

#ifndef INCLUDE_FILTER_H
#define INCLUDE_FILTER_H

#include "SoftFM.h"

// Low-pass filter for IQ samples.
class LowPassFilterFirIQ {
public:
  //
  // Construct low-pass filter.
  //
  // coeff        :: FIR filter coefficients.
  // downsample   :: Integer downsampling rate (>= 1)
  //
  LowPassFilterFirIQ(const IQSampleCoeff &coeff, const unsigned int downsample);

  // Process samples.
  void process(const IQSampleVector &samples_in, IQSampleVector &samples_out);

private:
  const IQSampleCoeff m_coeff;
  IQSampleVector m_state;
  unsigned int m_order;
  unsigned int m_downsample;
  unsigned int m_pos;
};

// Low-pass filter for mono audio signal.
class LowPassFilterFirAudio {
public:
  //
  // Construct low-pass mono audio filter. No down/up-sampling.
  //
  // coeff        :: FIR filter coefficients.
  //
  LowPassFilterFirAudio(const SampleCoeff &coeff);

  // Process samples.
  void process(const SampleVector &samples_in, SampleVector &samples_out);

private:
  SampleCoeff m_coeff;
  SampleVector m_state;
  unsigned int m_order;
  unsigned int m_pos;
};

// Generic 1st-order Direct Form 2 IIR filter
class FirstOrderIirFilter {
public:
  //
  // Construct generic 1st-order Direct Form 2 IIR filter.
  // b0, b1, a1 :: filter coefficients
  // representing the following filter of the transfer function H(z):
  // H(z) = (b0 + (b1 * z^(-1))) / (1  + (a1 * z^(-1)))
  FirstOrderIirFilter(const double b0, const double b1, const double a1);
  // Default constructor
  FirstOrderIirFilter() : FirstOrderIirFilter(0, 0, 0) {}

  // Process a value
  double process(double input);

private:
  double m_b0, m_b1, m_a1;
  double m_x0, m_x1;
};

// First order low-pass IIR filter for real-valued signals.
class LowPassFilterRC {
public:
  //
  // Construct 1st order low-pass IIR filter.
  //
  // timeconst :: RC time constant in seconds (1 / (2 * PI * cutoff_freq))
  //
  LowPassFilterRC(const double timeconst);

  // Process samples.
  void process(const SampleVector &samples_in, SampleVector &samples_out);

  // Process samples in-place.
  void process_inplace(SampleVector &samples);

  // Process interleaved samples.
  void process_interleaved(const SampleVector &samples_in,
                           SampleVector &samples_out);

  // Process interleaved samples in-place.
  void process_interleaved_inplace(SampleVector &samples);

private:
  double m_timeconst;
  Sample m_a1;
  Sample m_b0;
  FirstOrderIirFilter m_filter0;
  FirstOrderIirFilter m_filter1;
};

// Generic biquad (2nd-order) Direct Form 2 IIR filter
class BiquadIirFilter {
public:
  //
  // Construct generic 2nd-order Direct Form 2 IIR filter.
  // b0, b1, b2, a1, a2:: filter coefficients
  // representing the following filter of the transfer function H(z):
  // H(z) = (b0 + (b1 * z^(-1)) + (b2 * z^(-2))) /
  //        (1  + (a1 * z^(-1)) + (a2 * z^(-2)))
  BiquadIirFilter(const double b0, const double b1, const double b2,
                  const double a1, const double a2);
  // Default constructor
  BiquadIirFilter() : BiquadIirFilter(0, 0, 0, 0, 0) {}

  // Process a value
  double process(double input);

protected:
  double m_b0, m_b1, m_b2, m_a1, m_a2;

private:
  double m_x0, m_x1, m_x2;
};

// High-pass filter for real-valued signals based on Butterworth IIR filter.
class HighPassFilterIir : public BiquadIirFilter {
public:
  //
  // Construct 2nd order high-pass IIR filter.
  //
  // cutoff   :: High-pass cutoff relative to the sample frequency
  //             (valid range 0.0 .. 0.5, 0.5 = Nyquist)
  //
  HighPassFilterIir(const double cutoff);

  // Process samples.
  void process(const SampleVector &samples_in, SampleVector &samples_out);

  // Process samples in-place.
  void process_inplace(SampleVector &samples);

private:
  BiquadIirFilter m_iirfilter;
};

#endif
