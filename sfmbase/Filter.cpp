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

#include <algorithm>

#include "Filter.h"

// class LowPassFilterFirIQ

// Construct low-pass filter.
LowPassFilterFirIQ::LowPassFilterFirIQ(const IQSampleCoeff &coeff,
                                       const unsigned int downsample)
    : m_coeff(coeff), m_order(coeff.size() - 1), m_downsample(downsample),
      m_pos(0) {
  assert(downsample >= 1);
  m_state.resize(m_order);
}

// Process samples.
void LowPassFilterFirIQ::process(const IQSampleVector &samples_in,
                                 IQSampleVector &samples_out) {
  unsigned int order = m_state.size();
  unsigned int n = samples_in.size();

  // Integer downsample factor, no linear interpolation.

  unsigned int p = m_pos;
  unsigned int pstep = m_downsample;

  samples_out.resize((n - p + pstep - 1) / pstep);

  if (n == 0) {
    return;
  }

  // The first few samples need data from m_state.
  // NOTE: this assumes the filter has symmetric coefficient pairs
  unsigned int i = 0;
  for (; p < n && p < order; p += pstep, i++) {
    IQSample y = 0;
    for (unsigned int j = p + 1; j <= order; j++) {
      y += m_state[order + p - j] * m_coeff[j];
    }
    for (unsigned int j = 1; j <= p; j++) {
      y += samples_in[p - j] * m_coeff[j];
    }
    samples_out[i] = y;
  }

  // Remaining samples only need data from samples_in.
  // NOTE: this assumes the filter has symmetric coefficient pairs
  unsigned int half_order = (order - 1) / 2;
  for (; p < n; p += pstep, i++) {
    IQSample y = 0;
    for (unsigned int k = 0; k <= half_order; k++) {
      y += (samples_in[p - k] + samples_in[p - (order - k)]) * m_coeff[k];
    }
    if ((order % 2) == 0) {
      y += samples_in[p - (order / 2)] * m_coeff[(order / 2)];
    }
    samples_out[i] = y;
  }

  assert(i == samples_out.size());

  // Update index of start position in text sample block.
  m_pos = p - n;

  // Update m_state.
  if (n < order) {
    std::copy(m_state.begin() + n, m_state.end(), m_state.begin());
    std::copy(samples_in.begin(), samples_in.end(), m_state.end() - n);
  } else {
    std::copy(samples_in.end() - order, samples_in.end(), m_state.begin());
  }
}

// Class LowPassFilterFirAudio

// Construct low-pass filter.
LowPassFilterFirAudio::LowPassFilterFirAudio(const SampleCoeff &coeff)
    : m_coeff(coeff), m_order(coeff.size() - 1), m_pos(0) {
  m_state.resize(m_order);
}

// Process samples.
void LowPassFilterFirAudio::process(const SampleVector &samples_in,
                                    SampleVector &samples_out) {
  unsigned int order = m_state.size();
  unsigned int n = samples_in.size();
  unsigned int p = m_pos;
  samples_out.resize(n - p);

  if (n == 0) {
    return;
  }

  // The first few samples need data from m_state.
  // NOTE: this assumes the filter has symmetric coefficient pairs
  unsigned int i = 0;
  for (; p < n && p < order; p++, i++) {
    Sample y = 0;
    for (unsigned int j = p + 1; j <= order; j++) {
      y += m_state[order + p - j] * m_coeff[j];
    }
    for (unsigned int j = 1; j <= p; j++) {
      y += samples_in[p - j] * m_coeff[j];
    }
    samples_out[i] = y;
  }

  // Remaining samples only need data from samples_in.
  // NOTE: this assumes the filter has symmetric coefficient pairs
  unsigned int half_order = (order - 1) / 2;
  for (; p < n; p++, i++) {
    Sample y = 0;
    for (unsigned int k = 0; k <= half_order; k++) {
      y += (samples_in[p - k] + samples_in[p - (order - k)]) * m_coeff[k];
    }
    if ((order % 2) == 0) {
      y += samples_in[p - (order / 2)] * m_coeff[(order / 2)];
    }
    samples_out[i] = y;
  }

  assert(i == samples_out.size());

  // Update index of start position in text sample block.
  m_pos = p - n;

  // Update m_state.
  if (n < order) {
    std::copy(m_state.begin() + n, m_state.end(), m_state.begin());
    std::copy(samples_in.begin(), samples_in.end(), m_state.end() - n);
  } else {
    std::copy(samples_in.end() - order, samples_in.end(), m_state.begin());
  }
}

// Class FirstOrderIirFilter
// Construct generic 1st-order Direct Form 2 IIR filter
FirstOrderIirFilter::FirstOrderIirFilter(const double b0, const double b1,
                                         const double a1)
    : m_b0(b0), m_b1(b1), m_a1(a1), m_x1(0) {}

// Process a value.
double FirstOrderIirFilter::process(double input) {
  m_x0 = input;
  m_x0 -= m_a1 * m_x1;
  double y = m_b0 * m_x0 + m_b1 * m_x1;
  m_x1 = m_x0;
  return y;
}

// Class LowPassFilterRC
// Construct 1st order low-pass IIR filter.
// Continuous domain:
//   H(s) = 1 / (1 - s * timeconst)
// Discrete domain:
//   H(z) = (1 - exp(-1/timeconst)) / (1 - exp(-1/timeconst) / z)
LowPassFilterRC::LowPassFilterRC(const double timeconst)
    : m_timeconst(timeconst), m_a1(-std::exp(-1 / m_timeconst)), m_b0(1 + m_a1),
      m_filter0(m_b0, 0, m_a1), m_filter1(m_b0, 0, m_a1) {}

// Process samples.
void LowPassFilterRC::process(const SampleVector &samples_in,
                              SampleVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    samples_out[i] = m_filter0.process(samples_in[i]);
  }
}

// Process interleaved samples.
void LowPassFilterRC::process_interleaved(const SampleVector &samples_in,
                                          SampleVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  for (unsigned int i = 0; i < n - 1; i += 2) {
    samples_out[i] = m_filter0.process(samples_in[i]);
    samples_out[i + 1] = m_filter1.process(samples_in[i + 1]);
  }
}

// Process samples in-place.
void LowPassFilterRC::process_inplace(SampleVector &samples) {
  unsigned int n = samples.size();

  for (unsigned int i = 0; i < n; i++) {
    Sample x = samples[i];
    samples[i] = m_filter0.process(x);
  }
}

// Process interleaved samples in-place.
void LowPassFilterRC::process_interleaved_inplace(SampleVector &samples) {
  unsigned int n = samples.size();

  for (unsigned int i = 0; i < n - 1; i += 2) {
    Sample x0 = samples[i];
    Sample x1 = samples[i + 1];
    samples[i] = m_filter0.process(x0);
    samples[i + 1] = m_filter1.process(x1);
  }
}

// Class BiquadIirFilter
// Construct generic 2nd-order Direct Form 2 IIR filter.
BiquadIirFilter::BiquadIirFilter(const double b0, const double b1,
                                 const double b2, const double a1,
                                 const double a2)
    : m_b0(b0), m_b1(b1), m_b2(b2), m_a1(a1), m_a2(a2), m_x1(0), m_x2(0) {}

// Process a value.
double BiquadIirFilter::process(double input) {
  m_x0 = input;
  m_x0 -= m_a1 * m_x1 + m_a2 * m_x2;
  double y = m_b0 * m_x0 + m_b1 * m_x1 + m_b2 * m_x2;
  m_x2 = m_x1;
  m_x1 = m_x0;
  return y;
}

// Class HighPassFilterIir
// Construct 2nd order high-pass IIR filter.
HighPassFilterIir::HighPassFilterIir(const double cutoff) {

  typedef std::complex<double> CDbl;

  // Angular cutoff frequency.
  double w = 2 * M_PI * cutoff;

  // Poles 1 and 2 are a conjugate pair.
  // Continuous-domain:
  //   p_k = w / exp( (2*k + n - 1) / (2*n) * pi * j)
  CDbl p1s = w / std::exp((2 * 1 + 2 - 1) / double(2 * 2) * CDbl(0, M_PI));

  // Map poles to discrete-domain via matched Z transform.
  CDbl p1z = std::exp(p1s);

  // Both zeros are located in s = 0, z = 1.

  // Discrete-domain transfer function:
  //   H(z) = g * (1 - 1/z) * (1 - 1/z) / ( (1 - p1/z) * (1 - p2/z) )
  //        = g * (1 - 2/z + 1/z**2) / (1 - (p1+p2)/z + (p1*p2)/z**2)
  //
  // Note that z2 = conj(z1).
  // Therefore p1+p2 == 2*real(p1), p1*2 == abs(p1*p1), z4 = conj(z1)

  // Modify the values of parent BiquadIirFilter private variables
  m_b0 = 1;
  m_b1 = -2;
  m_b2 = 1;
  m_a1 = -2 * std::real(p1z);
  m_a2 = std::abs(p1z * p1z);

  // Adjust b coefficients to get unit gain at Nyquist frequency (z=-1).
  double g = (m_b0 - m_b1 + m_b2) / (1 - m_a1 + m_a2);
  m_b0 /= g;
  m_b1 /= g;
  m_b2 /= g;
}

// Process samples.
void HighPassFilterIir::process(const SampleVector &samples_in,
                                SampleVector &samples_out) {
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    samples_out[i] = BiquadIirFilter::process(samples_in[i]);
  }
}

// Process samples in-place.
void HighPassFilterIir::process_inplace(SampleVector &samples) {
  unsigned int n = samples.size();

  for (unsigned int i = 0; i < n; i++) {
    Sample y = BiquadIirFilter::process(samples[i]);
    samples[i] = y;
  }
}

/* end */
