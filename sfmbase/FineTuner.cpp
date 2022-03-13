// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2019-2021 Kenji Rikitake, JJ1BDX
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

#include "FineTuner.h"

// class FineTuner

// Constructors.
FineTuner::FineTuner(unsigned const int table_size)
    : m_table_size(table_size), m_table(table_size) {
  set_freq_shift(0);
}
FineTuner::FineTuner(unsigned const int table_size, const int freq_shift)
    : m_table_size(table_size), m_table(table_size) {
  set_freq_shift(freq_shift);
}

// Set shift frequency table.
void FineTuner::set_freq_shift(const int freq_shift) {
  double phase_step = 2.0 * M_PI / double(m_table_size);
  for (unsigned int i = 0; i < m_table_size; i++) {
    double phi = (((int64_t)freq_shift * i) % m_table_size) * phase_step;
    double pcos = cos(phi);
    double psin = sin(phi);
    m_table[i] = IQSample(pcos, psin);
  }
  m_index = 0;
}

// Process samples.
void FineTuner::process(const IQSampleVector &samples_in,
                        IQSampleVector &samples_out) {
  unsigned int tblidx = m_index;
  unsigned int tblsiz = m_table.size();
  unsigned int n = samples_in.size();

  samples_out.resize(n);

  for (unsigned int i = 0; i < n; i++) {
    samples_out[i] = samples_in[i] * m_table[tblidx];
    tblidx++;
    if (tblidx == tblsiz) {
      tblidx = 0;
    }
  }

  m_index = tblidx;
}

// end
