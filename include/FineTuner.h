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

#ifndef INCLUDE_FINETUNER_H
#define INCLUDE_FINETUNER_H

#include "SoftFM.h"

// Fine tuner which shifts the frequency of an IQ signal by a fixed offset.
class FineTuner {
public:
  // Construct fine tuner. table_size: size of the internal sin/cos tables (sets
  // the shift resolution); freq_shift: the signal is shifted by sample_rate *
  // freq_shift / table_size.
  FineTuner(unsigned const int table_size, const int freq_shift);
  FineTuner(unsigned const int table_size) : FineTuner(table_size, 0) {}

  // Initialize freq_shift table, keeping phase continuity with the ongoing
  // table (phase != 0). freq_shift: the signal is shifted by sample_rate *
  // freq_shift / table_size.
  void set_freq_shift(const int freq_shift);

  // Process samples.
  void process(const IQSampleVector &samples_in, IQSampleVector &samples_out);

private:
  unsigned int m_index;
  unsigned const int m_table_size;
  IQSampleVector m_table;
  SampleVector m_phase_table;
};

#endif
