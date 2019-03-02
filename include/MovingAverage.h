// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
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

#ifndef INCLUDE_MOVINGAVERAGE_H
#define INCLUDE_MOVINGAVERAGE_H

#include <stdint.h>
#include <vector>

template <class Type> class MovingAverage {
public:
  MovingAverage() : m_history(), m_sum(0), m_ptr(0) {}

  MovingAverage(int historySize, Type initial)
      : m_history(historySize, initial), m_sum((float)historySize * initial),
        m_ptr(0) {}

  void resize(int historySize, Type initial) {
    m_history.resize(historySize);
    for (size_t i = 0; i < m_history.size(); i++) {
      m_history[i] = initial;
    }
    m_sum = (float)m_history.size() * initial;
    m_ptr = 0;
  }

  void feed(Type value) {
    m_sum -= m_history[m_ptr];
    m_history[m_ptr] = value;
    m_sum += value;
    m_ptr++;
    if (m_ptr >= m_history.size()) {
      m_ptr = 0;
    }
  }

  void fill(Type value) {
    for (size_t i = 0; i < m_history.size(); i++) {
      m_history[i] = value;
    }
    m_sum = (float)m_history.size() * value;
  }

  Type average() const { return m_sum / (float)m_history.size(); }

  Type sum() const { return m_sum; }

protected:
  std::vector<Type> m_history;
  Type m_sum;
  unsigned int m_ptr;
};

#endif // INCLUDE_MOVINGAVERAGE_H
