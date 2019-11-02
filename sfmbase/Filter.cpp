// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>

#include "Filter.h"

/* ****************  class LowPassFilterFirIQ  **************** */

// Construct low-pass filter.
LowPassFilterFirIQ::LowPassFilterFirIQ(const IQSampleCoeff &coeff,
                                       unsigned int downsample)
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

    // for (unsigned int k = 0; k <= half_order; k++) {
    //   y += (samples_in[p - k] + samples_in[p - (order - k)]) * m_coeff[k];
    // }

    //  The following addition is split into two parts:
    //  y += (samples_in[p - k] + samples_in[p - (order - k)]) * m_coeff[k];

    //  // index: p - 0 -> p - half_order (decreasing)
    //  y += samples_in[p - k] * m_coeff[k];

    //  // index: (p - order) -> (p - order) + half_order (increasing)
    //  y += samples_in[p - (order - k)] * m_coeff[k];

    volk_32fc_32f_dot_prod_32fc(&y, &samples_in[p - half_order], m_coeff.data(),
                                half_order + 1);
    volk_32fc_32f_dot_prod_32fc(&y, &samples_in[p - order], m_coeff.data(),
                                half_order + 1);

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
    copy(m_state.begin() + n, m_state.end(), m_state.begin());
    copy(samples_in.begin(), samples_in.end(), m_state.end() - n);
  } else {
    copy(samples_in.end() - order, samples_in.end(), m_state.begin());
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
    copy(m_state.begin() + n, m_state.end(), m_state.begin());
    copy(samples_in.begin(), samples_in.end(), m_state.end() - n);
  } else {
    copy(samples_in.end() - order, samples_in.end(), m_state.begin());
  }
}

/* ****************  class LowPassFilterRC  **************** */

// Construct 1st order low-pass IIR filter.
LowPassFilterRC::LowPassFilterRC(double timeconst)
    : m_timeconst(timeconst), m_y0_1(0), m_y1_1(0) {
  m_a1 = -exp(-1 / m_timeconst);
  ;
  m_b0 = 1 + m_a1;
}

// Process samples.
void LowPassFilterRC::process(const SampleVector &samples_in,
                              SampleVector &samples_out) {
  /*
   * Continuous domain:
   *   H(s) = 1 / (1 - s * timeconst)
   *
   * Discrete domain:
   *   H(z) = (1 - exp(-1/timeconst)) / (1 - exp(-1/timeconst) / z)
   */
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  Sample y = m_y0_1;

  for (unsigned int i = 0; i < n; i++) {
    Sample x = samples_in[i];
    y = m_b0 * x - m_a1 * y;
    samples_out[i] = y;
  }

  m_y0_1 = y;
}

// Process interleaved samples.
void LowPassFilterRC::process_interleaved(const SampleVector &samples_in,
                                          SampleVector &samples_out) {
  /*
   * Continuous domain:
   *   H(s) = 1 / (1 - s * timeconst)
   *
   * Discrete domain:
   *   H(z) = (1 - exp(-1/timeconst)) / (1 - exp(-1/timeconst) / z)
   */
  unsigned int n = samples_in.size();
  samples_out.resize(n);

  Sample y0 = m_y0_1;
  Sample y1 = m_y1_1;

  for (unsigned int i = 0; i < n - 1; i += 2) {
    Sample x0 = samples_in[i];
    y0 = m_b0 * x0 - m_a1 * y0;
    samples_out[i] = y0;

    Sample x1 = samples_in[i + 1];
    y1 = m_b0 * x1 - m_a1 * y1;
    samples_out[i + 1] = y1;
  }

  m_y0_1 = y0;
  m_y1_1 = y1;
}

// Process samples in-place.
void LowPassFilterRC::process_inplace(SampleVector &samples) {
  unsigned int n = samples.size();

  Sample y = m_y0_1;

  for (unsigned int i = 0; i < n; i++) {
    Sample x = samples[i];
    y = m_b0 * x - m_a1 * y;
    samples[i] = y;
  }

  m_y0_1 = y;
}

// Process interleaved samples in-place.
void LowPassFilterRC::process_interleaved_inplace(SampleVector &samples) {
  unsigned int n = samples.size();

  Sample y0 = m_y0_1;
  Sample y1 = m_y1_1;

  for (unsigned int i = 0; i < n - 1; i += 2) {
    Sample x0 = samples[i];
    y0 = m_b0 * x0 - m_a1 * y0;
    samples[i] = y0;

    Sample x1 = samples[i + 1];
    y1 = m_b0 * x1 - m_a1 * y1;
    samples[i + 1] = y1;
  }

  m_y0_1 = y0;
  m_y1_1 = y1;
}

/* ****************  class HighPassFilterIir  **************** */

// Construct 2nd order high-pass IIR filter.
HighPassFilterIir::HighPassFilterIir(double cutoff)
    : x1(0), x2(0), y1(0), y2(0) {
  typedef std::complex<double> CDbl;

  // Angular cutoff frequency.
  double w = 2 * M_PI * cutoff;

  // Poles 1 and 2 are a conjugate pair.
  // Continuous-domain:
  //   p_k = w / exp( (2*k + n - 1) / (2*n) * pi * j)
  CDbl p1s = w / exp((2 * 1 + 2 - 1) / double(2 * 2) * CDbl(0, M_PI));

  // Map poles to discrete-domain via matched Z transform.
  CDbl p1z = exp(p1s);

  // Both zeros are located in s = 0, z = 1.

  // Discrete-domain transfer function:
  //   H(z) = g * (1 - 1/z) * (1 - 1/z) / ( (1 - p1/z) * (1 - p2/z) )
  //        = g * (1 - 2/z + 1/z**2) / (1 - (p1+p2)/z + (p1*p2)/z**2)
  //
  // Note that z2 = conj(z1).
  // Therefore p1+p2 == 2*real(p1), p1*2 == abs(p1*p1), z4 = conj(z1)
  //
  b0 = 1;
  b1 = -2;
  b2 = 1;
  a1 = -2 * real(p1z);
  a2 = abs(p1z * p1z);

  // Adjust b coefficients to get unit gain at Nyquist frequency (z=-1).
  double g = (b0 - b1 + b2) / (1 - a1 + a2);
  b0 /= g;
  b1 /= g;
  b2 /= g;
}

// Process samples.
void HighPassFilterIir::process(const SampleVector &samples_in,
                                SampleVector &samples_out) {
  unsigned int n = samples_in.size();

  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    Sample x = samples_in[i];
    Sample y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    samples_out[i] = y;
  }
}

// Process samples in-place.
void HighPassFilterIir::process_inplace(SampleVector &samples) {
  unsigned int n = samples.size();

  for (unsigned int i = 0; i < n; i++) {
    Sample x = samples[i];
    Sample y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    samples[i] = y;
  }
}

/* end */
