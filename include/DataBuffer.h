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

#ifndef INCLUDE_DATABUFFER_H
#define INCLUDE_DATABUFFER_H

#include "Utility.h"
#include "readerwriterqueue.h"
#include <atomic>

// Buffer to move sample data between threads.

template <class Element> class DataBuffer {
public:
  // Constructor.
  DataBuffer() : m_rwqueue(128), m_end_marked(false) {}

  // Add samples to the queue.
  void push(std::vector<Element> &&samples) {
    if (!samples.empty()) {
      m_rwqueue.enqueue(std::move(samples));
    }
  }

  // Mark the end of the data stream.
  void push_end() { m_end_marked.store(true); }

  // Return size of std::queue structure (for debugging).
  std::size_t queue_size() { return (m_rwqueue.size_approx()); }

  // If the queue is non-empty, remove a block from the queue and
  // return the samples. If the end marker has been reached, return
  // an empty vector. If the queue is empty, wait until more data is pushed
  // or until the end marker is pushed.
  std::vector<Element> pull() {
    std::vector<Element> ret;
    std::vector<Element> *p;
    {
      if (!m_end_marked.load()) {
        while (nullptr == (p = m_rwqueue.peek())) {
          Utility::millisleep(1);
        }
        std::swap(ret, *p);
        m_rwqueue.pop();
      }
      return ret;
    }
  }

  // Return true if the end has been reached at the Pull side.
  bool pull_end_reached() {
    return ((m_rwqueue.peek() == nullptr) && (m_end_marked.load()));
  }

private:
  moodycamel::ReaderWriterQueue<std::vector<Element>> m_rwqueue;
  std::atomic_bool m_end_marked;
};

#endif
