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

// For the function fast_atan2f() from GNU Radio
// the following copyright statement applies:
// Copyright 2005,2013 Free Software Foundation, Inc.
//
// This file is part of GNU Radio
//
// GNU Radio is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// GNU Radio is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with GNU Radio; see the file COPYING.  If not, write to
// the Free Software Foundation, Inc., 51 Franklin Street,
// Boston, MA 02110-1301, USA.

#ifndef INCLUDE_UTILITY_H
#define INCLUDE_UTILITY_H

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <limits.h>
#include <sys/time.h>

#include "SoftFM.h"

// namespace Utility.
// All functions and data MUST be inline.

namespace Utility {

// Parse floating numbers with unit suffixes.
inline bool parse_dbl(const char *s, double &v) {
  char *endp;

  errno = 0;
  v = strtod(s, &endp);

  if (endp == s) {
    return false;
  }

  // Reject overflow (HUGE_VAL / -HUGE_VAL with errno==ERANGE),
  // NaN, and +/-Inf so callers can rely on a finite result.
  if (errno == ERANGE || !std::isfinite(v)) {
    return false;
  }

  if (*endp == 'k') {
    v *= 1.0e3;
    endp++;
  } else if (*endp == 'M') {
    v *= 1.0e6;
    endp++;
  } else if (*endp == 'G') {
    v *= 1.0e9;
    endp++;
  }

  if (!std::isfinite(v)) {
    return false;
  }

  return (*endp == '\0');
}

// Parse integet numbers with "k" suffix.
inline bool parse_int(const char *s, int &v, bool allow_unit = false) {
  char *endp;
  errno = 0;
  long t = strtol(s, &endp, 10);
  if (endp == s) {
    return false;
  }
  if (errno == ERANGE) {
    return false;
  }
  if (allow_unit && *endp == 'k' && t > INT_MIN / 1000 && t < INT_MAX / 1000) {
    t *= 1000;
    endp++;
  }
  if (*endp != '\0' || t < INT_MIN || t > INT_MAX) {
    return false;
  }
  v = t;
  return true;
}

// Compute RMS over the specified IQSample vector.
inline float rms_level_sample(const IQSampleVector &samples) {
  unsigned int n = samples.size();
  if (n == 0) {
    return 0.0f;
  }
  volk::vector<float> magnitude_sq;
  magnitude_sq.resize(n);

  IQSample::value_type level = 0;

  volk_32fc_magnitude_squared_32f(magnitude_sq.data(), samples.data(), n);
  volk_32f_accumulator_s32f(&level, magnitude_sq.data(), n);
  // Return RMS level
  return std::sqrt(level / n);
}

// Compute mean value and RMS over the specified Sample vector.
inline void samples_mean_rms(const IQSampleDecodedVector &samples, float &mean,
                             float &rms) {
  float vsum = 0;
  float vsumsq = 0;
  unsigned int n = samples.size();

  if (n == 0) {
    mean = 0.0f;
    rms = 0.0f;
    return;
  }

  volk_32f_accumulator_s32f(&vsum, samples.data(), n);
  volk_32f_x2_dot_prod_32f(&vsumsq, samples.data(), samples.data(), n);

  mean = vsum / n;
  rms = std::sqrt(vsumsq / n);
}

// Simple linear gain adjustment.
inline void adjust_gain(SampleVector &samples, double gain) {
  for (unsigned int i = 0, n = samples.size(); i < n; i++) {
    double amplitude = samples[i] * gain;
    samples[i] = amplitude;
  }
}

// Sleep in milliseconds.
inline void millisleep(unsigned int milliseconds) {
  struct timespec ts_sleep = {
      .tv_sec = static_cast<time_t>(milliseconds / 1000),
      .tv_nsec = static_cast<long>((milliseconds % 1000) * 1000000L)};
  nanosleep(&ts_sleep, NULL);
}

// Return Unix time stamp in seconds.
inline double get_time() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec + (1.0e-6 * tv.tv_usec);
}

// Remove NaNs and infinities from a SampleVector
// by substituting them to zeros.
// NOTE: this function is for post-processing
// PhaseDiscriminator::Process(), specifically
// the output of volk_32fc_s32f_atan2_32f()
// so that no NaNs are contained in
// the returning vector elements.
inline void remove_nans(IQSampleDecodedVector &samples) {
  for (size_t i = 0; i < samples.size(); i++) {
    double v = samples[i];
    if (std::isnan(v)) {
      samples[i] = 0;
    }
  }
}

}; // namespace Utility

#endif // INCLUDE_UTILITY_H_
