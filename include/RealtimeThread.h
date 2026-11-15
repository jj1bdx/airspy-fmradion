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

#ifndef INCLUDE_REALTIMETHREAD_H
#define INCLUDE_REALTIMETHREAD_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// Real-time scheduling of the decode thread (macOS only): a time-constraint
/// policy and, if requested, an interval workgroup that is started and
/// finished once per block. The constraint equals the block period; the
/// computation is half the constraint (the kernel's minimum), clamped to
/// 50 ms. On other platforms start() only prints a warning.
/// start(), stop(), policy_held() and Cycle must all run on one thread.
class RealtimeThread {
public:
  /// Prepare for a block period of period_sec. use_workgroup also creates and
  /// joins the interval workgroup; without it Cycle does nothing. quiet
  /// suppresses the info line.
  RealtimeThread(double period_sec, bool use_workgroup, bool quiet);

  /// Leave the workgroup if joined.
  ~RealtimeThread();

  RealtimeThread(const RealtimeThread &) = delete;
  RealtimeThread &operator=(const RealtimeThread &) = delete;

  /// Apply the policy to the calling thread, then join the workgroup if it
  /// was requested. Return false if the policy failed; a workgroup failure
  /// only warns.
  bool start();

  /// Leave and release the workgroup, restore normal scheduling of the thread
  /// that called start(), and print the workgroup error count. Call it on that
  /// thread; safe to call twice.
  void stop();

  /// Computation time in ns that the kernel reports after start(); else 0.
  std::uint64_t computation_ns() const;

  /// Every few seconds, check that the calling thread still has the
  /// time-constraint policy; return false once it has been lost.
  bool policy_held();

  /// Mark one block cycle: begin on construction, finish on destruction or
  /// finish(). A null RealtimeThread, or one without a joined workgroup,
  /// makes this a no-op.
  class Cycle {
  public:
    explicit Cycle(RealtimeThread *rt) : m_rt(rt) {
      if (m_rt != nullptr) {
        m_rt->begin_cycle();
      }
    }
    ~Cycle() { finish(); }
    void finish() {
      if (m_rt != nullptr) {
        m_rt->end_cycle();
        m_rt = nullptr;
      }
    }
    Cycle(const Cycle &) = delete;
    Cycle &operator=(const Cycle &) = delete;

  private:
    RealtimeThread *m_rt;
  };

private:
  void begin_cycle();
  void end_cycle();

  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

/// Time-constraint policy for SDR producer threads (macOS only). Constraint
/// is the block period; computation is half of it, as in RealtimeThread. The
/// workgroup is not joined. Failures only print a warning. On other platforms
/// every call does nothing. Created by the main thread before the source
/// thread starts; the member functions are thread-safe.
class ProducerPolicy {
public:
  /// Threads of this task at one moment. Holds one Mach send right per thread.
  class ThreadSet {
  public:
    ThreadSet() = default;
    ~ThreadSet() { release(); }
    ThreadSet(ThreadSet &&) noexcept = default;
    ThreadSet &operator=(ThreadSet &&) = delete;
    ThreadSet(const ThreadSet &) = delete;
    ThreadSet &operator=(const ThreadSet &) = delete;

  private:
    friend class ProducerPolicy;
    void release();
    std::vector<std::uint32_t> m_ports; // Mach thread ports; 0 = taken
    std::vector<std::uint64_t> m_ids;   // unique thread ids
  };

  /// name appears in messages; period_sec is the block period.
  ProducerPolicy(const char *name, double period_sec, bool quiet);
  ~ProducerPolicy();

  ProducerPolicy(const ProducerPolicy &) = delete;
  ProducerPolicy &operator=(const ProducerPolicy &) = delete;

  /// Apply the policy to the calling thread; how is named in the startup
  /// line. Return true on success.
  bool apply_self(const char *how = "source thread");

  /// Take the list of threads of this task.
  static ThreadSet snapshot();

  /// Apply the policy to the threads in after that are not in before, only if
  /// there are exactly expected of them; otherwise apply to none, warn, and
  /// return false. Takes the applied threads' ports out of after.
  bool apply_new(const ThreadSet &before, ThreadSet &after,
                 std::size_t expected);

  /// The source thread calls this when it has finished applying the policy,
  /// on every path (also when the start call or the policy failed). The
  /// constructor calls it itself if the period is invalid.
  void mark_ready();

  /// Wait until mark_ready(), at most timeout. A timeout prints a warning.
  /// Return true if the source was ready.
  bool wait_ready(std::chrono::milliseconds timeout);

  /// Call at the start of every data callback. Until the first block has
  /// been handled it records the calling (consumer) thread, and applies the
  /// policy to it if apply_new() fell back. A single atomic load afterwards.
  void on_callback() {
    if (!m_callback_seen.load(std::memory_order_acquire)) {
      first_callback();
    }
  }

  /// Call periodically from a thread outside the data path: prints the
  /// startup line once the result is known, and re-checks the policy of the
  /// applied threads every few seconds.
  void tick();

  /// Re-read the policy of the applied threads now.
  void check();

  /// One line for the exit summary; empty if no policy was applied.
  std::string summary();

private:
  void first_callback();

  struct Impl;
  std::unique_ptr<Impl> m_impl;
  std::atomic<bool> m_callback_seen{false};
  std::atomic<bool> m_decided{false}; // apply_new() has chosen B or A
  std::atomic<bool> m_fallback{false};
};

#endif // INCLUDE_REALTIMETHREAD_H
