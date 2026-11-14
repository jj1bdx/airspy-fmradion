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

#ifndef INCLUDE_DATABUFFER_H
#define INCLUDE_DATABUFFER_H

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fmt/format.h>
#include <mutex>
#include <queue>
#include <vector>

// Timing statistics of a DataBuffer (all times in nanoseconds).
struct DataBufferStats {
  /// Wake-latency thresholds in ms for wake_over[].
  static constexpr std::array<unsigned int, 5> wake_thresholds_ms{1, 2, 5, 10,
                                                                  20};
  /// Histogram bucket 0 is below 1 us; bucket b is [2^(b-1), 2^b) us; the last
  /// bucket takes everything above.
  static constexpr std::size_t hist_buckets = 16;

  // Blocks pushed while the consumer was blocked waiting: push to pull return.
  std::uint64_t wake_count = 0;
  std::uint64_t wake_max_ns = 0;
  std::array<std::uint64_t, wake_thresholds_ms.size()> wake_over{};
  std::array<std::uint64_t, hist_buckets> wake_hist{};
  // Blocks pulled without waiting (consumer busy): push to pull return.
  std::uint64_t backlog_count = 0;
  std::uint64_t backlog_max_ns = 0;
  // Time between consecutive pushes, and counts above 1.5x and 3x the nominal
  // block period (zero until the period is set).
  std::uint64_t gap_max_ns = 0;
  std::uint64_t gap_over_1_5 = 0;
  std::uint64_t gap_over_3 = 0;
};

// Buffer to move sample data between threads.

template <class Element> class DataBuffer {
public:
  // Queue bound: beyond this many blocks the oldest is dropped. Reached only
  // when the consumer cannot keep up; worst case 512 MiB (Airspy R2: 65536
  // samples * 8 bytes * 1024 blocks). See doc/FIXES_CLAUDE_20260610.md (X7).
  static constexpr std::size_t max_queue_blocks = 1024;

  // Constructor.
  DataBuffer() : m_end_marked(false) {}

  // Set the nominal block period for the push-gap counters; 0 disables them.
  inline void set_nominal_period_ns(std::uint64_t period_ns) {
    m_period_ns.store(period_ns, std::memory_order_relaxed);
  }

  // Add samples to the queue; if it is full, drop the oldest block and warn, so
  // memory stays bounded when the consumer lags a real-time source. The warning
  // is printed only at the first drop of each overflow episode (a run of
  // consecutive pushes that each drop a block), to avoid flooding stderr from
  // the source callback thread. It shows the running total at that time.
  inline void push(std::vector<Element> &&samples) {
    if (!samples.empty()) {
      std::uint64_t dropped = 0;
      const std::int64_t now_ns = clock_ns();
      {
        std::scoped_lock<std::mutex> lock(m_mutex);
        record_push_gap(now_ns);
        // The consumer waits only on an empty queue, so it waits for this one.
        const bool consumer_waiting = m_waiting && m_queue.empty();
        m_queue.push(Entry{std::move(samples), now_ns, consumer_waiting});
        const bool episode_continues = m_overflowing;
        m_overflowing = false;
        while (m_queue.size() > max_queue_blocks) {
          m_queue.pop();
          m_dropped_blocks++;
          m_overflowing = true;
          if (!episode_continues) {
            dropped = m_dropped_blocks;
          }
        }
        m_max_queue_size = std::max(m_max_queue_size, m_queue.size());
        // unlock m_mutex here by getting out of scope
      }
      if (dropped > 0) {
        fmt::println(stderr, "DataBuffer: queue overflow, dropped blocks = {}",
                     dropped);
      }
      m_cond.notify_all();
    }
  }

  // Return the total number of blocks dropped due to queue overflow.
  inline std::uint64_t dropped_blocks() const {
    {
      std::scoped_lock<std::mutex> lock(m_mutex);
      return m_dropped_blocks;
      // unlock m_mutex here by getting out of scope
    }
  }

  // Return the largest queue size seen (high-water mark).
  inline std::size_t max_queue_size() const {
    std::scoped_lock<std::mutex> lock(m_mutex);
    return m_max_queue_size;
  }

  // Mark the end of the data stream.
  inline void push_end() {
    {
      std::scoped_lock<std::mutex> lock(m_mutex);
      m_end_marked = true;
      // unlock m_mutex here by getting out of scope
    }
    m_cond.notify_all();
  }

  // Return size of std::queue structure (for debugging).
  inline std::size_t queue_size() const {
    {
      std::scoped_lock<std::mutex> lock(m_mutex);
      return (m_queue.size());
      // unlock m_mutex here by getting out of scope
    }
  }

  // Return a copy of the timing statistics.
  inline DataBufferStats stats() const {
    std::scoped_lock<std::mutex> lock(m_mutex);
    return m_stats;
  }

  // Remove and return one block, waiting while the queue is empty; return an
  // empty vector once the end marker has been reached.
  inline std::vector<Element> pull() {
    std::vector<Element> ret;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      if (m_queue.empty() && !m_end_marked) {
        m_waiting = true;
        m_cond.wait(lock,
                    [&] { return !(m_queue.empty() && (!m_end_marked)); });
        m_waiting = false;
      }
      if (!m_queue.empty()) {
        record_pull(m_queue.front(), clock_ns());
        std::swap(ret, m_queue.front().samples);
        m_queue.pop();
      }
      return (ret);
      // unlock m_mutex here by getting out of scope
    }
  }

  // Return true if the end has been reached at the Pull side.
  inline bool pull_end_reached() const {
    {
      std::scoped_lock<std::mutex> lock(m_mutex);
      return (m_queue.empty() && (m_end_marked));
      // unlock m_mutex here by getting out of scope
    }
  }

private:
  struct Entry {
    std::vector<Element> samples;
    std::int64_t push_ns;
    bool consumer_waiting; // the consumer was blocked waiting for this block
  };

  static std::int64_t clock_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  // Both called with m_mutex held.
  void record_push_gap(std::int64_t now_ns) {
    if (m_have_last_push) {
      const auto gap = static_cast<std::uint64_t>(
          std::max<std::int64_t>(now_ns - m_last_push_ns, 0));
      m_stats.gap_max_ns = std::max(m_stats.gap_max_ns, gap);
      const std::uint64_t period = m_period_ns.load(std::memory_order_relaxed);
      if (period > 0) {
        m_stats.gap_over_1_5 += (gap * 2 > period * 3);
        m_stats.gap_over_3 += (gap > period * 3);
      }
    }
    m_have_last_push = true;
    m_last_push_ns = now_ns;
  }

  void record_pull(const Entry &entry, std::int64_t now_ns) {
    const auto age = static_cast<std::uint64_t>(
        std::max<std::int64_t>(now_ns - entry.push_ns, 0));
    if (!entry.consumer_waiting) {
      m_stats.backlog_count++;
      m_stats.backlog_max_ns = std::max(m_stats.backlog_max_ns, age);
      return;
    }
    m_stats.wake_count++;
    m_stats.wake_max_ns = std::max(m_stats.wake_max_ns, age);
    for (std::size_t i = 0; i < DataBufferStats::wake_thresholds_ms.size();
         i++) {
      m_stats.wake_over[i] +=
          (age > DataBufferStats::wake_thresholds_ms[i] * 1000000ULL);
    }
    const std::size_t bucket = std::min<std::size_t>(
        std::bit_width(age / 1000), DataBufferStats::hist_buckets - 1);
    m_stats.wake_hist[bucket]++;
  }

  bool m_end_marked;
  bool m_overflowing = false; // the previous push dropped a block
  std::uint64_t m_dropped_blocks = 0;
  std::size_t m_max_queue_size = 0;
  std::queue<Entry> m_queue;
  // Timing statistics, all under m_mutex except the atomic period.
  DataBufferStats m_stats;
  bool m_waiting = false; // the consumer is blocked in pull()
  bool m_have_last_push = false;
  std::int64_t m_last_push_ns = 0;
  std::atomic<std::uint64_t> m_period_ns{0};
  // mutable: locked from the const accessors above (dropped_blocks(),
  // queue_size(), pull_end_reached(), stats()), which only read state under the
  // lock.
  mutable std::mutex m_mutex;
  std::condition_variable m_cond;
};

#endif
