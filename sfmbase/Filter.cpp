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

/** Prepare Lanczos FIR filter coefficients. */
template <class T>
static void make_lanczos_coeff(unsigned int filter_order, double cutoff,
                               std::vector<T> &coeff) {
  coeff.resize(filter_order + 1);

  // Prepare Lanczos FIR filter.
  //   t[i]     =  (i - order/2)
  //   coeff[i] =  Sinc(2 * cutoff * t[i]) * Sinc(t[i] / (order/2 + 1))
  //   coeff    /= sum(coeff)

  double ysum = 0.0;

  // Calculate filter kernel.
  for (int i = 0; i <= (int)filter_order; i++) {
    int t2 = 2 * i - filter_order;

    double y;
    if (t2 == 0) {
      y = 1.0;
    } else {
      double x1 = cutoff * t2;
      double x2 = t2 / double(filter_order + 2);
      y = (sin(M_PI * x1) / M_PI / x1) * (sin(M_PI * x2) / M_PI / x2);
    }

    coeff[i] = y;
    ysum += y;
  }

  // Apply correction factor to ensure unit gain at DC.
  for (unsigned i = 0; i <= filter_order; i++) {
    coeff[i] /= ysum;
  }
}

/* ****************  class LowPassFilterFirIQ  **************** */

// Construct low-pass filter.
LowPassFilterFirIQ::LowPassFilterFirIQ(
    const std::vector<IQSample::value_type> &coeff, unsigned int downsample)
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
    copy(m_state.begin() + n, m_state.end(), m_state.begin());
    copy(samples_in.begin(), samples_in.end(), m_state.end() - n);
  } else {
    copy(samples_in.end() - order, samples_in.end(), m_state.begin());
  }
}

/* ****************  class DownsampleFilter  **************** */

// Construct low-pass filter with optional downsampling.
DownsampleFilter::DownsampleFilter(
    const std::vector<SampleVector::value_type> &coeff, double downsample,
    bool integer_factor)
    : m_coeff(coeff), m_order(coeff.size() - 1), m_downsample(downsample),
      m_downsample_int(integer_factor ? lrint(downsample) : 0), m_pos_int(0),
      m_pos_frac(0) {
  // Force the first coefficient to zero and append an extra zero at the
  // end of the array. This ensures we can always obtain (m_order+1)
  // coefficients by linear interpolation between adjacent array elements.
  m_coeff.insert(m_coeff.begin(), 0);
  m_coeff.push_back(0);
  m_order = coeff.size();

  assert(downsample >= 1);
  assert(m_order > 2);
  m_state.resize(m_order);
}

// Process samples.
void DownsampleFilter::process(const SampleVector &samples_in,
                               SampleVector &samples_out) {
  unsigned int order = m_state.size();
  unsigned int n = samples_in.size();

  if (m_downsample_int != 0) {

    // Integer downsample factor, no linear interpolation.
    // This is relatively simple.

    unsigned int p = m_pos_int;
    unsigned int pstep = m_downsample_int;

    samples_out.resize((n - p + pstep - 1) / pstep);

    // The first few samples need data from m_state.
    // NOTE: this assumes the filter has symmetric coefficient pairs
    unsigned int i = 0;
    for (; p < n && p < order; p += pstep, i++) {
      Sample y = 0;
      for (unsigned int j = 1; j <= p; j++) {
        y += samples_in[p - j] * m_coeff[j];
      }
      for (unsigned int j = p + 1; j <= order; j++) {
        y += m_state[order + p - j] * m_coeff[j];
      }
      samples_out[i] = y;
    }

    // Remaining samples only need data from samples_in.
    // NOTE: this assumes the filter has symmetric coefficient pairs
    unsigned int half_order = order / 2;
    for (; p < n; p += pstep, i++) {
      Sample y = 0;
      for (unsigned int k = 1; k <= half_order; k++) {
        y += (samples_in[p - k] + samples_in[p - ((order + 1) - k)]) *
             m_coeff[k];
      }
      if ((order % 2) > 0) {
        y += samples_in[p - ((order + 1) / 2)] * m_coeff[((order + 1) / 2)];
      }
      samples_out[i] = y;
    }
    assert(i == samples_out.size());

    // Update index of start position in text sample block.
    m_pos_int = p - n;

  } else {

    // Fractional downsample factor via linear interpolation of
    // the FIR coefficient table. This is a real headache.

    // Estimate number of output samples we can produce in this run.
    Sample p = m_pos_frac;
    Sample pstep = m_downsample;
    unsigned int n_out = int(2 + n / pstep);

    samples_out.resize(n_out);

    // Produce output samples.
    unsigned int i = 0;
    Sample pf = p;
    unsigned int pi = int(pf);
    while (pi < n) {
      Sample k1 = pf - pi;
      Sample k0 = 1 - k1;

      Sample y = 0;
      for (unsigned int j = 0; j <= order; j++) {
        Sample k = m_coeff[j] * k0 + m_coeff[j + 1] * k1;
        Sample s = (j <= pi) ? samples_in[pi - j] : m_state[order + pi - j];
        y += k * s;
      }
      samples_out[i] = y;

      i++;
      pf = p + i * pstep;
      pi = int(pf);
    }

    // We may overestimate the number of samples by 1 or 2.
    assert(i <= n_out && i + 2 >= n_out);
    samples_out.resize(i);

    // Update fractional index of start position in text sample block.
    // Limit to 0 to avoid catastrophic results of rounding errors.
    m_pos_frac = pf - n;
    if (m_pos_frac < 0) {
      m_pos_frac = 0;
    }
  }

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

/* ****************  class LowPassFilterIir  **************** */

// Construct 4th order low-pass IIR filter.
LowPassFilterIir::LowPassFilterIir(double cutoff) : y1(0), y2(0), y3(0), y4(0) {
  typedef std::complex<double> CDbl;

  // Angular cutoff frequency.
  double w = 2 * M_PI * cutoff;

  // Poles 1 and 4 are a conjugate pair, and poles 2 and 3 are another pair.
  // Continuous domain:
  //   p_k = w * exp( (2*k + n - 1) / (2*n) * pi * j)
  CDbl p1s = w * exp((2 * 1 + 4 - 1) / double(2 * 4) * CDbl(0, M_PI));
  CDbl p2s = w * exp((2 * 2 + 4 - 1) / double(2 * 4) * CDbl(0, M_PI));

  // Map poles to discrete-domain via matched Z transform.
  CDbl p1z = exp(p1s);
  CDbl p2z = exp(p2s);

  // Discrete-domain transfer function:
  //   H(z) = b0 / ( (1 - p1/z) * (1 - p4/z) * (1 - p2/z) * (1 - p3/z) )
  //        = b0 / ( (1 - (p1+p4)/z + p1*p4/z**2) *
  //                 (1 - (p2+p3)/z + p2*p3/z**2) )
  //        = b0 / (1 - (p1 + p4 + p2 + p3)/z
  //                  + (p1*p4 + p2*p3 + (p1+p4)*(p2+p3))/z**2
  //                  - ((p1+p4)*p2*p3 + (p2+p3)*p1*p4)/z**3
  //                  + p1*p4*p2*p3/z**4
  //
  // Note that p3 = conj(p2), p4 = conj(p1)
  // Therefore p1+p4 == 2*real(p1), p1*p4 == abs(p1*p1)
  //
  a1 = -(2 * real(p1z) + 2 * real(p2z));
  a2 = (abs(p1z * p1z) + abs(p2z * p2z) + 2 * real(p1z) * 2 * real(p2z));
  a3 = -(2 * real(p1z) * abs(p2z * p2z) + 2 * real(p2z) * abs(p1z * p1z));
  a4 = abs(p1z * p1z) * abs(p2z * p2z);

  // Choose b0 to get unit DC gain.
  b0 = 1 + a1 + a2 + a3 + a4;
}

// Process samples.
void LowPassFilterIir::process(const SampleVector &samples_in,
                               SampleVector &samples_out) {
  unsigned int n = samples_in.size();

  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    Sample x = samples_in[i];
    Sample y = b0 * x - a1 * y1 - a2 * y2 - a3 * y3 - a4 * y4;
    y4 = y3;
    y3 = y2;
    y2 = y1;
    y1 = y;
    samples_out[i] = y;
  }
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
