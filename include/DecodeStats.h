// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2026 Kenji Rikitake, JJ1BDX
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

#ifndef INCLUDE_DECODESTATS_H
#define INCLUDE_DECODESTATS_H

#include <atomic>
#include <cstdint>
#include <fmt/format.h>
#include <string>

/// Format a duration: microseconds below 0.1 ms, otherwise milliseconds.
inline std::string format_ns(std::uint64_t ns) {
  if (ns < 100000) {
    return fmt::format("{}us", (ns + 500) / 1000);
  }
  return fmt::format("{:.1f}ms", static_cast<double>(ns) * 1.0e-6);
}

/// Decode time per block, written by the decode thread only and read by the
/// printer thread. Relaxed atomics: no lock on the decode path, and the
/// fields are not mutually consistent at any instant.
class DecodeStats {
public:
  struct Snapshot {
    std::uint64_t blocks = 0;
    std::uint64_t total_ns = 0;
    std::uint64_t max_ns = 0;
    std::uint64_t over_computation = 0;
    std::uint64_t over_period = 0;
    std::uint64_t period_ns = 0;
    std::uint64_t computation_ns = 0; // 0 without --realtime
    bool policy_lost = false;
    std::uint64_t policy_lost_block = 0;
  };

  /// Set the block period and the kernel's computation value (0 if none).
  void set_thresholds(std::uint64_t period_ns, std::uint64_t computation_ns) {
    m_period_ns.store(period_ns, std::memory_order_relaxed);
    m_computation_ns.store(computation_ns, std::memory_order_relaxed);
  }

  /// Record the decode time of one block (decode thread only).
  void record(std::uint64_t ns) {
    m_blocks.store(m_blocks.load(std::memory_order_relaxed) + 1,
                   std::memory_order_relaxed);
    m_total_ns.store(m_total_ns.load(std::memory_order_relaxed) + ns,
                     std::memory_order_relaxed);
    if (ns > m_max_ns.load(std::memory_order_relaxed)) {
      m_max_ns.store(ns, std::memory_order_relaxed);
    }
    const std::uint64_t comp = m_computation_ns.load(std::memory_order_relaxed);
    if (comp > 0 && ns > comp) {
      m_over_computation.store(
          m_over_computation.load(std::memory_order_relaxed) + 1,
          std::memory_order_relaxed);
    }
    const std::uint64_t period = m_period_ns.load(std::memory_order_relaxed);
    if (period > 0 && ns > period) {
      m_over_period.store(m_over_period.load(std::memory_order_relaxed) + 1,
                          std::memory_order_relaxed);
    }
  }

  /// Note that the decode thread lost its real-time policy (decode thread).
  void mark_policy_lost(std::uint64_t block) {
    m_policy_lost_block.store(block, std::memory_order_relaxed);
    m_policy_lost.store(true, std::memory_order_release);
  }

  Snapshot snapshot() const {
    Snapshot s;
    s.policy_lost = m_policy_lost.load(std::memory_order_acquire);
    s.policy_lost_block = m_policy_lost_block.load(std::memory_order_relaxed);
    s.blocks = m_blocks.load(std::memory_order_relaxed);
    s.total_ns = m_total_ns.load(std::memory_order_relaxed);
    s.max_ns = m_max_ns.load(std::memory_order_relaxed);
    s.over_computation = m_over_computation.load(std::memory_order_relaxed);
    s.over_period = m_over_period.load(std::memory_order_relaxed);
    s.period_ns = m_period_ns.load(std::memory_order_relaxed);
    s.computation_ns = m_computation_ns.load(std::memory_order_relaxed);
    return s;
  }

private:
  std::atomic<std::uint64_t> m_blocks{0};
  std::atomic<std::uint64_t> m_total_ns{0};
  std::atomic<std::uint64_t> m_max_ns{0};
  std::atomic<std::uint64_t> m_over_computation{0};
  std::atomic<std::uint64_t> m_over_period{0};
  std::atomic<std::uint64_t> m_period_ns{0};
  std::atomic<std::uint64_t> m_computation_ns{0};
  std::atomic<std::uint64_t> m_policy_lost_block{0};
  std::atomic<bool> m_policy_lost{false};
};

#endif // INCLUDE_DECODESTATS_H
